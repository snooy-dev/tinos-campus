// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Renderer;

import std;

// Initialize and render with Application's context current on the calling thread.
export class Renderer
{
public:
    [[nodiscard]] std::expected<void, const char*> Initialize();
    // Requires successful initialization and nonnegative framebuffer dimensions.
    void Clear(int width, int height);
};
