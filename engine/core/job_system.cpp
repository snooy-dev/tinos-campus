// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module JobSystem;

import Debug;

namespace
{
thread_local JobSystem* worker_system = nullptr;
thread_local JobTarget worker_target = JobTarget::Cpu;
thread_local bool executing_task = false;
thread_local std::size_t worker_index = 0;
void Require(bool condition)
{
    if (!condition) Debug::ReportCrash("Job system contract violation");
}
}

bool JobHandle::IsComplete() const
{
    return !state_ || state_->complete.load(std::memory_order_acquire);
}

void JobHandle::Wait() const
{
    Require(!worker_system);
    if (!state_) return;
    Require(std::this_thread::get_id() == state_->generation->owner);
    state_->complete.wait(false, std::memory_order_acquire);
}

JobSystem::JobSystem() = default;

JobSystem::~JobSystem() { Shutdown(); }

void JobSystem::Initialize(unsigned cpu_workers, unsigned io_workers)
{
    Require(std::this_thread::get_id() == owner_ && !worker_system);
    if (generation_)
    {
        Require(!draining_.load());
        return;
    }
    Require(io_workers != 0);
    if (!cpu_workers)
    {
        const auto available = std::thread::hardware_concurrency();
        cpu_workers = available > 2 ? available - 2 : 1;
    }
#ifdef _DEBUG
    executed_ = waiting_ = ready_ = attempts_ = steals_ = queue_ns_ = execution_ns_ = 0;
#endif
    generation_ = std::make_shared<JobGeneration>(owner_);
    stopping_.store(false);
    draining_.store(false);
    render_stopping_.store(false);
    last_render_ = {};
    for (unsigned i = 0; i < cpu_workers; ++i)
        cpu_.local.push_back(std::make_unique<Queue>());
    const auto handler = std::get_terminate();
    for (unsigned i = 0; i < cpu_workers; ++i)
        cpu_.threads.emplace_back([this, i, handler] { std::set_terminate(handler); Run(JobTarget::Cpu, i); });
    for (unsigned i = 0; i < io_workers; ++i)
        io_.threads.emplace_back([this, i, handler] { std::set_terminate(handler); Run(JobTarget::Io, i); });
}

void JobSystem::Drain()
{
    Require(std::this_thread::get_id() == owner_ && !worker_system);
    if (!generation_) return;
    draining_.store(true, std::memory_order_release);
    auto pending = outstanding_.load(std::memory_order_acquire);
    while (pending)
    {
        outstanding_.wait(pending, std::memory_order_acquire);
        pending = outstanding_.load(std::memory_order_acquire);
    }
}

void JobSystem::Shutdown()
{
    Require(std::this_thread::get_id() == owner_ && !worker_system);
    Require(!render_attached_.load(std::memory_order_acquire));
    if (!generation_) return;
    Drain();
    stopping_.store(true, std::memory_order_release);
    for (auto* pool : { &cpu_, &io_ })
    {
        pool->epoch.fetch_add(1, std::memory_order_release);
        pool->epoch.notify_all();
        pool->threads.clear();
        pool->local.clear();
    }
    last_render_ = {};
    generation_.reset();
}

JobHandle JobSystem::SubmitCpu(std::move_only_function<void()> job, std::span<const JobHandle> prerequisites)
{
    return Submit(std::move(job), prerequisites, JobTarget::Cpu);
}

JobHandle JobSystem::SubmitIo(std::move_only_function<void()> job, std::span<const JobHandle> prerequisites)
{
    return Submit(std::move(job), prerequisites, JobTarget::Io);
}

JobHandle JobSystem::SubmitRender(std::move_only_function<void()> job,
    std::span<const JobHandle> prerequisites)
{
    const std::lock_guard lock(render_submission_mutex_);
    auto handle = Submit(std::move(job), prerequisites, JobTarget::Render, last_render_.state_);
    last_render_ = handle;
    return handle;
}

