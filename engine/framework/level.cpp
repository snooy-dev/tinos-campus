// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "framework/level_json.hpp"

module Level;
import std;
import GameApi;

namespace
{
namespace fs = std::filesystem;
using namespace level_json;
template<class T> using Result = std::expected<T, std::string>;
using Properties = std::variant<Empty, Perspective, Orthographic, Mesh>;

std::string Describe(const Error& error)
{
    return std::format("{} (byte {})", error.message, error.offset);
}

std::unexpected<std::string> Field(const Record& record, std::string_view field,
    std::string_view message)
{
    return std::unexpected(std::format("object '{}', {}: {}", record.id, field, message));
}

struct FileHandle
{
    HANDLE value;
    ~FileHandle() { Close(); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    explicit FileHandle(HANDLE handle) : value(handle) {}
    bool Close()
    {
        const auto handle = std::exchange(value, INVALID_HANDLE_VALUE);
        return handle == INVALID_HANDLE_VALUE || CloseHandle(handle);
    }
};

Result<std::string> ReadFileBytes(HANDLE file)
{
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > MaxBytes)
        return std::unexpected("Cannot read level size or level exceeds 64 MiB.");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD count = 0;
    if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
        count != bytes.size())
        return std::unexpected("Cannot read the complete level.");
    return bytes;
}

std::string WindowsError(std::string_view operation)
{
    return std::format("{} (Windows error {})", operation, GetLastError());
}

Result<fs::path> ResolveAsset(const fs::path& root, std::string_view asset)
{
    if (asset.empty() || asset.size() > 32767 ||
        asset.find_first_of("\\:*?\"<>|") != std::string_view::npos ||
        std::ranges::any_of(asset, [](unsigned char ch) { return ch < 32; }))
        return std::unexpected("Expected a content-relative UTF-8 path with '/' separators.");
    const int length = static_cast<int>(asset.size());
    const auto wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        asset.data(), length, nullptr, 0);
    if (!wide_size)
        return std::unexpected("Invalid UTF-8 asset path.");
    std::wstring wide(wide_size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        asset.data(), length, wide.data(), wide_size))
        return std::unexpected("Invalid UTF-8 asset path.");
    const fs::path relative(wide);
    if (!relative.is_relative() || relative.has_root_path())
        return std::unexpected("Asset paths must be relative.");
    for (const auto& part : relative)
    {
        const auto& name = part.native();
        if (name.empty() || name.back() == L'.' || name.back() == L' ')
            return std::unexpected("Invalid asset path component.");
    }
    std::error_code error;
    const auto base = fs::canonical(root, error);
    if (error)
        return std::unexpected("Cannot resolve the content root.");
    const auto path = fs::canonical(base / relative, error);
    if (error)
        return std::unexpected("Asset file does not exist.");
    const auto [end, remainder] = std::ranges::mismatch(base, path);
    if (end != base.end() || remainder == path.end())
        return std::unexpected("Asset resolves outside the content root.");
    if (!fs::is_regular_file(path, error))
        return std::unexpected("Asset must be a regular file.");
    return path;
}

