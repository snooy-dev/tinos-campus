// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
export module MeshResource;
import std;
import <cassert>;
export import Transform;

export struct MeshTexture
{
    int width = 0, height = 0;
    int min_filter = 9987, mag_filter = 9729;
    int wrap_s = 10497, wrap_t = 10497;
    bool srgb = false;
    std::vector<std::uint8_t> rgba;
};

export struct MeshMaterial
{
    glm::vec4 base_color{ 1.0f };
    float metallic = 0.0f, roughness = 1.0f;
    float alpha_cutoff = -1.0f;
    bool double_sided = true;
    std::optional<std::size_t> base_texture, metallic_roughness_texture;
};

export struct MeshGeometry
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;
    std::vector<std::uint32_t> indices;
    MeshMaterial material;
    std::vector<glm::vec4> joints;
    std::vector<glm::vec4> weights;
    std::vector<glm::vec4> morph_deltas;
};

export struct MeshInstance
{
    std::size_t geometry;
    glm::mat4 transform{ 1.0f };
    std::optional<std::size_t> skin, pose_node;
};

export inline constexpr std::size_t MeshJointLimit = 32;
export inline constexpr std::size_t MeshMorphLimit = 32;

export struct MeshJoint
{
    std::size_t node;
    glm::mat4 inverse_bind{ 1.0f };
};

export struct MeshPoseNode
{
    std::optional<std::size_t> parent;
    Transform rest;
    std::optional<glm::mat4> matrix;
    std::vector<float> morph_weights;
};

export struct MeshAnimationTrack
{
    enum class Property { Translation, Rotation, Scale, MorphWeights };
    std::size_t node;
    Property property;
    bool step;
    std::vector<float> times;
    std::vector<glm::vec4> values;
    std::vector<float> morph_values;
};

export struct MeshPose
{
    std::vector<glm::mat4> nodes;
    std::vector<std::vector<glm::mat4>> skins;
    std::vector<std::vector<float>> morph_weights;
};

export struct MeshData
{
    std::vector<MeshGeometry> geometries;
    std::vector<MeshInstance> instances;
    std::vector<MeshTexture> textures;
    std::vector<std::vector<MeshJoint>> skins;
    std::vector<MeshPoseNode> pose_nodes;
    std::vector<MeshAnimationTrack> tracks;
    float animation_start = 0.0f;
    float animation_duration = 0.0f;

    [[nodiscard]] MeshPose SamplePose(std::optional<double> seconds) const
    {
        if (skins.empty()) return {};

        std::vector<Transform> local;
        local.reserve(pose_nodes.size());
        MeshPose pose;
        pose.morph_weights.reserve(pose_nodes.size());
        for (const auto& node : pose_nodes)
        {
            local.push_back(node.rest);
            pose.morph_weights.push_back(node.morph_weights);
        }

        if (seconds && animation_duration > 0.0f)
        {
            const float time = animation_start + static_cast<float>(std::fmod(*seconds, animation_duration));
            for (const auto& track : tracks)
            {
                const auto lower = static_cast<std::size_t>(
                    std::ranges::upper_bound(track.times, time) - track.times.begin());
                const auto first = lower > 0 ? lower - 1 : 0;
                const auto last = std::min(lower, track.times.size() - 1);
                const float blend = track.step || first == last ? 0.0f :
                    (time - track.times[first]) / (track.times[last] - track.times[first]);
                auto& node = local[track.node];
                if (track.property == MeshAnimationTrack::Property::MorphWeights)
                {
                    auto& weights = pose.morph_weights[track.node];
                    for (std::size_t i = 0; i < weights.size(); ++i)
                        weights[i] = std::lerp(track.morph_values[first * weights.size() + i],
                            track.morph_values[last * weights.size() + i], blend);
                    continue;
                }
                const auto& a = track.values[first];
                const auto& b = track.values[last];
                switch (track.property)
                {
                case MeshAnimationTrack::Property::Translation:
                    node.position = glm::mix(glm::vec3(a), glm::vec3(b), blend);
                    break;
                case MeshAnimationTrack::Property::Rotation:
                    node.rotation = glm::normalize(glm::slerp(glm::quat(a.w, a.x, a.y, a.z),
                        glm::quat(b.w, b.x, b.y, b.z), blend));
                    break;
                case MeshAnimationTrack::Property::Scale:
                    node.scale = glm::mix(glm::vec3(a), glm::vec3(b), blend);
                    break;
                default:
                    break;
                }
            }
        }

        pose.nodes.reserve(pose_nodes.size());
        for (std::size_t i = 0; i < pose_nodes.size(); ++i)
        {
            const auto& node = pose_nodes[i];
            const auto transform = node.matrix ? *node.matrix : local[i].ToMatrix();
            pose.nodes.push_back(node.parent ? pose.nodes[*node.parent] * transform : transform);
        }
        pose.skins.reserve(skins.size());
        for (const auto& skin : skins)
        {
            auto& palette = pose.skins.emplace_back();
            palette.reserve(skin.size());
            for (const auto& joint : skin)
                palette.push_back(pose.nodes[joint.node] * joint.inverse_bind);
        }
        return pose;
    }
};

// Status describes the initial load, not GPU residency after renderer reinitialization.
export enum class MeshStatus { Loading, Ready, Failed };

export namespace MeshDetail
{
struct LoadState
{
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic<MeshStatus> status{ MeshStatus::Loading };
    std::shared_ptr<const MeshData> data;
    std::string error;
};
}

// Handles retain CPU geometry; GPU objects belong exclusively to Renderer.
export class MeshHandle
{
public:
    MeshHandle() = default;
    explicit MeshHandle(std::shared_ptr<MeshDetail::LoadState> state) : state_(std::move(state)) {}
    [[nodiscard]] MeshStatus Status() const
    {
        return state_ ? state_->status.load(std::memory_order_acquire) : MeshStatus::Failed;
    }
    [[nodiscard]] std::string_view Error() const
    {
        if (!state_) return "No mesh request.";
        return Status() == MeshStatus::Failed ? std::string_view(state_->error) : std::string_view{};
    }
    [[nodiscard]] std::shared_ptr<const MeshData> Data() const
    {
        return Status() == MeshStatus::Ready ? state_->data : nullptr;
    }
    // Wait only on the requesting main thread, never from a job.
    [[nodiscard]] std::expected<void, std::string> Wait() const
    {
        if (!state_) return std::unexpected("No mesh request.");
        assert(std::this_thread::get_id() == state_->owner);
        auto status = Status();
        while (status == MeshStatus::Loading)
        {
            state_->status.wait(status, std::memory_order_acquire);
            status = Status();
        }
        if (status == MeshStatus::Failed) return std::unexpected(state_->error);
        return {};
    }
private:
    std::shared_ptr<MeshDetail::LoadState> state_;
};

export struct MeshDraw
{
    std::shared_ptr<const MeshData> data;
    glm::mat4 world{ 1.0f };
    glm::vec3 color{ 1.0f };
    MeshPose pose;
};

export namespace MeshLoading
{
[[nodiscard]] std::expected<std::vector<std::byte>, std::string> Read(const std::filesystem::path& path);
[[nodiscard]] std::expected<std::shared_ptr<const MeshData>, std::string>
Parse(std::span<const std::byte> bytes);
}