void JobSystem::AttachRenderThread()
{
    Require(!worker_system && std::this_thread::get_id() != owner_);
    Require(generation_ && !draining_.load());
    bool expected = false;
    Require(render_attached_.compare_exchange_strong(expected, true));
    worker_system = this;
    worker_target = JobTarget::Render;
}

void JobSystem::RunRenderTasks()
{
    Require(worker_system == this && worker_target == JobTarget::Render && !executing_task);
    Run(JobTarget::Render, 0);
}

void JobSystem::StopRenderTasks()
{
    Require(std::this_thread::get_id() == owner_ && !worker_system);
    Require(draining_.load() && outstanding_.load(std::memory_order_acquire) == 0);
    render_stopping_.store(true, std::memory_order_release);
    render_.epoch.fetch_add(1, std::memory_order_release);
    render_.epoch.notify_one();
}

JobHandle JobSystem::Submit(std::move_only_function<void()> body,
    std::span<const JobHandle> prerequisites, JobTarget target,
    const std::shared_ptr<JobState>& ordered_parent)
{
    Require(worker_system == this || (!worker_system && std::this_thread::get_id() == owner_));
    Require(generation_ && bool(body));
    Require(!draining_.load(std::memory_order_acquire) || (worker_system == this && executing_task));
    Require(target != JobTarget::Render || render_attached_.load(std::memory_order_acquire));
    for (const auto& parent : prerequisites)
        Require(!parent.state_ || parent.state_->generation == generation_);
    auto job = std::make_shared<JobState>();
    job->generation = generation_;
    job->body = std::move(body);
    job->target = target;
    outstanding_.fetch_add(1, std::memory_order_relaxed);
#ifdef _DEBUG
    waiting_.fetch_add(1, std::memory_order_relaxed);
#endif
    const auto connect = [&job](const std::shared_ptr<JobState>& parent)
    {
        if (!parent) return;
        std::lock_guard lock(parent->mutex);
        if (!parent->complete.load(std::memory_order_relaxed))
        {
            job->remaining.fetch_add(1, std::memory_order_relaxed);
            parent->successors.push_back(job);
        }
    };
    for (const auto& parent : prerequisites) connect(parent.state_);
    connect(ordered_parent);
    // Registration owns one dependency until every predecessor has been inspected.
    Ready(job);
    return JobHandle(job);
}

