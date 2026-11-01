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

    const auto renderer_result = renderer_.Initialize();
    if (!renderer_result)
    {
        application_.Shutdown();
        return std::unexpected(renderer_result.error());
    }

    initialized_ = true;
    return {};
}

void Engine::Run()
{
    assert(initialized_);
    while (true)
    {
        const auto state = application_.ProcessEvents();
        if (state.close_requested)
        {
            break;
        }
        if (state.minimized)
            continue;

        renderer_.Clear(state.framebuffer_width, state.framebuffer_height);
        application_.Present();
    }
}
