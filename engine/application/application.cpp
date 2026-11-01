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

std::expected<void, const char*> Application::Initialize()
{
    assert(std::this_thread::get_id() == owner_thread_);
    if (window_)
    {
        assert(glfwGetCurrentContext() == window_);
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
            auto& state = static_cast<Application*>(glfwGetWindowUserPointer(window))->state_;
            state.focused = focused == GLFW_TRUE;
        });
    glfwSetWindowIconifyCallback(window_, [](GLFWwindow* window, int minimized)
        {
            auto& state = static_cast<Application*>(glfwGetWindowUserPointer(window))->state_;
            state.minimized = minimized == GLFW_TRUE;
        });

    glfwGetFramebufferSize(window_, &state_.framebuffer_width, &state_.framebuffer_height);
    state_.focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    state_.minimized = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
    state_.close_requested = false;
    glfwMakeContextCurrent(window_);
    if (glfwGetCurrentContext() != window_)
    {
        Shutdown();
        return std::unexpected("OpenGL context activation failed.");
    }

    glfwSwapInterval(1);
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
}

ApplicationState Application::ProcessEvents()
{
    assert(std::this_thread::get_id() == owner_thread_);
    assert(window_ != nullptr);

    glfwPollEvents();
    if (state_.minimized && !glfwWindowShouldClose(window_))
        glfwWaitEvents();

    state_.close_requested = glfwWindowShouldClose(window_) == GLFW_TRUE;

    return state_;
}

void Application::Present()
{
    assert(std::this_thread::get_id() == owner_thread_);
    assert(window_ != nullptr);
    assert(glfwGetCurrentContext() == window_);
    glfwSwapBuffers(window_);
}
