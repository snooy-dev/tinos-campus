// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Renderer;

import <GL/glew.h>;
import <cassert>;

std::expected<void, const char*> Renderer::Initialize()
{
    glewExperimental = GL_TRUE;
    const GLenum error_code = glewInit();
    if (error_code != GLEW_OK)
    {
        return std::unexpected(
            reinterpret_cast<const char*>(glewGetErrorString(error_code)));
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    return {};
}

void Renderer::Clear(int width, int height)
{
    assert(width >= 0 && height >= 0);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
}
