import std;
import GameApi;
import GameFramework;

namespace
{
class Game final : public IGame
{
public:
    std::expected<void, const char*> Initialize(IEngineServices&) override
    {
        // Game-specific actors and assets will be created here.
        return {};
    }
    GameFrame Update(double delta_seconds, float aspect_ratio) override
    {
        scene_.Update(delta_seconds);
        scene_.UpdateWorldTransforms();
        return { scene_.GetActiveCamera().GetViewProjectionMatrix(aspect_ratio),
            scene_.CollectRenderItems() };
    }
    void Shutdown() override { scene_.Clear(); }

private:
    Scene scene_;
};
}

extern "C" IGame* __cdecl CreateGame(unsigned api_version) noexcept
{
    if (api_version != kGameApiVersion)
        return nullptr;
    return new (std::nothrow) Game;
}

extern "C" void __cdecl DestroyGame(IGame* game) noexcept
{
    delete game;
}
