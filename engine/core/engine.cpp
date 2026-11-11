// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Engine;

import <cassert>;

Engine::~Engine()
{
    Shutdown();
}

void Engine::Shutdown()
{
    jobs_.Drain();
    renderer_.Shutdown();
    jobs_.Shutdown();
    application_.Shutdown();
    initialized_ = false;
}

std::expected<void, const char*> Engine::Initialize()
{
    if (initialized_)
        return {};

    const auto application_result = application_.Initialize();
    if (!application_result)
        return std::unexpected(application_result.error());

    jobs_.Initialize();
    const auto renderer_result = renderer_.Initialize(application_, jobs_);
    if (!renderer_result)
    {
        Shutdown();
        return std::unexpected(renderer_result.error());
    }

    initialized_ = true;
    return {};
}

void Engine::Run()
{
    assert(initialized_);
    std::optional<std::chrono::steady_clock::time_point> previous_time;
    while (true)
    {
        renderer_.WaitForFrameSlot();
        const auto state = ProcessInput();
        if (state.close_requested)
        {
            break;
        }
        if (state.minimized)
        {
            previous_time.reset();
            application_.WaitEvents();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const double delta_seconds = previous_time ?
            std::chrono::duration<double>(now - *previous_time).count() : 0.0;
        previous_time = now;

        Update(delta_seconds);
        Simulate(delta_seconds);
        scene_.UpdateWorldTransforms();
        renderer_.SubmitFrame({ state.framebuffer_width, state.framebuffer_height,
            view_projection_, scene_.CollectRenderItems() });
    }

    Shutdown();
}

ApplicationState Engine::ProcessInput()
{
    return application_.ProcessEvents();
}

void Engine::Update(double delta_seconds)
{
    scene_.Update(delta_seconds);
}

void Engine::Simulate(double)
{
    // Physics and other simulation systems will be implemented here.
}
