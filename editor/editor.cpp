// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

import std;
import <GLFW/glfw3.h>;
import Application;
import Renderer;
import JobSystem;
import Level;
import GameApi;
import CampusGameRuntime;
import Debug;

namespace
{
namespace fs = std::filesystem;

class MeshServices final : public IEngineServices
{
public:
    explicit MeshServices(Renderer& renderer) : renderer_(renderer) {}
    MeshHandle LoadMeshAsync(fs::path path) override
    {
        return renderer_.LoadMeshAsync(std::move(path));
    }
private:
    Renderer& renderer_;
};

const GameObject* Parent(GameObject& object)
{
    auto* component = dynamic_cast<ActorComponent*>(&object);
    if (!component)
        return nullptr;
    if (component->GetParent())
        return component->GetParent();
    return &component->GetActor();
}

using Children = std::unordered_map<const GameObject*, std::vector<GameObject*>>;

void Hierarchy(const Children& children, const GameObject* parent, std::string& selected)
{
    const auto found = children.find(parent);
    if (found == children.end())
        return;
    for (auto* object : found->second)
    {
        const auto& id = object->GetLevelId();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!children.contains(object))
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (selected == id)
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx(object, flags, "%s", id.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            selected = id;
        if (open)
        {
            Hierarchy(children, object, selected);
            ImGui::TreePop();
        }
    }
}

bool Property(LevelProperty& field)
{
    const auto& name = field.name;
    if (auto* vec = std::get_if<std::array<float, 3>>(&field.value))
    {
        if (name == "color")
            return ImGui::ColorEdit3("Color", vec->data());
        return ImGui::DragFloat3(name == "transform.position" ? "Position" : "Scale",
            vec->data(), 0.02f);
    }
    if (auto* rotation = std::get_if<std::array<float, 4>>(&field.value))
    {
        const auto& r = *rotation;
        auto angles = glm::degrees(glm::eulerAngles(glm::quat(r[0], r[1], r[2], r[3])));
        if (!ImGui::DragFloat3("Rotation (deg)", &angles.x, 0.5f))
            return false;
        const glm::quat q(glm::radians(angles));
        *rotation = { q.w, q.x, q.y, q.z };
        return true;
    }
    if (auto* value = std::get_if<float>(&field.value))
        return ImGui::DragFloat(name.c_str(), value, 0.01f);
    auto& value = std::get<std::string>(field.value);
    if (name == "mode")
    {
        int index = value == "perspective" ? 0 : 1;
        if (!ImGui::Combo("Projection", &index, "Perspective\0Orthographic\0"))
            return false;
        value = index == 0 ? "perspective" : "orthographic";
        return true;
    }
    std::vector<char> buffer(std::max<std::size_t>(32768, value.size() + 1));
    std::ranges::copy(value, buffer.begin());
    if (!ImGui::InputText("Asset", buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        return false;
    value = buffer.data();
    return true;
}

struct Document
{
    fs::path root;
    Level level{ root };
    IEngineServices& services;
    fs::path file;
    std::string selection, error;
    bool dirty = false;
    Scene camera_scene;

    bool Open(const fs::path& path)
    {
        const auto result = level.Open(path, services);
        if (!result)
        {
            error = result.error();
            return false;
        }
        file = path;
        dirty = false;
        error.clear();
        selection.clear();
        auto& scene = level.GetScene();
        scene.UpdateWorldTransforms();
        Transform pose;
        if (const auto* camera = scene.FindActiveCamera())
        {
            const auto world = glm::inverse(camera->GetViewMatrix());
            pose.position = glm::vec3(world[3]);
            pose.rotation = glm::quat_cast(world);
        }
        else
            pose.position.z = 3.0f;
        camera_scene.GetDefaultCameraActor().SetLocalTransform(pose);
        return true;
    }

    bool Save()
    {
        if (const auto result = level.Save(file); !result)
        {
            error = result.error();
            return false;
        }
        dirty = false;
        error.clear();
        return true;
    }

    bool MayLeave()
    {
        if (!dirty)
            return true;
        const int answer = MessageBoxW(nullptr, L"Save changes to the current scene?",
            L"Tino's Campus Editor", MB_YESNOCANCEL | MB_ICONQUESTION);
        return answer == IDNO || (answer == IDYES && Save());
    }

    void ChooseFile()
    {
        std::array<wchar_t, 32768> path{};
        OPENFILENAMEW dialog{ sizeof(dialog) };
        dialog.lpstrFilter = L"Scene files (*.json)\0*.json\0";
        dialog.lpstrInitialDir = root.c_str();
        dialog.lpstrFile = path.data();
        dialog.nMaxFile = static_cast<DWORD>(path.size());
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog))
            return;
        std::error_code ec;
        const auto relative = fs::relative(path.data(), root, ec);
        if (ec || relative.empty() || *relative.begin() == L".." || relative.is_absolute())
        {
            error = "Choose a scene inside the project content folder.";
            return;
        }
        if (MayLeave())
            Open(path.data());
    }

    void Inspector()
    {
        if (selection.empty())
        {
            ImGui::TextDisabled("Select an object in the hierarchy.");
            return;
        }
        ImGui::TextUnformatted(selection.c_str());
        const auto fields = level.GetProperties(selection);
        if (!fields)
            return;
        ImGui::PushID(selection.c_str());
        bool show_transform = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
        std::optional<bool> show_properties;
        for (auto field : *fields)
        {
            if (field.name.starts_with("transform."))
            {
                if (!show_transform)
                    continue;
            }
            else
            {
                if (!show_properties)
                    show_properties = ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen);
                if (!*show_properties)
                    continue;
            }
            if (!Property(field))
                continue;
            const auto result = level.SetProperties(selection, std::span(&field, 1), services);
            if (result)
            {
                dirty = true;
                error.clear();
            }
            else
                error = field.name + ": " + result.error();
        }
        ImGui::PopID();
    }
};

