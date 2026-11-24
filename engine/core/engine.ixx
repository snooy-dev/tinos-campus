// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Engine;

import std;
import Application;
import Renderer;
import JobSystem;
import GameModule;
import GameApi;

// Construct, initialize, run and destroy on the main thread.
export class Engine : private IEngineServices, private IUiAssets
{
public:
    ~Engine();
    // Failure releases acquired resources and permits another initialization attempt.
    [[nodiscard]] std::expected<void, std::string> Initialize();
    // Requires successful initialization; returns when the window closes.
    void Run();

private:
    void Shutdown();
    MeshHandle LoadMeshAsync(std::filesystem::path path) override;
    std::expected<UiTextureHandle, const char*> LoadUiImage(const std::filesystem::path& path) override;
    std::expected<UiTextureHandle, const char*> CreateUiText(const std::wstring& family,
        float size, std::wstring_view text) override;
    void Simulate(double delta_seconds);

    GameModule game_;
    Application application_;
    JobSystem jobs_;
    Renderer renderer_;
    bool initialized_ = false;
    bool stopping_ = true;
};