void JobSystem::Ready(const std::shared_ptr<JobState>& job)
{
    if (job->remaining.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
#ifdef _DEBUG
    waiting_.fetch_sub(1, std::memory_order_relaxed);
    ready_.fetch_add(1, std::memory_order_relaxed);
    job->queued = std::chrono::steady_clock::now();
#endif
    auto& pool = job->target == JobTarget::Render ? render_ :
        (job->target == JobTarget::Io ? io_ : cpu_);
    auto& queue = job->target == JobTarget::Cpu && worker_system == this &&
        worker_target == JobTarget::Cpu ?
        *cpu_.local[worker_index] : pool.incoming;
    {
        std::lock_guard lock(queue.mutex);
        queue.jobs.push_back(job);
    }
    pool.epoch.fetch_add(1, std::memory_order_release);
    pool.epoch.notify_one();
}

std::shared_ptr<JobState> JobSystem::Pop(Queue& queue, bool back)
{
    std::lock_guard lock(queue.mutex);
    if (queue.jobs.empty()) return {};
    auto job = back ? std::move(queue.jobs.back()) : std::move(queue.jobs.front());
    if (back) queue.jobs.pop_back(); else queue.jobs.pop_front();
    return job;
}

void JobSystem::Run(JobTarget target, std::size_t index)
{
    worker_system = this;
    worker_target = target;
    worker_index = index;
    auto& pool = target == JobTarget::Render ? render_ : (target == JobTarget::Io ? io_ : cpu_);
    std::size_t victim = index, local_streak = 0;
    while (true)
    {
        const auto epoch = pool.epoch.load(std::memory_order_acquire);
        std::shared_ptr<JobState> job;
        if (target == JobTarget::Cpu)
        {
            if (local_streak >= 8)
            {
                job = Pop(pool.incoming, false);
                local_streak = 0;
            }
            if (!job)
            {
                job = Pop(*pool.local[index], true);
                if (job) ++local_streak;
            }
        }
        if (!job) job = Pop(pool.incoming, false);
        if (!job && target == JobTarget::Cpu)
            for (std::size_t i = 0; i < pool.local.size(); ++i)
            {
                victim = (victim + 1) % pool.local.size();
                if (victim != index)
                {
#ifdef _DEBUG
                    attempts_.fetch_add(1, std::memory_order_relaxed);
#endif
                    job = Pop(*pool.local[victim], false);
#ifdef _DEBUG
                    if (job) steals_.fetch_add(1, std::memory_order_relaxed);
#endif
                }
                if (job) break;
            }
        if (!job)
        {
            const bool stop = target == JobTarget::Render ?
                render_stopping_.load(std::memory_order_acquire) : stopping_.load(std::memory_order_acquire);
            if (stop) break;
            pool.epoch.wait(epoch, std::memory_order_acquire);
            continue;
        }
#ifdef _DEBUG
        const auto start = std::chrono::steady_clock::now();
        ready_.fetch_sub(1, std::memory_order_relaxed);
        queue_ns_.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(start - job->queued).count(),
            std::memory_order_relaxed);
#endif
        executing_task = true;
        job->body();
        job->body = nullptr;
        executing_task = false;
#ifdef _DEBUG
        execution_ns_.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count(),
            std::memory_order_relaxed);
        executed_.fetch_add(1, std::memory_order_relaxed);
#endif
        std::vector<std::shared_ptr<JobState>> successors;
        {
            std::lock_guard lock(job->mutex);
            successors.swap(job->successors);
            job->complete.store(true, std::memory_order_release);
        }
        job->complete.notify_all();
        for (const auto& successor : successors) Ready(successor);
        if (outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            outstanding_.notify_one();
    }
    worker_system = nullptr;
    if (target == JobTarget::Render)
        render_attached_.store(false, std::memory_order_release);
}

JobHandle JobSystem::ParallelFor(std::size_t count, std::size_t grain,
    std::move_only_function<void(std::size_t, std::size_t)> body,
    std::span<const JobHandle> prerequisites)
{
    Require(worker_system == this || (!worker_system && std::this_thread::get_id() == owner_));
    Require(grain && bool(body) && generation_);
    struct Range
    {
        std::atomic<std::size_t> next{0};
        std::move_only_function<void(std::size_t, std::size_t)> body;
    };
    auto range = std::make_shared<Range>();
    range->body = std::move(body);
    std::vector<JobHandle> runners;
    const auto workers = count ? std::min(cpu_.local.size(), 1 + (count - 1) / grain) : 0;
    for (std::size_t i = 0; i < workers; ++i)
        runners.push_back(SubmitCpu([range, count, grain]
        {
            auto begin = range->next.load(std::memory_order_relaxed);
            while (begin < count)
            {
                const auto end = begin + std::min(grain, count - begin);
                if (range->next.compare_exchange_weak(begin, end, std::memory_order_relaxed))
                {
                    range->body(begin, end);
                    begin = range->next.load(std::memory_order_relaxed);
                }
            }
        }, prerequisites));
    return SubmitCpu([] {}, runners.empty() ? prerequisites : std::span<const JobHandle>(runners));
}

#ifdef _DEBUG
JobStatistics JobSystem::Statistics() const
{
    return { executed_.load(), waiting_.load(), ready_.load(), attempts_.load(),
        steals_.load(), queue_ns_.load(), execution_ns_.load() };
}
#endif
