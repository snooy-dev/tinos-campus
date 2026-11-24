// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Application;

import <GLFW/glfw3.h>;
import std;
import Input;

export struct ApplicationState
{
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    bool focused = false;
    bool minimized = false;
    bool close_requested = true;
};

// Owned exclusively by Engine; only one Application may exist per process.
// Window lifecycle and events belong to the main thread; the renderer owns the context.
export class Application
{
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    [[nodiscard]] std::expected<void, const char*> Initialize();
    void Shutdown();
    [[nodiscard]] GLFWwindow* Window() const { return window_; }
    // Requires successful initialization; called on the main thread.
    ApplicationState ProcessEvents();
    [[nodiscard]] Input SampleInput();
    void WaitEvents();
    [[nodiscard]] std::expected<void, const char*> ActivateContext();
    void ReleaseContext();
    // Requires successful initialization and this application's current context.
    void Present();

private:
    const std::thread::id owner_thread_{ std::this_thread::get_id() };
    GLFWwindow* window_ = nullptr;
    ApplicationState state_;
    bool orbiting_ = false;
    double cursor_x_ = 0.0;
    double cursor_y_ = 0.0;
};
