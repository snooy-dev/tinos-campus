// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Renderer;

import <GL/glew.h>;
import <cassert>;

Renderer::~Renderer()
{
    Shutdown();
}

std::expected<void, const char*> Renderer::Initialize(Application& application, JobSystem& jobs)
{
    assert(std::this_thread::get_id() == owner_thread_);
    if (thread_.joinable())
    {
        assert(application_ == &application && jobs_ == &jobs);
        return {};
    }

    application_ = &application;
    jobs_ = &jobs;
    const auto terminate_handler = std::get_terminate();
    thread_ = std::jthread([this, terminate_handler]
        {
            std::set_terminate(terminate_handler);
            Run();
        });

    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return ready_; });
    const auto result = initialization_result_;
    lock.unlock();
    if (!result)
        Shutdown();
    return result;
}

void Renderer::Shutdown()
{
    assert(std::this_thread::get_id() == owner_thread_);
    if (!thread_.joinable())
        return;

    jobs_->Drain();
    jobs_->StopRenderTasks();
    thread_.join();
    application_ = nullptr;
    jobs_ = nullptr;
    frame_fences_ = {};
    next_frame_ = 0;
    ready_ = false;
    initialization_result_ = {};
}

void Renderer::WaitForFrameSlot()
{
    assert(std::this_thread::get_id() == owner_thread_);
    assert(thread_.joinable());
    frame_fences_[next_frame_].Wait();
}

JobHandle Renderer::SubmitFrame(RenderFrame frame, std::span<const JobHandle> prerequisites)
{
    assert(frame.framebuffer_width >= 0 && frame.framebuffer_height >= 0);
    WaitForFrameSlot();
    auto handle = jobs_->SubmitRender([this, frame] { Render(frame); }, prerequisites);
    frame_fences_[next_frame_] = handle;
    next_frame_ = (next_frame_ + 1) % frame_fences_.size();
    return handle;
}

void Renderer::Run()
{
    const auto context_result = application_->ActivateContext();
    auto result = context_result;
    if (result)
    {
        glewExperimental = GL_TRUE;
        const GLenum error_code = glewInit();
        if (error_code != GLEW_OK)
            result = std::unexpected(
                reinterpret_cast<const char*>(glewGetErrorString(error_code)));
        else
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    }

    if (result)
        jobs_->AttachRenderThread();

    {
        const std::lock_guard lock(mutex_);
        initialization_result_ = result;
        ready_ = true;
    }
    condition_.notify_one();

    if (result)
        jobs_->RunRenderTasks();

    if (context_result)
        application_->ReleaseContext();
}

void Renderer::Render(const RenderFrame& frame)
{
    assert(std::this_thread::get_id() != owner_thread_);
    if (frame.framebuffer_width == 0 || frame.framebuffer_height == 0)
        return;

    glViewport(0, 0, frame.framebuffer_width, frame.framebuffer_height);
    glClear(GL_COLOR_BUFFER_BIT);
    application_->Present();
}
