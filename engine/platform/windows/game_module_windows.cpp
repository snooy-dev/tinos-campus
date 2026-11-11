module;
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

module GameModule;
import <cassert>;

GameModule::~GameModule()
{
    Shutdown();
    Unload();
}

std::expected<void, std::string> GameModule::Initialize(IEngineServices& services)
{
    assert(!library_);
    std::wstring path(256, L'\0');
    for (;;)
    {
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length)
            return std::unexpected("Cannot locate the engine executable.");
        if (length < path.size())
        {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"game.dll";
    library_ = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library_)
        return std::unexpected("Cannot load game.dll beside the engine executable.");

    const auto create = reinterpret_cast<CreateGameFunction>(
        GetProcAddress(static_cast<HMODULE>(library_), "CreateGame"));
    destroy_ = reinterpret_cast<DestroyGameFunction>(
        GetProcAddress(static_cast<HMODULE>(library_), "DestroyGame"));
    if (!create || !destroy_)
        return std::unexpected("game.dll is missing its required entry points.");
    game_ = create(kGameApiVersion);
    if (!game_)
        return std::unexpected("game.dll rejected the API version or could not create the game.");
    started_ = true;
    const auto result = game_->Initialize(services);
    if (!result)
        return std::unexpected(std::string(result.error() ? result.error() : "Game initialization failed."));
    return {};
}

GameFrame GameModule::Update(double delta_seconds, float aspect_ratio)
{
    assert(started_ && game_);
    return game_->Update(delta_seconds, aspect_ratio);
}

void GameModule::Shutdown()
{
    if (!started_)
        return;
    game_->Shutdown();
    started_ = false;
}

void GameModule::Destroy()
{
    assert(!started_);
    if (game_)
        destroy_(game_);
    game_ = nullptr;
}

void GameModule::Unload()
{
    Destroy();
    destroy_ = nullptr;
    if (library_)
        FreeLibrary(static_cast<HMODULE>(library_));
    library_ = nullptr;
}
