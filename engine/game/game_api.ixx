export module GameApi;
import std;
export import MeshResource;

export inline constexpr unsigned kGameApiVersion = 3;

export struct GameFrame
{
    glm::mat4 view_projection{ 1.0f };
    std::vector<MeshDraw> meshes;
};

// Borrowed on the main thread until Shutdown returns; no work may be submitted during shutdown.
export class IEngineServices
{
public:
    virtual ~IEngineServices() = default;
    [[nodiscard]] virtual MeshHandle LoadMeshAsync(std::filesystem::path path) = 0;
};

// Both binaries must use the same compiler, architecture and runtime configuration.
export class IGame
{
public:
    virtual ~IGame() = default;
    virtual std::expected<void, const char*> Initialize(IEngineServices& services) = 0;
    // aspect_ratio must be finite and positive.
    // Frames contain engine-owned mesh data, never game objects or callbacks.
    virtual GameFrame Update(double delta_seconds, float aspect_ratio) = 0;
    // Also called after failed initialization; stop and join all game-owned work.
    virtual void Shutdown() = 0;
};

export using CreateGameFunction = IGame* (__cdecl*)(unsigned) noexcept;
export using DestroyGameFunction = void (__cdecl*)(IGame*) noexcept;
