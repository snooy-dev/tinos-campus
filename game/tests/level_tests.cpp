// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
// Build game.vcxproj with /p:LevelTests=true and run level_tests.exe beside content/.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
import std;
import Level;
import GameApi;

namespace
{
namespace fs = std::filesystem;

void Check(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("FAIL: {}", message);
        std::exit(1);
    }
}

template<class T>
T Require(std::expected<T, std::string> result)
{
    Check(result.has_value(), result ? "" : result.error());
    if constexpr (!std::is_void_v<T>)
        return std::move(*result);
}

class Services : public IEngineServices
{
public:
    MeshHandle LoadMeshAsync(fs::path path) override
    {
        ++requests;
        auto state = std::make_shared<MeshDetail::LoadState>();
        auto data = MeshLoading::Read(path).and_then(MeshLoading::Parse);
        if (data)
        {
            state->data = std::move(*data);
            state->status = MeshStatus::Ready;
        }
        else
        {
            state->error = data.error();
            state->status = MeshStatus::Failed;
        }
        return MeshHandle(state);
    }
    int requests = 0;
};

std::string Read(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    Check(stream.good(), "open test file for reading");
    return { std::istreambuf_iterator<char>(stream), {} };
}

void Write(const fs::path& path, std::string_view bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), bytes.size());
    stream.close();
    Check(stream.good(), "write test file");
}

std::string Change(std::string text, std::string_view from, std::string_view to)
{
    const auto position = text.find(from);
    Check(position != std::string::npos, from);
    return text.replace(position, from.size(), to);
}

GameObject* Find(Scene& scene, std::string_view id)
{
    for (auto* object : scene.GetLiveObjects())
        if (object->GetLevelId() == id)
            return object;
    return nullptr;
}
}

