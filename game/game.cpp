import std;
import GameApi;
import CampusGameRuntime;

extern "C" IGame* __cdecl CreateGame(unsigned api_version) noexcept
{
    if (api_version != kGameApiVersion)
        return nullptr;
    return new (std::nothrow) CampusGame;
}

extern "C" void __cdecl DestroyGame(IGame* game) noexcept
{
    delete game;
}
