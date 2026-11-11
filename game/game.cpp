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
    GameFrame Update(double delta_seconds) override
    {
        scene_.Update(delta_seconds);
        scene_.UpdateWorldTransforms();
        return { view_projection_, scene_.CollectRenderItems() };
    }
    void Shutdown() override { scene_.Clear(); }

private:
    Scene scene_;
    glm::mat4 view_projection_{ 1.0f };
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