int main()
{
    const auto content = Require(Level::ExecutableContentRoot());
    const auto source = Read(content / L"start.json");
    const auto temp = fs::temp_directory_path() /
        std::format(L"tinos-level-{}-{}", GetCurrentProcessId(),
            std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code error;
    Check(fs::create_directory(temp, error), "create test directory");
    struct Cleanup
    {
        const fs::path& path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{ temp };
    const auto path = temp / L"level.json";
    Write(path, source);

    Level level(content);
    Services services;
    Require(level.Open(path, services));
    auto& scene = level.GetScene();
    auto* owner = dynamic_cast<Actor*>(Find(scene, "triangle-actor"));
    auto* parent = dynamic_cast<ActorComponent*>(Find(scene, "triangle-root"));
    auto* mesh = dynamic_cast<MeshComponent*>(Find(scene, "triangle"));
    auto* actor = dynamic_cast<CameraActor*>(Find(scene, "view"));
    auto* camera = dynamic_cast<CameraComponent*>(Find(scene, "view-camera"));
    Check(owner && parent && mesh && actor && camera, "restore registered types");
    Check(&mesh->GetActor() == owner && mesh->GetParent() == parent, "restore hierarchy");
    Check(camera == actor->FindPrimaryCamera() && camera == scene.FindActiveCamera(),
        "reuse the primary camera and restore the active reference");
    Check(scene.CollectRenderItems().size() == 1 && services.requests == 1, "load actual GLB");
    const auto pose_fields = Require(level.GetProperties("triangle-actor"));
    Check(std::ranges::find(pose_fields, "transform.rotation", &LevelProperty::name) != pose_fields.end(),
        "enumerate transform through metadata");
    const LevelProperty position[] = { { "transform.position", std::array{ 0.5f, 0.0f, 0.0f } } };
    Require(level.SetProperties("triangle-actor", position, services));
    Check(mesh->GetWorldMatrix()[3].x == 0.5f, "owner setter invalidates child matrix");

    const auto fields = Require(level.GetProperties("view-camera"));
    const auto near_field = std::ranges::find(fields, "near_plane", &LevelProperty::name);
    Check(near_field != fields.end() && std::get<float>(near_field->value) == 0.1f,
        "enumerate camera properties through metadata");
    const LevelProperty rejected[] = { { "near_plane", 200.0f },
        { "transform.position", std::array{ 1.0f, 0.0f, 0.0f } } };
    Check(!level.SetProperties("view-camera", rejected, services) && camera->GetNearPlane() == 0.1f &&
        camera->GetLocalTransform().position.x == 0.0f && camera->GetWorldMatrix()[3].x == 0.0f,
        "camera rejected edit preserves the original value");
    const LevelProperty tiny_fov[] = { { "fov_y_radians", 1e-40f } };
    Check(!level.SetProperties("view-camera", tiny_fov, services) && camera->GetFieldOfView() > 1.0f,
        "reject perspective overflow without changing the camera");
    const LevelProperty tiny_ortho[] = {
        { "mode", "orthographic" }, { "vertical_size", 1e-40f } };
    Check(!level.SetProperties("view-camera", tiny_ortho, services) &&
        camera->GetProjectionMode() == ProjectionMode::Perspective,
        "reject orthographic overflow without changing projection mode");
    const LevelProperty changes[] = { { "mode", "orthographic" }, { "vertical_size", 3.0f },
        { "near_plane", 0.5f } };
    Require(level.SetProperties("view-camera", changes, services));
    const LevelProperty color[] = { { "color", std::array{ 1.0f, 0.0f, 0.0f } } };
    Require(level.SetProperties("triangle", color, services));
    Check(services.requests == 1 && mesh->GetColor().r == 1.0f,
        "color edit preserves the loaded mesh");
    Require(level.Place({ .id = "placed", .type = "camera_actor" }, services));
    Check(!level.Place({ .id = "\xff", .type = "actor" }, services), "reject invalid UTF-8 placement ID");
    Require(level.Place({ .id = "deleted", .type = "actor_component", .owner = "placed" }, services));
    Require(level.Remove("deleted"));
    Check(!level.Remove("placed/primary_camera"), "reject direct primary camera deletion");
    Check(scene.SpawnActor<Actor>() != nullptr, "create transient runtime actor");
    Require(level.Save(path));
    const auto saved = Read(path);
    Level roundtrip(content);
    Require(roundtrip.Open(path, services));
    auto& restored = roundtrip.GetScene();
    Check(restored.GetLiveObjects().size() == 7 && !Find(restored, "deleted") &&
        Find(restored, "placed/primary_camera"), "save only persistent live objects");
    Check(restored.GetActiveCamera().GetProjectionMode() == ProjectionMode::Orthographic &&
        restored.GetActiveCamera().GetNearPlane() == 0.5f &&
        restored.GetActiveCamera().GetOrthographicSize() == 3.0f, "camera round trip");
    Check(restored.CollectRenderItems()[0].world[3].x == 0.5f &&
        restored.CollectRenderItems()[0].color.r == 1.0f, "mesh and transform round trip");
    Require(level.Save(path));
    Check(Read(path) == saved, "deterministic output");
    const LevelProperty edited[] = { { "near_plane", 2.0f } };
    Require(roundtrip.SetProperties("view-camera", edited, services));
    Require(roundtrip.Save(path));
    Check(!level.Save(path), "detect a save from another level instance");
    Write(path, saved);

    const auto bad = temp / L"bad.json";
    const std::pair<std::string_view, std::string_view> invalid[] = {
        { "\"schema_version\": 1", "\"schema_version\": 2" },
        { "\"schema_version\": 1,", "\"schema_version\": 1, \"unexpected\": 0," },
        { "\"schema_version\": 1,", "\"schema_version\": 1, \"\\u0073chema_version\": 1," },
        { "\"active_camera\": \"view-camera\",", "" },
        { "\"active_camera\": \"view-camera\"", "\"active_camera\": \"triangle\"" },
        { "\"id\": \"triangle\"", "\"id\": \"view\"" },
        { "\"id\": \"triangle\"", "\"id\": \"\\uD800\"" },
        { "\"id\": \"triangle\"", "\"id\": \"\xff\"" },
        { "\"owner\": \"triangle-actor\"", "\"owner\": \"view\"" },
        { "\"parent\": \"triangle-root\"", "\"parent\": \"triangle\"" },
        { "\"slot\": \"primary_camera\"", "\"slot\": null" },
        { "\"slot\": null,", "" },
        { "\"near_plane\": 0.1", "\"near_plane\": 1e99" },
        { "\"near_plane\": 0.1", "\"near_plane\": NaN" },
        { "\"near_plane\": 0.1", "\"near_plane\": -1" },
        { "\"fov_y_radians\": 1.0471976", "\"fov_y_radians\": 1e-40" },
        { "\"asset\": \"삼각형.glb\"", "\"asset\": \"../삼각형.glb\"" },
        { "\"asset\": \"삼각형.glb\"", "\"asset\": \"missing.glb\"" },
    };
    for (auto [from, to] : invalid)
    {
        Write(bad, Change(source, from, to));
        const auto opened = level.Open(bad, services);
        Check(!opened, to);
        Check(&level.GetScene() == &scene, "failed open preserves scene");
    }
    for (const auto array : { "[1,2]", "[1,2,3,4]" })
    {
        const auto start = source.find('[', source.find("\"position\""));
        const auto stop = source.find(']', start);
        auto edited = source;
        edited.replace(start, stop - start + 1, array);
        Write(bad, edited);
        Check(!level.Open(bad, services), "reject wrong array lengths");
    }
    Write(bad, source + " garbage");
    Check(!level.Open(bad, services), "reject trailing data");
    Write(bad, std::string(65, '[') + std::string(65, ']'));
    Check(!level.Open(bad, services), "reject deep JSON");
    fs::resize_file(bad, 64 * 1024 * 1024 + 1, error);
    Check(!error && !level.Open(bad, services), "reject oversized level");
    const auto invalid_mesh = temp / L"broken.glb";
    Write(invalid_mesh, "invalid mesh data");
    Write(bad, Change(source, "삼각형.glb", "broken.glb"));
    Level bad_asset(temp);
    auto* previous = &bad_asset.GetScene();
    Check(!bad_asset.Open(bad, services) && &bad_asset.GetScene() == previous,
        "mesh parse failure preserves scene");

    const auto held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(held != INVALID_HANDLE_VALUE, "lock saved file against replacement");
    Check(!level.Save(path) && Read(path) == saved, "replacement failure preserves original");
    CloseHandle(held);
    Write(path, source);
    const auto conflict = level.Save(path);
    Check(!conflict && conflict.error().contains("conflict") && Read(path) == source,
        "detect a modified original without overwriting it");
    Check(&level.GetScene() == &scene, "save failure preserves scene");
    const auto alternate_case = temp / L"LEVEL.JSON";
    if (fs::equivalent(path, alternate_case, error))
    {
        Check(fs::remove(path, error) && !error, "remove original before alternate-case save");
        const auto deleted = level.Save(alternate_case);
        Check(!deleted && deleted.error().contains("removed") && !fs::exists(alternate_case),
            "alternate-case save detects deleted original");
    }

    auto duplicated = source;
    const auto begin = source.find("\n    {");
    const auto end = source.find("\n    },", begin) + 6;
    auto duplicate = Change(source.substr(begin, end - begin), "\"id\": \"triangle\"", "\"id\": \"aaa\"");
    duplicate = Change(std::move(duplicate), "0.2", "0.9");
    duplicated.insert(end + 1, duplicate + ",");
    Write(bad, duplicated);
    Level shared_mesh(content);
    const int before = services.requests;
    Require(shared_mesh.Open(bad, services));
    const auto draws = shared_mesh.GetScene().CollectRenderItems();
    Check(draws.size() == 2 && services.requests == before + 1 && draws[0].data == draws[1].data,
        "duplicate asset paths share one load and CPU geometry");
    Require(shared_mesh.Save(bad));
    Require(shared_mesh.Open(bad, services));
    Check(shared_mesh.GetScene().CollectRenderItems()[0].color == draws[0].color,
        "save and reopen preserve equal-depth mesh draw order");

    const auto empty_path = temp / L"empty.json";
    Level empty(content);
    Check(empty.GetScene().GetActiveCamera().GetLevelId().empty(), "runtime camera has no level ID");
    Require(empty.Save(empty_path));
    Require(empty.Open(empty_path, services));
    Check(empty.GetScene().GetLiveObjects().empty() && !empty.GetScene().FindActiveCamera(),
        "serialize an implicit camera as a null reference");
    std::println("Level regression tests passed.");
}