bool MatchesMissingSource(const fs::path& path, const fs::path& source)
{
    std::error_code error;
    if (path.parent_path() != source.parent_path() || fs::exists(path, error) || error ||
        CompareStringOrdinal(path.filename().c_str(), -1, source.filename().c_str(), -1, TRUE) != CSTR_EQUAL)
        return false;
    const FileHandle parent(CreateFileW(path.parent_path().c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (parent.value == INVALID_HANDLE_VALUE)
        return false;
    FILE_CASE_SENSITIVE_INFO info{};
    if (GetFileInformationByHandleEx(parent.value, FileCaseSensitiveInfo, &info, sizeof(info)))
        return !(info.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR);
    // Older Windows and filesystems without directory flags use ordinary Windows path matching.
    const auto status = GetLastError();
    return status == ERROR_INVALID_PARAMETER || status == ERROR_NOT_SUPPORTED ||
        status == ERROR_INVALID_FUNCTION;
}

enum class Kind { Actor, CameraActor, Component, Camera, Mesh };
struct Type
{
    std::string_view key;
    Kind kind;
    const std::type_info& info;
    GameObject* (*create)(Scene&, Actor*, ActorComponent*);
    bool IsActor() const { return kind == Kind::Actor || kind == Kind::CameraActor; }
};

template<class T>
GameObject* Create(Scene& scene, Actor* owner, ActorComponent* parent)
{
    if constexpr (std::derived_from<T, Actor>)
        return scene.SpawnActor<T>();
    else
        return parent ? parent->AddComponent<T>() : owner->AddComponent<T>();
}

const Type types[] = {
    { "actor", Kind::Actor, typeid(Actor), Create<Actor> },
    { "camera_actor", Kind::CameraActor, typeid(CameraActor), Create<CameraActor> },
    { "actor_component", Kind::Component, typeid(ActorComponent), Create<ActorComponent> },
    { "camera_component", Kind::Camera, typeid(CameraComponent), Create<CameraComponent> },
    { "mesh_component", Kind::Mesh, typeid(MeshComponent), Create<MeshComponent> }
};
constexpr std::string_view PrimarySlot = "primary_camera";

const Type* FindType(std::string_view key)
{
    const auto found = std::ranges::find(types, key, &Type::key);
    return found == std::end(types) ? nullptr : found;
}

const Type* FindType(const GameObject& object)
{
    const auto found = std::ranges::find_if(types,
        [&](const Type& entry) { return entry.info == typeid(object); });
    return found == std::end(types) ? nullptr : found;
}

Result<Transform> DecodePose(const Pose& pose)
{
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!std::ranges::all_of(pose.position, finite) ||
        !std::ranges::all_of(pose.rotation, finite) ||
        !std::ranges::all_of(pose.scale, finite))
        return std::unexpected("Transform values must be finite.");
    const auto& r = pose.rotation;
    double squared = 0;
    for (double axis : r)
        squared += axis * axis;
    if (squared == 0)
        return std::unexpected("Rotation must have nonzero length.");
    const auto length = std::abs(squared - 1.0) <= std::numeric_limits<float>::epsilon() ?
        1.0 : std::sqrt(squared);
    return Transform{
        { pose.position[0], pose.position[1], pose.position[2] },
        { static_cast<float>(r[0] / length), static_cast<float>(r[1] / length),
            static_cast<float>(r[2] / length), static_cast<float>(r[3] / length) },
        { pose.scale[0], pose.scale[1], pose.scale[2] }
    };
}

Pose EncodePose(const Transform& transform)
{
    const auto& [p, r, s] = transform;
    return { { p.x, p.y, p.z }, { r.w, r.x, r.y, r.z }, { s.x, s.y, s.z } };
}

const Transform& LocalPose(const GameObject& object)
{
    if (const auto* component = dynamic_cast<const ActorComponent*>(&object))
        return component->GetLocalTransform();
    return static_cast<const Actor&>(object).GetLocalTransform();
}

void SetLocalPose(GameObject& object, const Transform& transform)
{
    if (auto* component = dynamic_cast<ActorComponent*>(&object))
        component->SetLocalTransform(transform);
    else
        static_cast<Actor&>(object).SetLocalTransform(transform);
}

bool FiniteWorld(const GameObject& object)
{
    const auto* component = dynamic_cast<const ActorComponent*>(&object);
    const auto world = component ? component->GetWorldMatrix() :
        static_cast<const Actor&>(object).GetWorldMatrix();
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(world[column][row]))
                return false;
    return true;
}

bool InSubtree(const GameObject& root, const GameObject& object)
{
    if (&root == &object)
        return true;
    const auto* component = dynamic_cast<const ActorComponent*>(&object);
    if (!component)
        return false;
    if (&component->GetActor() == &root)
        return true;
    for (auto* parent = component->GetParent(); parent; parent = parent->GetParent())
        if (parent == &root)
            return true;
    return false;
}

template<class T>
Result<Properties> ReadProperties(std::string_view json)
{
    auto result = Read<T>(json);
    if (!result)
        return std::unexpected(Describe(result.error()));
    return Properties(std::move(*result));
}

