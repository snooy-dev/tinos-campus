// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Renderer;

import std;
import Application;
import JobSystem;

export struct RenderFrame
{
    int framebuffer_width = 0;
    int framebuffer_height = 0;
};

// Lifecycle and frame submission belong to the main thread; Application and JobSystem must outlive Renderer.
export class Renderer
{
public:
    ~Renderer();

    [[nodiscard]] std::expected<void, const char*> Initialize(Application& application, JobSystem& jobs);
    // Drains the shared job system and closes its external submissions before detaching.
    void Shutdown();
    // Wait before sampling input; SubmitFrame also enforces the two-slot limit.
    void WaitForFrameSlot();
    // Completion covers the render task, not GPU execution or display scanout.
    JobHandle SubmitFrame(RenderFrame frame, std::span<const JobHandle> prerequisites = {});

private:
    void Run();
    void Render(const RenderFrame& frame);

    const std::thread::id owner_thread_{ std::this_thread::get_id() };
    Application* application_ = nullptr;
    JobSystem* jobs_ = nullptr;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::array<JobHandle, 2> frame_fences_;
    std::size_t next_frame_ = 0;
    std::expected<void, const char*> initialization_result_;
    bool ready_ = false;
    std::jthread thread_;
};
