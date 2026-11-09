// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Engine;

import std;
import Application;
import Input;
import Renderer;
import JobSystem;
export import GameFramework;

// Construct, initialize, run and destroy on the main thread.
export class Engine
{
public:
    ~Engine();
    // Failure releases acquired resources and permits another initialization attempt.
    [[nodiscard]] std::expected<void, const char*> Initialize();
    // Requires successful initialization; returns when the window closes.
    void Run();
    [[nodiscard]] MeshHandle LoadMeshAsync(std::filesystem::path path)
    {
        return renderer_.LoadMeshAsync(std::move(path));
    }
    void SetViewProjection(const glm::mat4& matrix) { view_projection_ = matrix; }
    [[nodiscard]] Scene& GetScene() { return scene_; }

private:
    void Shutdown();
    ApplicationState ProcessInput();
    void Update(double delta_seconds);
    void Simulate(double delta_seconds);

    glm::mat4 view_projection_{ 1.0f };
    Scene scene_;
    Application application_;
    Input input_;
    JobSystem jobs_;
    Renderer renderer_;
    bool initialized_ = false;
};
