export module CampusGameRuntime;

import std;
import GameApi;
import CampusMenu;
import Level;
import TinoCharacter;
import <glm/gtc/matrix_transform.hpp>;

export class CampusGame final : public IGame
{
public:
    std::expected<void, const char*> Initialize(IEngineServices& services, IUiAssets& ui_assets) override
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
        return Start(services, ui_assets, *content);
    }

    std::expected<void, const char*> StartPreview(Level level, const std::filesystem::path& content,
        IEngineServices& services, IUiAssets& ui_assets)
    {
        level_.emplace(std::move(level));
        return Start(services, ui_assets, content);
    }

    GameFrame Update(double delta_seconds, float aspect_ratio, const Input& input,
        const UiInput& ui_input) override
    {
        auto& scene = level_->GetScene();
        const bool was_paused = menu_.Paused();
        menu_.Update(ui_input);
        if (!was_paused && !menu_.Paused())
        {
            tino_->SetInput(input);
            scene.Update(delta_seconds);
            scene.UpdateWorldTransforms();
        }
        const auto& camera = scene.GetActiveCamera();
        const auto view = camera.GetViewMatrix();
        const auto eye = camera.GetProjectionMode() == ProjectionMode::Perspective
            ? glm::vec4(glm::vec3(camera.GetWorldMatrix()[3]), 1.0f)
            : glm::vec4(view[0][2], view[1][2], view[2][2], 0.0f);
        GameFrame frame{ camera.GetProjectionMatrix(aspect_ratio) * view, scene.CollectRenderItems(), eye };
        frame.direction_to_light = glm::normalize(glm::vec3(0.4f, 1.0f, 0.6f));
        const auto shadow_center = tino_->GetLocalTransform().position;
        const auto up = std::abs(frame.direction_to_light.y) > 0.99f
            ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const auto light_view = glm::lookAt(shadow_center + frame.direction_to_light * 40.0f,
            shadow_center, up);
        frame.light_view_projection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 80.0f)
            * light_view;
        frame.shadows_enabled = true;
        frame.sky_ambient = { 0.16f, 0.23f, 0.32f };
        frame.ground_ambient = { 0.12f, 0.17f, 0.085f };
        const auto sky_projection = camera.GetProjectionMode() == ProjectionMode::Perspective
            ? camera.GetProjectionMatrix(aspect_ratio)
            : glm::perspective(glm::radians(60.0f), aspect_ratio, 0.1f, 10.0f);
        frame.sky_view_projection = sky_projection * glm::mat4(glm::mat3(view));
        frame.sky_enabled = true;
        frame.ui = menu_.Draw(ui_input.window);
        frame.gameplay_paused = menu_.Paused();
        return frame;
    }
    void Shutdown() override
    {
        tino_ = nullptr;
        level_.reset();
    }

private:
    std::expected<void, const char*> Start(IEngineServices& services, IUiAssets& ui_assets,
        const std::filesystem::path& content)
    {
        auto mesh = services.LoadMeshAsync(content / L"tino.glb");
        if (const auto ready = mesh.Wait(); !ready)
        {
            error_ = "content/tino.glb: " + ready.error();
            return std::unexpected(error_.c_str());
        }

        auto& scene = level_->GetScene();
        tino_ = scene.SpawnActor<Tino>(std::move(mesh));
        Transform spawn;
        spawn.position.x = 2.0f;
        tino_->SetLocalTransform(spawn);
        const auto camera = scene.SetActiveCamera(tino_->GetCamera());
        if (!camera) return camera;
        scene.UpdateWorldTransforms();
        return menu_.Initialize(ui_assets, content);
    }

    std::optional<Level> level_;
    CampusMenu menu_;
    Tino* tino_ = nullptr;
    std::string error_;
};
