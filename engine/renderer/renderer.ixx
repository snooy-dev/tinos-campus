// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Renderer;

import std;
import Application;

export struct RenderFrame
{
    int framebuffer_width = 0;
    int framebuffer_height = 0;
};

// Lifecycle and submission belong to the main thread; Application must outlive Renderer.
export class Renderer
{
public:
    ~Renderer();

    [[nodiscard]] std::expected<void, const char*> Initialize(Application& application);
    void Shutdown();
    // Requires successful initialization and nonnegative framebuffer dimensions.
    void SubmitFrame(RenderFrame frame);

private:
    void Run();
    void Render(const RenderFrame& frame);

    const std::thread::id owner_thread_{ std::this_thread::get_id() };
    Application* application_ = nullptr;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<RenderFrame> pending_frame_;
    std::expected<void, const char*> initialization_result_;
    bool ready_ = false;
    bool stopping_ = false;
    std::jthread thread_;
};
