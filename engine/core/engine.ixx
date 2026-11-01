// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Engine;

import std;
import Application;
import Input;
import Renderer;

// Construct, initialize, run and destroy on the main thread.
export class Engine
{
public:
    // Failure releases acquired resources and permits another initialization attempt.
    [[nodiscard]] std::expected<void, const char*> Initialize();
    // Requires successful initialization; returns when the window closes.
    void Run();

private:
    Application application_;
    Input input_;
    Renderer renderer_;
    bool initialized_ = false;
};
