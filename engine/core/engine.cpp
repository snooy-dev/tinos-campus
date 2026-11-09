// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Engine;

import <cassert>;

std::expected<void, const char*> Engine::Initialize()
{
    if (initialized_)
        return {};

    const auto application_result = application_.Initialize();
    if (!application_result)
        return std::unexpected(application_result.error());

    const auto renderer_result = renderer_.Initialize(application_);
    if (!renderer_result)
    {
        application_.Shutdown();
        return std::unexpected(renderer_result.error());
    }

    jobs_.Initialize();
    initialized_ = true;
    return {};
}

void Engine::Run()
{
    assert(initialized_);
    std::optional<std::chrono::steady_clock::time_point> previous_time;
    while (true)
    {
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
        renderer_.SubmitFrame({ state.framebuffer_width, state.framebuffer_height });
    }

    jobs_.Shutdown();
    renderer_.Shutdown();
    application_.Shutdown();
    initialized_ = false;
}

ApplicationState Engine::ProcessInput()
{
    return application_.ProcessEvents();
}

void Engine::Update(double)
{
    // Game updates will be implemented here.
}

void Engine::Simulate(double)
{
    // Physics and other simulation systems will be implemented here.
}