Result<Properties> DecodeProperties(Kind kind, std::string_view json)
{
    if (kind == Kind::Mesh)
        return ReadProperties<Mesh>(json);
    if (kind != Kind::Camera)
        return ReadProperties<Empty>(json);
    const auto mode = ReadCameraMode(json);
    if (!mode)
        return std::unexpected(Describe(mode.error()));
    if (*mode == "perspective")
        return ReadProperties<Perspective>(json);
    if (*mode == "orthographic")
        return ReadProperties<Orthographic>(json);
    return std::unexpected("mode must be perspective or orthographic.");
}

Properties ExtractProperties(const GameObject& object)
{
    if (const auto* mesh = dynamic_cast<const MeshComponent*>(&object))
    {
        const auto color = mesh->GetColor();
        return Mesh{ { color.r, color.g, color.b }, mesh->GetAsset() };
    }
    if (const auto* camera = dynamic_cast<const CameraComponent*>(&object))
    {
        const auto near_plane = camera->GetNearPlane();
        const auto far_plane = camera->GetFarPlane();
        if (camera->GetProjectionMode() == ProjectionMode::Perspective)
            return Perspective{ "perspective", camera->GetFieldOfView(), near_plane, far_plane };
        return Orthographic{ "orthographic", camera->GetOrthographicSize(), near_plane, far_plane };
    }
    return Empty{};
}

struct MeshCache
{
    std::unordered_map<std::string_view, fs::path> paths;
    std::unordered_map<fs::path, MeshHandle> handles;
};

Result<void> ApplyProperties(GameObject& object, const Properties& properties,
    const fs::path& root, IEngineServices* services, MeshCache* cache = nullptr)
{
    return std::visit([&]<class T>(const T& value) -> Result<void>
    {
        if constexpr (std::same_as<T, Perspective> || std::same_as<T, Orthographic>)
        {
            auto& camera = static_cast<CameraComponent&>(object);
            const auto applied = [&]
            {
                if constexpr (std::same_as<T, Perspective>)
                    return camera.SetPerspective(value.fov_y_radians, value.near_plane, value.far_plane);
                else
                    return camera.SetOrthographic(value.vertical_size, value.near_plane, value.far_plane);
            }();
            if (!applied)
                return std::unexpected(applied.error());
        }
        else if constexpr (std::same_as<T, Mesh>)
        {
            if (!std::ranges::all_of(value.color, [](float x) { return std::isfinite(x); }))
                return std::unexpected("color must contain finite values.");
            fs::path asset;
            if (cache)
            {
                if (const auto found = cache->paths.find(value.asset); found != cache->paths.end())
                    asset = found->second;
            }
            if (asset.empty())
            {
                auto resolved = ResolveAsset(root, value.asset);
                if (!resolved)
                    return std::unexpected(std::format("asset '{}': {}", value.asset, resolved.error()));
                asset = std::move(*resolved);
                if (cache)
                    cache->paths.emplace(value.asset, asset);
            }
            auto& mesh = static_cast<MeshComponent&>(object);
            if (services && mesh.GetAsset() != value.asset)
            {
                auto* cached = cache ? &cache->handles[asset] : nullptr;
                MeshHandle handle = cached ? *cached : MeshHandle{};
                if (handle.Status() != MeshStatus::Ready)
                {
                    handle = services->LoadMeshAsync(asset);
                    if (const auto loaded = handle.Wait(); !loaded)
                        return std::unexpected(std::format("asset '{}': {}", value.asset, loaded.error()));
                    if (cached)
                        *cached = handle;
                }
                mesh.SetMesh(std::move(handle), value.asset);
            }
            mesh.SetColor({ value.color[0], value.color[1], value.color[2] });
        }
        return {};
    }, properties);
}

