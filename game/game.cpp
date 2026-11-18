import std;
import GameApi;
import GameFramework;
import <glm/gtc/matrix_transform.hpp>;

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
        const auto& camera = scene_.GetActiveCamera();
        const auto view = camera.GetViewMatrix();
        const auto eye = camera.GetProjectionMode() == ProjectionMode::Perspective
            ? glm::vec4(glm::vec3(camera.GetWorldMatrix()[3]), 1.0f)
            : glm::vec4(view[0][2], view[1][2], view[2][2], 0.0f);
        GameFrame frame{ camera.GetProjectionMatrix(aspect_ratio) * view, scene_.CollectRenderItems(), eye };
        frame.direction_to_light = glm::normalize(glm::vec3(0.4f, 1.0f, 0.6f));
        const glm::vec3 shadow_center(0.0f);
        const auto up = std::abs(frame.direction_to_light.y) > 0.99f
            ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const auto light_view = glm::lookAt(shadow_center + frame.direction_to_light * 40.0f,
            shadow_center, up);
        frame.light_view_projection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 80.0f)
            * light_view;
        frame.shadows_enabled = true;
        frame.sky_ambient = { 0.16f, 0.23f, 0.32f };
        frame.ground_ambient = { 0.12f, 0.17f, 0.085f };
        return frame;
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
