// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module JobSystem;

import Debug;

namespace
{
thread_local JobSystem* worker_system = nullptr;
thread_local bool worker_io = false;
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
    if (generation_) return;
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
    for (unsigned i = 0; i < cpu_workers; ++i)
        cpu_.local.push_back(std::make_unique<Queue>());
    const auto handler = std::get_terminate();
    for (unsigned i = 0; i < cpu_workers; ++i)
        cpu_.threads.emplace_back([this, i, handler] { std::set_terminate(handler); Run(false, i); });
    for (unsigned i = 0; i < io_workers; ++i)
        io_.threads.emplace_back([this, i, handler] { std::set_terminate(handler); Run(true, i); });
}

void JobSystem::Shutdown()
{
    Require(std::this_thread::get_id() == owner_ && !worker_system);
    if (!generation_) return;
    auto pending = outstanding_.load(std::memory_order_acquire);
    while (pending)
    {
        outstanding_.wait(pending, std::memory_order_acquire);
        pending = outstanding_.load(std::memory_order_acquire);
    }
    stopping_.store(true, std::memory_order_release);
    for (auto* pool : { &cpu_, &io_ })
    {
        pool->epoch.fetch_add(1, std::memory_order_release);
        pool->epoch.notify_all();
        pool->threads.clear();
        pool->local.clear();
    }
    generation_.reset();
}

JobHandle JobSystem::SubmitCpu(std::move_only_function<void()> job, std::span<const JobHandle> prerequisites)
{
    return Submit(std::move(job), prerequisites, false);
}

JobHandle JobSystem::SubmitIo(std::move_only_function<void()> job, std::span<const JobHandle> prerequisites)
{
    return Submit(std::move(job), prerequisites, true);
}

JobHandle JobSystem::Submit(std::move_only_function<void()> body, std::span<const JobHandle> prerequisites, bool io)
{
    Require(worker_system == this || (!worker_system && std::this_thread::get_id() == owner_));
    Require(generation_ && bool(body));
    for (const auto& parent : prerequisites)
        Require(!parent.state_ || parent.state_->generation == generation_);
    auto job = std::make_shared<JobState>();
    job->generation = generation_;
    job->body = std::move(body);
    job->io = io;
    outstanding_.fetch_add(1, std::memory_order_relaxed);
#ifdef _DEBUG
    waiting_.fetch_add(1, std::memory_order_relaxed);
#endif
    for (const auto& parent : prerequisites)
    {
        if (!parent.state_) continue;
        std::lock_guard lock(parent.state_->mutex);
        if (!parent.state_->complete.load(std::memory_order_relaxed))
        {
            job->remaining.fetch_add(1, std::memory_order_relaxed);
            parent.state_->successors.push_back(job);
        }
    }
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
    auto& pool = job->io ? io_ : cpu_;
    auto& queue = !job->io && worker_system == this && !worker_io ?
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

void JobSystem::Run(bool io, std::size_t index)
{
    worker_system = this;
    worker_io = io;
    worker_index = index;
    auto& pool = io ? io_ : cpu_;
    std::size_t victim = index, local_streak = 0;
    while (true)
    {
        const auto epoch = pool.epoch.load(std::memory_order_acquire);
        std::shared_ptr<JobState> job;
        if (!io)
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
        if (!job && !io)
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
            if (stopping_.load(std::memory_order_acquire)) break;
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
        job->body();
        job->body = nullptr;
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
        outstanding_.fetch_sub(1, std::memory_order_acq_rel);
        outstanding_.notify_all();
    }
    worker_system = nullptr;
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