Result<std::unique_ptr<Scene>> Build(const Document& document, const fs::path& root,
    IEngineServices* services)
{
    struct Node
    {
        const Type* type;
        Transform pose;
        Properties properties;
        unsigned depth = 0;
        unsigned state = 0;
    };
    std::vector<Node> nodes;
    std::unordered_map<std::string_view, std::size_t> ids;
    for (const auto& record : document.objects)
    {
        if (record.id.empty() || !ids.emplace(record.id, nodes.size()).second)
            return Field(record, "id", "Expected a unique nonempty ID.");
        const auto* type = FindType(record.type);
        if (!type)
            return Field(record, "type", "Unknown type.");
        const auto pose = DecodePose(record.transform);
        if (!pose)
            return Field(record, "transform", pose.error());
        auto properties = DecodeProperties(type->kind, record.properties.str);
        if (!properties)
            return Field(record, "properties", properties.error());
        nodes.push_back({ type, *pose, std::move(*properties) });
    }

    std::unordered_set<std::string_view> primary;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& record = document.objects[i];
        const auto& type = *nodes[i].type;
        if (type.IsActor())
        {
            if (record.owner || record.parent || record.slot)
                return Field(record, "owner/parent/slot", "Actors must have null links.");
            continue;
        }
        const auto owner = record.owner ? ids.find(*record.owner) : ids.end();
        if (owner == ids.end() || !nodes[owner->second].type->IsActor())
            return Field(record, "owner", "Expected an existing Actor.");
        if (record.parent)
        {
            const auto parent = ids.find(*record.parent);
            if (parent == ids.end() || nodes[parent->second].type->IsActor() ||
                document.objects[parent->second].owner != record.owner)
                return Field(record, "parent", "Expected a component with the same owner.");
        }
        if (record.slot && (*record.slot != PrimarySlot || record.parent ||
            type.kind != Kind::Camera || nodes[owner->second].type->kind != Kind::CameraActor ||
            !primary.insert(*record.owner).second))
            return Field(record, "slot", "Invalid or duplicate primary camera slot.");
    }
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].type->kind == Kind::CameraActor && !primary.contains(document.objects[i].id))
            return Field(document.objects[i], "slot", "CameraActor requires its primary camera.");
    if (document.active_camera)
    {
        const auto camera = ids.find(*document.active_camera);
        if (camera == ids.end() || nodes[camera->second].type->kind != Kind::Camera)
            return std::unexpected("active_camera must reference an existing CameraComponent.");
    }

    std::vector<std::size_t> order;
    const auto visit = [&](this const auto& self, std::size_t i, unsigned stack) -> Result<void>
    {
        auto& node = nodes[i];
        const auto& record = document.objects[i];
        if (node.state == 1 || stack > 64)
            return Field(record, "parent", "Cycle or component depth exceeds 64.");
        if (node.state == 2)
            return {};
        node.state = 1;
        if (record.parent)
        {
            const auto p = ids.find(*record.parent)->second;
            if (const auto valid = self(p, stack + 1); !valid)
                return valid;
            node.depth = nodes[p].depth + 1;
            if (node.depth >= 64)
                return Field(record, "parent", "Component depth exceeds 64.");
        }
        node.state = 2;
        order.push_back(i);
        return {};
    };
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].type->IsActor())
        {
            nodes[i].state = 2;
            order.push_back(i);
        }
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (const auto valid = visit(i, 1); !valid)
            return std::unexpected(valid.error());

    auto scene = std::make_unique<Scene>();
    std::vector<GameObject*> objects(nodes.size());
    for (const auto i : order)
    {
        const auto& record = document.objects[i];
        const auto& node = nodes[i];
        auto* owner = record.owner ? static_cast<Actor*>(objects[ids.find(*record.owner)->second]) : nullptr;
        auto* parent = record.parent ?
            static_cast<ActorComponent*>(objects[ids.find(*record.parent)->second]) : nullptr;
        auto* object = record.slot ? &static_cast<CameraActor*>(owner)->GetCameraComponent() :
            node.type->create(*scene, owner, parent);
        object->SetLevelId(record.id);
        SetLocalPose(*object, node.pose);
        if (!FiniteWorld(*object))
            return Field(record, "transform", "World transform overflow.");
        objects[i] = object;
    }
    // Validate setters and asset paths before scheduling any mesh jobs.
    MeshCache meshes;
    for (const auto i : order)
        if (const auto applied = ApplyProperties(*objects[i], nodes[i].properties, root, nullptr, &meshes); !applied)
            return Field(document.objects[i], "properties", applied.error());
    if (services)
        for (const auto i : order)
            if (nodes[i].type->kind == Kind::Mesh)
                if (const auto applied = ApplyProperties(*objects[i], nodes[i].properties, root, services, &meshes); !applied)
                    return Field(document.objects[i], "properties", applied.error());
    if (document.active_camera)
    {
        auto* camera = static_cast<CameraComponent*>(objects[ids.find(*document.active_camera)->second]);
        if (const auto active = scene->SetActiveCamera(camera); !active)
            return std::unexpected(active.error());
    }
    scene->UpdateWorldTransforms();
    return scene;
}

