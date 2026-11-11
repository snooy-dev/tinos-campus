// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Engine;

import <cassert>;

Engine::~Engine()
{
    Shutdown();
}

void Engine::Shutdown()
{
    stopping_ = true;
    game_.Shutdown();
    jobs_.Drain();
    game_.Destroy();
    renderer_.Shutdown();
    jobs_.Shutdown();
    application_.Shutdown();
    game_.Unload();
    initialized_ = false;
}

std::expected<void, std::string> Engine::Initialize()
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

    stopping_ = false;
    const auto game_result = game_.Initialize(*this);
    if (!game_result)
    {
        Shutdown();
        return std::unexpected(game_result.error());
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

        if (state.framebuffer_width == 0 || state.framebuffer_height == 0)
            continue;
        const float aspect_ratio = static_cast<float>(state.framebuffer_width) /
            static_cast<float>(state.framebuffer_height);
        auto frame = game_.Update(delta_seconds, aspect_ratio);
        Simulate(delta_seconds);
        renderer_.SubmitFrame({ state.framebuffer_width, state.framebuffer_height,
            frame.view_projection, std::move(frame.meshes) });
    }

    Shutdown();
}

ApplicationState Engine::ProcessInput()
{
    return application_.ProcessEvents();
}

MeshHandle Engine::LoadMeshAsync(std::filesystem::path path)
{
    if (stopping_)
        return {};
    return renderer_.LoadMeshAsync(std::move(path));
}

void Engine::Simulate(double)
{
    // Physics and other simulation systems will be implemented here.
}
