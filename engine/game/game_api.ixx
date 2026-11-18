export module GameApi;
import std;
export import MeshResource;

export inline constexpr unsigned kGameApiVersion = 6;

export struct GameFrame
{
    glm::mat4 view_projection{ 1.0f };
    std::vector<MeshDraw> meshes;
    // xyz is a perspective eye position (w=1) or a direction toward an orthographic camera (w=0).
    glm::vec4 camera{ 0.0f, 0.0f, 1.0f, 0.0f };
    glm::vec3 direction_to_light{ 0.4f, 1.0f, 0.6f };
    glm::mat4 light_view_projection{ 1.0f };
    bool shadows_enabled = false;
    glm::vec3 sky_ambient{ 0.16f };
    glm::vec3 ground_ambient{ 0.16f };
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