GameObject* FindObject(const Scene& scene, std::string_view id)
{
    if (!id.empty())
        for (auto* object : scene.GetLiveObjects())
            if (object->GetLevelId() == id)
                return object;
    return nullptr;
}

bool IsPrimary(const ActorComponent& component)
{
    const auto* camera_actor = dynamic_cast<const CameraActor*>(&component.GetActor());
    return camera_actor && camera_actor->FindPrimaryCamera() == &component;
}

Result<Document> Capture(const Scene& scene)
{
    Document document;
    if (const auto* active = scene.FindActiveCamera())
        document.active_camera = active->GetLevelId();
    for (const auto* object : scene.GetLiveObjects())
    {
        if (object->GetLevelId().empty())
            continue;
        Record record;
        record.id = object->GetLevelId();
        const auto* type = FindType(*object);
        if (!type)
            return Field(record, "type", "Unregistered C++ type.");
        record.type = type->key;
        if (const auto* component = dynamic_cast<const ActorComponent*>(object))
        {
            record.owner = component->GetActor().GetLevelId();
            if (const auto* parent = component->GetParent())
                record.parent = parent->GetLevelId();
            if (IsPrimary(*component))
                record.slot = PrimarySlot;
        }
        record.transform = EncodePose(LocalPose(*object));
        if (const auto pose = DecodePose(record.transform); !pose)
            return Field(record, "transform", pose.error());
        auto written = std::visit([](const auto& value) { return Write(value); },
            ExtractProperties(*object));
        if (!written)
            return Field(record, "properties", Describe(written.error()));
        record.properties.str = std::move(*written);
        document.objects.push_back(std::move(record));
    }
    return document;
}

