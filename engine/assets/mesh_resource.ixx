// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
export module MeshResource;
import std;
import <cassert>;
export import Transform;

export struct MeshGeometry
{
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
};

export struct MeshInstance
{
    std::size_t geometry;
    glm::mat4 transform{ 1.0f };
};

export struct MeshData
{
    std::vector<MeshGeometry> geometries;
    std::vector<MeshInstance> instances;
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
};

export namespace MeshLoading
{
[[nodiscard]] std::expected<std::vector<std::byte>, std::string> Read(const std::filesystem::path& path);
[[nodiscard]] std::expected<std::shared_ptr<const MeshData>, std::string>
Parse(std::span<const std::byte> bytes);
}
