// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Application;

import <cassert>;
import <GLFW/glfw3.h>;

import "core/log.hpp";

namespace
{
constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 720;
constexpr char kWindowTitle[] = "Tino's Campus";
bool glfw_in_use = false;
}

Application::~Application()
{
    Shutdown();
}

std::expected<void, const char*> Application::Initialize(bool enable_game_ui)
{
    assert(std::this_thread::get_id() == owner_thread_);
    if (window_)
    {
        return {};
    }
    if (glfw_in_use)
    {
        return std::unexpected("Another Application already owns GLFW.");
    }

#ifdef _DEBUG
    glfwSetErrorCallback([](int error_code, const char* description)
        {
            LOG(Error, "GLFW error {}: {}", error_code, description);
        });
#endif

    if (!glfwInit())
    {
        return std::unexpected("GLFW initialization failed.");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(kInitialWindowWidth, kInitialWindowHeight, kWindowTitle, nullptr, nullptr);
    if (!window_)
    {
        glfwTerminate();
        return std::unexpected("OpenGL 3.3 window creation failed.");
    }

    glfw_in_use = true;
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int width, int height)
        {
            auto& state = static_cast<Application*>(glfwGetWindowUserPointer(window))->state_;
            state.framebuffer_width = width;
            state.framebuffer_height = height;
        });
    glfwSetWindowFocusCallback(window_, [](GLFWwindow* window, int focused)
        {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));
            app.state_.focused = focused == GLFW_TRUE;
            if (!app.state_.focused)
            {
                app.ui_events_.clear();
                app.ui_events_.push_back({ UiEventType::Cancel });
                app.orbit_blocked_until_release_ = true;
            }
        });
    glfwSetWindowIconifyCallback(window_, [](GLFWwindow* window, int minimized)
        {
            auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));
            app.state_.minimized = minimized == GLFW_TRUE;
            if (app.state_.minimized)
            {
                app.ui_events_.clear();
                app.ui_events_.push_back({ UiEventType::Cancel });
                app.orbit_blocked_until_release_ = true;
            }
        });

    if (enable_game_ui)
    {
        glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int)
            {
                auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));
                if (button != GLFW_MOUSE_BUTTON_LEFT || !app.state_.focused ||
                    app.state_.minimized || app.orbiting_)
                    return;
                double x = 0, y = 0;
                glfwGetCursorPos(window, &x, &y);
                app.ui_events_.push_back({ action == GLFW_PRESS ? UiEventType::Press : UiEventType::Release,
                    { static_cast<float>(x), static_cast<float>(y) } });
            });
        glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int, int action, int modifiers)
            {
                auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));
                if (action != GLFW_PRESS || !app.state_.focused || app.state_.minimized)
                    return;
                switch (key)
                {
                case GLFW_KEY_TAB:
                    app.ui_events_.push_back({ modifiers & GLFW_MOD_SHIFT ?
                        UiEventType::Previous : UiEventType::Next });
                    break;
                case GLFW_KEY_ENTER:
                case GLFW_KEY_SPACE:
                    app.ui_events_.push_back({ UiEventType::Activate });
                    break;
                case GLFW_KEY_ESCAPE:
                    app.ui_events_.push_back({ UiEventType::Escape });
                    break;
                }
            });
    }
    glfwGetFramebufferSize(window_, &state_.framebuffer_width, &state_.framebuffer_height);
    state_.focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    state_.minimized = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
    state_.close_requested = false;
    return {};
}

void Application::Shutdown()
{
    assert(std::this_thread::get_id() == owner_thread_);
    if (!window_)
    {
        return;
    }

    // Engine must finish all rendering work before destroying the context.
    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
    glfw_in_use = false;
    state_ = {};
    orbiting_ = false;
    orbit_blocked_until_release_ = false;
    ui_events_.clear();
}

ApplicationState Application::ProcessEvents()
{
    assert(std::this_thread::get_id() == owner_thread_);
    assert(window_ != nullptr);

    glfwPollEvents();
    state_.close_requested = glfwWindowShouldClose(window_) == GLFW_TRUE;

    return state_;
}

Input Application::SampleInput(bool gameplay_paused)
{
    assert(std::this_thread::get_id() == owner_thread_ && window_);
    Input input;
    const bool active = state_.focused && !state_.minimized && !state_.close_requested;
    const bool right_down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    orbit_blocked_until_release_ = right_down && (gameplay_paused || !active || orbit_blocked_until_release_);
    const bool orbit = active && right_down && !orbit_blocked_until_release_;
    if (orbit != orbiting_)
        glfwSetInputMode(window_, GLFW_CURSOR, orbit ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (orbit)
    {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window_, &x, &y);
        if (orbiting_)
        {
            input.orbit_x = x - cursor_x_;
            input.orbit_y = y - cursor_y_;
        }
        cursor_x_ = x;
        cursor_y_ = y;
    }
    orbiting_ = orbit;
    if (active && !gameplay_paused)
    {
        const auto down = [this](int key) { return glfwGetKey(window_, key) == GLFW_PRESS; };
        input.move_right = down(GLFW_KEY_D) - down(GLFW_KEY_A);
        input.move_forward = down(GLFW_KEY_W) - down(GLFW_KEY_S);
    }
    return input;
}

UiInput Application::SampleUiInput()
{
    assert(std::this_thread::get_id() == owner_thread_ && window_);
    int width = 0, height = 0;
    double x = 0, y = 0;
    glfwGetWindowSize(window_, &width, &height);
    glfwGetCursorPos(window_, &x, &y);
    const bool focused = state_.focused && !state_.minimized && !state_.close_requested &&
        width > 0 && height > 0 && state_.framebuffer_width > 0 && state_.framebuffer_height > 0;
    return { { static_cast<float>(width), static_cast<float>(height) },
        { static_cast<float>(x), static_cast<float>(y) }, focused && !orbiting_,
        focused, std::exchange(ui_events_, {}) };
}

void Application::WaitEvents()
{
    assert(std::this_thread::get_id() == owner_thread_);
    assert(window_ != nullptr);
    glfwWaitEvents();
}

std::expected<void, const char*> Application::ActivateContext()
{
    assert(std::this_thread::get_id() != owner_thread_);
    assert(window_ != nullptr);
    glfwMakeContextCurrent(window_);
    if (glfwGetCurrentContext() != window_)
        return std::unexpected("OpenGL context activation failed.");

    glfwSwapInterval(1);
    return {};
}

void Application::ReleaseContext()
{
    assert(std::this_thread::get_id() != owner_thread_);
    assert(glfwGetCurrentContext() == window_);
    glfwMakeContextCurrent(nullptr);
}

void Application::Present()
{
    assert(window_ != nullptr);
    assert(glfwGetCurrentContext() == window_);
    glfwSwapBuffers(window_);
}