Result<void> Replace(const fs::path& path, const std::string& bytes, const std::string* original)
{
    auto lock_path = path;
    lock_path += L".save-lock";
    const FileHandle lock(CreateFileW(lock_path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (lock.value == INVALID_HANDLE_VALUE)
        return std::unexpected(WindowsError("Cannot lock level path for saving"));

    static std::atomic_uint sequence = 0;
    fs::path temp;
    HANDLE handle = INVALID_HANDLE_VALUE;
    do
    {
        temp = path;
        temp += std::format(L".{}.{}.tmp", GetCurrentProcessId(), ++sequence);
        handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
    } while (handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS);
    if (handle == INVALID_HANDLE_VALUE)
        return std::unexpected(WindowsError("Cannot create temporary level file"));

    struct Temp
    {
        const fs::path& path;
        ~Temp() { DeleteFileW(path.c_str()); }
    } cleanup{ temp };
    FileHandle output(handle);
    DWORD count = 0;
    if (!WriteFile(output.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
        count != bytes.size() || !FlushFileBuffers(output.value) || !output.Close())
        return std::unexpected(WindowsError("Cannot write temporary level file"));

    if (original)
    {
        const FileHandle current(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (current.value == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                return std::unexpected("Save conflict: the original level was removed.");
            return std::unexpected(WindowsError("Cannot check original level for changes"));
        }
        const auto observed = ReadFileBytes(current.value);
        if (!observed)
            return std::unexpected(observed.error());
        if (*observed != *original)
            return std::unexpected("Save conflict: the original level changed on disk.");
    }
    // shortcut: an external writer can race the rename, use an out-of-process protocol if required.
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return std::unexpected(WindowsError("Cannot replace level file"));
    return {};
}
}

Level::Level(fs::path content_root) : content_root_(std::move(content_root)) {}

Result<fs::path> Level::ExecutableContentRoot()
{
    std::wstring path(256, L'\0');
    for (;;)
    {
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length)
            return std::unexpected(WindowsError("Cannot locate the engine executable"));
        if (length < path.size())
        {
            path.resize(length);
            return fs::path(path).parent_path() / L"content";
        }
        path.resize(path.size() * 2);
    }
}

Result<void> Level::Open(const fs::path& file, IEngineServices& services)
{
    std::error_code error;
    auto path = fs::canonical(file, error);
    if (error)
        return std::unexpected("Cannot resolve level file.");
    FileHandle input(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (input.value == INVALID_HANDLE_VALUE)
        return std::unexpected(WindowsError("Cannot open level file"));
    auto bytes = ReadFileBytes(input.value);
    if (!bytes)
        return std::unexpected(bytes.error());
    auto document = ReadDocument(*bytes);
    if (!document)
        return std::unexpected(Describe(document.error()));
    auto candidate = Build(*document, content_root_, &services);
    if (!candidate)
        return std::unexpected(candidate.error());
    scene_ = std::move(*candidate);
    source_ = std::move(path);
    original_ = std::move(*bytes);
    return {};
}

Result<void> Level::Save(const fs::path& file)
{
    std::error_code error;
    auto path = fs::weakly_canonical(file, error);
    if (error)
        return std::unexpected("Cannot resolve the save path.");
    const auto document = Capture(*scene_);
    if (!document)
        return std::unexpected(document.error());
    auto bytes = Write(*document);
    if (!bytes)
        return std::unexpected(Describe(bytes.error()));
    const auto checked = ReadDocument(*bytes);
    if (!checked)
        return std::unexpected(Describe(checked.error()));
    const auto validated = Build(*checked, content_root_, nullptr);
    if (!validated)
        return std::unexpected(validated.error());
    const bool same_file = !source_.empty() && (path == source_ || fs::equivalent(path, source_, error) ||
        MatchesMissingSource(path, source_));
    if (const auto saved = Replace(path, *bytes, same_file ? &original_ : nullptr); !saved)
        return saved;
    source_ = std::move(path);
    original_ = std::move(*bytes);
    return {};
}

Result<void> Level::Place(const LevelPlacement& placement, IEngineServices& services)
{
    if (placement.id.empty() || FindObject(*scene_, placement.id))
        return std::unexpected("Expected a unique nonempty ID.");
    if (placement.id.size() > MaxBytes ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, placement.id.data(),
            static_cast<int>(placement.id.size()), nullptr, 0))
        return std::unexpected("Expected a UTF-8 level ID within the file size limit.");
    const auto* type = FindType(placement.type);
    if (!type)
        return std::unexpected("Unknown placement type.");
    Actor* owner = nullptr;
    ActorComponent* parent = nullptr;
    if (type->IsActor())
    {
        if (placement.owner || placement.parent)
            return std::unexpected("Actor placements cannot have an owner or parent.");
    }
    else
    {
        if (placement.owner)
            owner = dynamic_cast<Actor*>(FindObject(*scene_, *placement.owner));
        if (!owner)
            return std::unexpected("Placement owner must be a level Actor.");
        if (placement.parent)
        {
            parent = dynamic_cast<ActorComponent*>(FindObject(*scene_, *placement.parent));
            if (!parent || &parent->GetActor() != owner)
                return std::unexpected("Placement parent must be a component with the same owner.");
            unsigned depth = 1;
            for (auto* ancestor = parent; ancestor; ancestor = ancestor->GetParent())
                if (++depth > 64)
                    return std::unexpected("Component depth exceeds 64.");
        }
    }
    const auto primary_id = placement.id + "/primary_camera";
    if (type->kind == Kind::CameraActor && FindObject(*scene_, primary_id))
        return std::unexpected("Primary camera ID already exists.");
    const auto pose = DecodePose(EncodePose(placement.transform));
    if (!pose)
        return std::unexpected(pose.error());
    const auto properties = DecodeProperties(type->kind, placement.properties);
    if (!properties)
        return std::unexpected(properties.error());
    auto* object = type->create(*scene_, owner, parent);
    if (!object)
        return std::unexpected("Scene is being destroyed.");
    SetLocalPose(*object, *pose);
    const auto applied = FiniteWorld(*object) ?
        ApplyProperties(*object, *properties, content_root_, &services) :
        Result<void>(std::unexpected("World transform overflow."));
    if (!applied)
    {
        if (owner)
            static_cast<ActorComponent*>(object)->Destroy();
        else
            static_cast<Actor*>(object)->Destroy();
        return applied;
    }
    object->SetLevelId(placement.id);
    if (type->kind == Kind::CameraActor)
        static_cast<CameraActor*>(object)->GetCameraComponent().SetLevelId(primary_id);
    return {};
}

Result<void> Level::Remove(std::string_view id)
{
    auto* object = FindObject(*scene_, id);
    if (!object)
        return std::unexpected("Unknown level object.");
    if (auto* component = dynamic_cast<ActorComponent*>(object))
    {
        if (IsPrimary(*component))
            return std::unexpected("Remove the CameraActor to remove its primary camera.");
        component->Destroy();
    }
    else
        static_cast<Actor*>(object)->Destroy();
    return {};
}

Result<std::vector<LevelProperty>> Level::GetProperties(std::string_view id) const
{
    const auto* object = FindObject(*scene_, id);
    if (!object)
        return std::unexpected("Unknown level object.");
    std::vector<LevelProperty> fields;
    const auto pose = EncodePose(LocalPose(*object));
    ForEachProperty(pose, [&](std::string_view name, const auto& member)
    {
        fields.push_back({ std::format("transform.{}", name), member });
    });
    auto properties = ExtractProperties(*object);
    std::visit([&](auto& value)
    {
        ForEachProperty(value, [&](std::string_view name, auto& member)
        {
            fields.push_back({ std::string(name), member });
        });
    }, properties);
    return fields;
}

Result<void> Level::SetProperties(std::string_view id, std::span<const LevelProperty> changes,
    IEngineServices& services)
{
    auto* object = FindObject(*scene_, id);
    if (!object)
        return std::unexpected("Unknown level object.");
    auto properties = ExtractProperties(*object);
    const auto previous_pose = LocalPose(*object);
    auto pose = EncodePose(previous_pose);
    bool pose_changed = false;
    bool properties_changed = false;
    std::unordered_set<std::string_view> changed;
    if (auto* camera = dynamic_cast<CameraComponent*>(object))
        for (const auto& change : changes)
            if (change.name == "mode")
            {
                const auto* mode = std::get_if<std::string>(&change.value);
                if (!mode || (*mode != "perspective" && *mode != "orthographic"))
                    return std::unexpected("mode must be perspective or orthographic.");
                if (*mode == "perspective")
                    properties = Perspective{ *mode, camera->GetFieldOfView(),
                        camera->GetNearPlane(), camera->GetFarPlane() };
                else
                    properties = Orthographic{ *mode, camera->GetOrthographicSize(),
                        camera->GetNearPlane(), camera->GetFarPlane() };
                break;
            }
    for (const auto& change : changes)
    {
        if (!changed.insert(change.name).second)
            return std::unexpected(std::format("Duplicate property '{}'.", change.name));
        bool accepted = false;
        const auto replace = [&](std::string_view field, std::string_view name, auto& member)
        {
            if (name == field)
                if (const auto* replacement = std::get_if<std::decay_t<decltype(member)>>(&change.value))
                {
                    member = *replacement;
                    accepted = true;
                }
        };
        const std::string_view name = change.name;
        if (name.starts_with("transform."))
        {
            ForEachProperty(pose, [&](std::string_view key, auto& member)
            {
                replace(name.substr(10), key, member);
            });
            pose_changed |= accepted;
        }
        else
        {
            std::visit([&](auto& value)
            {
                ForEachProperty(value, [&](std::string_view key, auto& member)
                {
                    replace(name, key, member);
                });
            }, properties);
            properties_changed |= accepted;
        }
        if (!accepted)
            return std::unexpected(std::format("Unknown property or wrong value type for '{}'.", change.name));
    }
    if (pose_changed)
    {
        const auto transform = DecodePose(pose);
        if (!transform)
            return std::unexpected(transform.error());
        SetLocalPose(*object, *transform);
        for (const auto* affected : scene_->GetLiveObjects())
            if (InSubtree(*object, *affected) && !FiniteWorld(*affected))
            {
                SetLocalPose(*object, previous_pose);
                return std::unexpected("World transform overflow.");
            }
    }
    if (!properties_changed)
        return {};
    const auto applied = ApplyProperties(*object, properties, content_root_, &services);
    if (!applied && pose_changed)
        SetLocalPose(*object, previous_pose);
    return applied;
}
