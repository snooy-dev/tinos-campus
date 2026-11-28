// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
export module Level;
import std;
export import GameFramework;
import GameApi;

export using LevelValue = std::variant<float, std::string, std::array<float, 3>, std::array<float, 4>>;
export struct LevelProperty
{
    std::string name;
    LevelValue value;
};

export struct LevelPlacement
{
    std::string id;
    std::string type;
    std::optional<std::string> owner;
    std::optional<std::string> parent;
    Transform transform;
    std::string properties = "{}";
};

export class Level
{
public:
    explicit Level(std::filesystem::path content_root);
    [[nodiscard]] static std::expected<std::filesystem::path, std::string> ExecutableContentRoot();
    [[nodiscard]] std::expected<Level, std::string> Clone(IEngineServices& services) const;
    [[nodiscard]] Scene& GetScene() { return *scene_; }
    [[nodiscard]] std::expected<void, std::string> Open(
        const std::filesystem::path& file, IEngineServices& services);
    [[nodiscard]] std::expected<void, std::string> Save(const std::filesystem::path& file);
    [[nodiscard]] std::expected<void, std::string> Place(
        const LevelPlacement& placement, IEngineServices& services);
    [[nodiscard]] std::expected<void, std::string> Remove(std::string_view id);
    [[nodiscard]] std::expected<std::vector<LevelProperty>, std::string> GetProperties(
        std::string_view id) const;
    [[nodiscard]] std::expected<void, std::string> SetProperties(std::string_view id,
        std::span<const LevelProperty> changes, IEngineServices& services);

private:
    std::filesystem::path content_root_;
    std::unique_ptr<Scene> scene_ = std::make_unique<Scene>();
    std::filesystem::path source_;
    std::string original_;
};