void MoveCamera(CameraActor& camera, const Input& input, float dt)
{
    auto pose = camera.GetLocalTransform();
    pose.rotation = glm::normalize(
        glm::angleAxis(float(-input.orbit_x) * 0.003f, glm::vec3(0, 1, 0)) *
        pose.rotation * glm::angleAxis(float(-input.orbit_y) * 0.003f, glm::vec3(1, 0, 0)));
    glm::vec3 move = pose.rotation * glm::vec3(float(input.move_right), 0, float(-input.move_forward));
    move.y += float(ImGui::IsKeyDown(ImGuiKey_E)) - float(ImGui::IsKeyDown(ImGuiKey_Q));
    if (glm::dot(move, move) > 0)
    {
        const float speed = ImGui::GetIO().KeyShift ? 12.0f : 4.0f;
        pose.position += glm::normalize(move) * (speed * std::min(dt, 0.1f));
    }
    camera.SetLocalTransform(pose);
}

std::expected<void, std::string> Run()
{
    Application app;
    if (const auto result = app.Initialize(true); !result)
        return std::unexpected(result.error());
    glfwSetWindowTitle(app.Window(), "Tino's Campus Editor");
    JobSystem jobs;
    jobs.Initialize();
    Renderer renderer;
    if (const auto result = renderer.Initialize(app, jobs); !result)
        return std::unexpected(result.error());
    MeshServices services(renderer);
    const auto output = Level::ExecutableContentRoot();
    if (!output)
        return std::unexpected(output.error());
    const auto root = (output->parent_path() / "../../../game/content").lexically_normal();
    Document doc{ root, Level(root), services };
    doc.Open(root / "start.json");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    std::array<wchar_t, MAX_PATH> windows{};
    if (GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size())))
    {
        const auto font = fs::path(windows.data()) / L"Fonts/malgun.ttf";
        std::error_code ec;
        if (fs::is_regular_file(font, ec))
        {
            const auto name = font.u8string();
            io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(name.c_str()), 16.0f);
        }
    }
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.FrameRounding = 3;
    style.WindowPadding = { 10, 9 };
    style.Colors[ImGuiCol_Header] = { 0.16f, 0.30f, 0.46f, 1 };
    style.Colors[ImGuiCol_WindowBg] = { 0.115f, 0.12f, 0.135f, 1 };
    style.FontSizeBase = 20.0f;
    const auto base_style = style;
    float applied_scale = 0.0f;
    bool platform = ImGui_ImplGlfw_InitForOpenGL(app.Window(), true);
    bool graphics = false;
    if (platform)
        jobs.SubmitRender([&] { graphics = ImGui_ImplOpenGL3_Init("#version 330"); }).Wait();
    UiAssets ui_assets;
    std::optional<CampusGame> preview;
    bool gameplay_paused = true;
    bool reset_preview_time = true;
    bool navigating = false;
    while (graphics)
    {
        const auto state = app.ProcessEvents();
        if (state.close_requested)
        {
            if (doc.MayLeave())
                break;
            glfwSetWindowShouldClose(app.Window(), GLFW_FALSE);
        }

        if (state.minimized || !state.framebuffer_width || !state.framebuffer_height)
        {
            navigating = false;
            reset_preview_time = true;
            (void)app.SampleInput(true);
            (void)app.SampleUiInput();
            app.WaitEvents();
            continue;
        }

        jobs.SubmitRender([] { ImGui_ImplOpenGL3_NewFrame(); }).Wait();
        ImGui_ImplGlfw_NewFrame();
        const float width = io.DisplaySize.x, height = io.DisplaySize.y;
        if (width <= 0 || height <= 0)
            continue;
        const float ui_scale = std::min(width / 1280.0f, height / 720.0f);
        if (ui_scale != applied_scale)
        {
            style = base_style;
            style.ScaleAllSizes(ui_scale);
            style.FontScaleMain = ui_scale;
            applied_scale = ui_scale;
        }
        ImGui::NewFrame();
        const bool toggle_preview = ImGui::IsKeyPressed(ImGuiKey_F5, false);
        bool start_preview = toggle_preview && !preview && !doc.file.empty();
        if (preview)
        {
            if (toggle_preview)
            {
                preview.reset();
                (void)app.SampleInput(true);
                glfwSetWindowTitle(app.Window(), "Tino's Campus Editor");
            }
            else
            {
                ImGui::EndFrame();
                const auto input = app.SampleInput(gameplay_paused);
                const auto elapsed = std::exchange(reset_preview_time, false) ? 0.0f : io.DeltaTime;
                auto frame = preview->Update(elapsed,
                    float(state.framebuffer_width) / state.framebuffer_height,
                    input, app.SampleUiInput());
                gameplay_paused = frame.gameplay_paused;
                renderer.SubmitFrame(MakeRenderFrame(state.framebuffer_width,
                    state.framebuffer_height, std::move(frame))).Wait();
                continue;
            }
        }
        (void)app.SampleUiInput();
        const float header = ImGui::GetFrameHeight();
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open scene..."))
                    doc.ChooseFile();
                if (ImGui::MenuItem("Save", "Ctrl+S", false, doc.dirty))
                    doc.Save();
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Game Preview (F5)", nullptr, false, !doc.file.empty()))
                start_preview = true;
            const auto title = doc.file.filename().u8string();
            ImGui::TextDisabled("%s%s", reinterpret_cast<const char*>(title.c_str()), doc.dirty ? " *" : "");
            ImGui::EndMainMenuBar();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && doc.dirty)
            doc.Save();

        if (start_preview)
        {
            auto snapshot = doc.level.Clone(services);
            if (!snapshot)
                doc.error = snapshot.error();
            else
            {
                const auto started = preview.emplace().StartPreview(std::move(*snapshot),
                    doc.root, services, ui_assets);
                if (started)
                {
                    navigating = false;
                    gameplay_paused = true;
                    reset_preview_time = true;
                    (void)app.SampleInput(true);
                    glfwSetWindowTitle(app.Window(), "Tino's Campus - Game Preview (F5 to return)");
                    ImGui::EndFrame();
                    continue;
                }
                doc.error = started.error();
                preview.reset();
            }
        }

        const float left = width * 0.22f;
        const float right = width * 0.32f;
        const float center = width - left - right;
        auto panel = [&](const char* name, float x, float w, ImGuiWindowFlags extra = 0)
        {
            ImGui::SetNextWindowPos({ x, header });
            ImGui::SetNextWindowSize({ w, std::max(1.0f, height - header) });
            ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | extra);
        };
        panel("Hierarchy", 0, left);
        Children children;
        for (auto* object : doc.level.GetScene().GetLiveObjects())
            children[Parent(*object)].push_back(object);
        Hierarchy(children, nullptr, doc.selection);
        ImGui::End();

        panel("Inspector", width - right, right);
        doc.Inspector();
        if (!doc.error.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", doc.error.c_str());
        }
        ImGui::End();

        panel("Scene", left, center, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        ImGui::TextDisabled("RMB + WASD to fly, Q/E down/up, Shift to speed up");
        const auto pos = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##viewport", { std::max(1.f, available.x), std::max(1.f, available.y) },
            ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            navigating = true;
        navigating = navigating && state.focused &&
            glfwGetMouseButton(app.Window(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        auto& camera = doc.camera_scene.GetDefaultCameraActor();
        const auto flight = app.SampleInput(!navigating);
        if (navigating)
            MoveCamera(camera, flight, io.DeltaTime);
        ImGui::End();
        ImGui::Render();
        const auto scale = io.DisplayFramebufferScale;
        const RenderViewport viewport{ int(pos.x * scale.x),
            int((height - pos.y - available.y) * scale.y),
            std::max(1, int(available.x * scale.x)), std::max(1, int(available.y * scale.y)) };
        auto& scene = doc.level.GetScene();
        scene.UpdateWorldTransforms();
        const auto& view_camera = camera.GetCameraComponent();
        const auto view = view_camera.GetViewMatrix();
        RenderFrame frame{ state.framebuffer_width, state.framebuffer_height,
            view_camera.GetProjectionMatrix(float(viewport.width) / viewport.height) * view,
            scene.CollectRenderItems() };
        frame.camera = view_camera.GetProjectionMode() == ProjectionMode::Perspective
            ? glm::vec4(glm::vec3(view_camera.GetWorldMatrix()[3]), 1.0f)
            : glm::vec4(view[0][2], view[1][2], view[2][2], 0.0f);
        frame.viewport = viewport;
        frame.draw_overlay = [] { ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); };
        // The next UI frame may reuse every draw list once this submission completes.
        renderer.SubmitFrame(std::move(frame)).Wait();
    }
    if (graphics)
        jobs.SubmitRender([] { ImGui_ImplOpenGL3_Shutdown(); }).Wait();
    if (platform)
        ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (!graphics)
        return std::unexpected("Could not initialize the editor UI.");
    return {};
}
}

int main()
{
    if (const auto result = Run(); !result)
        Debug::ReportCrash(result.error().c_str());
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    return main();
}
