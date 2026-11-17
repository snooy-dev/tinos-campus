import std;
import GameApi;
import Level;

namespace
{
class Game final : public IGame
{
public:
    std::expected<void, const char*> Initialize(IEngineServices& services) override
    {
        const auto content = Level::ExecutableContentRoot();
        if (!content)
        {
            error_ = content.error();
            return std::unexpected(error_.c_str());
        }
        level_.emplace(*content);
        const auto opened = level_->Open(*content / L"start.json", services);
        if (!opened)
        {
            error_ = "content/start.json: " + opened.error();
            return std::unexpected(error_.c_str());
        }
        return {};
    }
    GameFrame Update(double delta_seconds, float aspect_ratio) override
    {
        auto& scene = level_->GetScene();
        scene.Update(delta_seconds);
        scene.UpdateWorldTransforms();
        return { scene.GetActiveCamera().GetViewProjectionMatrix(aspect_ratio),
            scene.CollectRenderItems() };
    }
    void Shutdown() override { level_.reset(); }

private:
    std::optional<Level> level_;
    std::string error_;
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
