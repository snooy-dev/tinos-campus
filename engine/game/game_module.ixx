export module GameModule;
import std;
import GameApi;

export class GameModule
{
public:
    GameModule() = default;
    ~GameModule();
    GameModule(const GameModule&) = delete;
    GameModule& operator=(const GameModule&) = delete;
    std::expected<void, std::string> Initialize(IEngineServices& services);
    GameFrame Update(double delta_seconds, float aspect_ratio);
    void Shutdown();
    void Destroy();
    void Unload();

private:
    void* library_ = nullptr;
    IGame* game_ = nullptr;
    DestroyGameFunction destroy_ = nullptr;
    bool started_ = false;
};
