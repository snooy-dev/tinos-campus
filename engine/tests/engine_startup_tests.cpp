// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
// Run through run-engine-startup-tests.ps1, which stages isolated content.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

import std;
import Engine;

namespace
{
namespace fs = std::filesystem;

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("FAIL: {}", message);
        std::exit(1);
    }
}

void Write(const fs::path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), bytes.size());
    file.close();
    Require(file.good(), "write isolated mesh fixture");
}
}

int wmain(int argc, wchar_t** argv)
{
    Require(argc == 2 && std::wstring_view(argv[1]) == L"--isolated-fixture",
        "run through run-engine-startup-tests.ps1");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const auto mesh = fs::path(argv[0]).parent_path() / L"content" / L"삼각형.glb";
    std::ifstream file(mesh, std::ios::binary);
    const std::string original(std::istreambuf_iterator<char>(file), {});
    Require(!original.empty(), "read isolated mesh fixture");
    file.close();

    Engine engine;
    Write(mesh, "not a GLB");
    const auto failed = engine.Initialize();
    Require(!failed && failed.error().contains("Invalid glTF file"), "reject corrupt GLB after mesh job");
    Require(!GetModuleHandleW(L"game.dll"), "failed initialization unloads game.dll");
    Write(mesh, original);

    for (int run = 0; run < 2; ++run)
    {
        const auto started = engine.Initialize();
        Require(started.has_value(), started ? "" : started.error());
        Require(GetModuleHandleW(L"game.dll") != nullptr, "load game.dll beside executable");
        const auto owner = GetCurrentThreadId();
        std::jthread close([owner]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            EnumThreadWindows(owner, [](HWND window, LPARAM) -> BOOL
            {
                wchar_t title[64]{};
                GetWindowTextW(window, title, 64);
                if (std::wstring_view(title) != L"Tino's Campus")
                    return TRUE;
                PostMessageW(window, WM_CLOSE, 0, 0);
                return FALSE;
            }, 0);
        });
        engine.Run();
        Require(!GetModuleHandleW(L"game.dll"), "normal shutdown unloads game.dll");
    }
    std::println("Engine startup, mesh jobs and DLL reload passed.");
}
