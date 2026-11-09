// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module JobSystem;

import std;

struct JobGeneration
{
    std::thread::id owner;
};
struct JobState
{
    std::shared_ptr<JobGeneration> generation;
    std::mutex mutex;
    std::vector<std::shared_ptr<JobState>> successors;
    std::atomic<bool> complete{false};
    std::atomic<std::size_t> remaining{1};
    std::move_only_function<void()> body;
    bool io = false;
#ifdef _DEBUG
    std::chrono::steady_clock::time_point queued;
#endif
};

export class JobHandle
{
public:
    JobHandle() = default;
    [[nodiscard]] bool IsComplete() const;
    // Wait belongs to the creating main thread, even for an already completed handle.
    void Wait() const;
private:
    friend class JobSystem;
    explicit JobHandle(std::shared_ptr<JobState> state) : state_(std::move(state)) {}
    std::shared_ptr<JobState> state_;
};

#ifdef _DEBUG
export struct JobStatistics
{
    std::uint64_t executed, waiting, ready, steal_attempts, steals;
    std::uint64_t queue_nanoseconds, execution_nanoseconds;
};
#endif
export class JobSystem
{
public:
    JobSystem();
    ~JobSystem();
#ifdef _DEBUG
    // Independent snapshots are approximate and must not drive synchronization.
    JobStatistics Statistics() const;
#endif
    // Construct, initialize, shut down and destroy on the main thread.
    void Initialize(unsigned cpu_workers = 0, unsigned io_workers = 2);
    void Shutdown();
    // Submit from the owner or its workers; dependencies must belong to this initialization.
    // Captured references must remain valid until completion; dropping a handle does not cancel work.
    JobHandle SubmitCpu(std::move_only_function<void()> job,
        std::span<const JobHandle> prerequisites = {});
    JobHandle SubmitIo(std::move_only_function<void()> job,
        std::span<const JobHandle> prerequisites = {});
    // Body can execute concurrently; referenced data must outlive the returned handle's completion.
    JobHandle ParallelFor(std::size_t count, std::size_t grain_size,
        std::move_only_function<void(std::size_t, std::size_t)> body,
        std::span<const JobHandle> prerequisites = {});
private:
    struct Queue
    {
        std::mutex mutex;
        std::deque<std::shared_ptr<JobState>> jobs;
    };
    struct Pool
    {
        Queue incoming;
        std::vector<std::unique_ptr<Queue>> local;
        std::atomic<std::uint64_t> epoch{0};
        std::vector<std::jthread> threads;
    };
    JobHandle Submit(std::move_only_function<void()> job,
        std::span<const JobHandle> prerequisites, bool io);
    void Ready(const std::shared_ptr<JobState>& job);
    void Run(bool io, std::size_t index);
    static std::shared_ptr<JobState> Pop(Queue& queue, bool back);
#ifdef _DEBUG
    std::atomic<std::uint64_t> executed_{0}, waiting_{0}, ready_{0};
    std::atomic<std::uint64_t> attempts_{0}, steals_{0}, queue_ns_{0}, execution_ns_{0};
#endif
    const std::thread::id owner_{std::this_thread::get_id()};
    std::shared_ptr<JobGeneration> generation_;
    Pool cpu_, io_;
    std::atomic<std::size_t> outstanding_{0};
    std::atomic<bool> stopping_{false};
};
