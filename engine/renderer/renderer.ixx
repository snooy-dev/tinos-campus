// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Renderer;

import std;
import Application;
import JobSystem;
export import MeshResource;

export struct RenderFrame
{
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glm::mat4 view_projection{ 1.0f };
    std::vector<MeshDraw> meshes;
    glm::vec4 camera{ 0.0f, 0.0f, 1.0f, 0.0f };
    glm::vec3 direction_to_light{ 0.4f, 1.0f, 0.6f };
    glm::mat4 light_view_projection{ 1.0f };
    bool shadows_enabled = false;
};

// Lifecycle and frame submission belong to the main thread; Application and JobSystem must outlive Renderer.
export class Renderer
{
public:
    ~Renderer();

    [[nodiscard]] std::expected<void, const char*> Initialize(Application& application, JobSystem& jobs);
    // Drains the shared job system and closes its external submissions before detaching.
    void Shutdown();
    // Wait before sampling input; SubmitFrame also enforces the two-slot limit.
    void WaitForFrameSlot();
    [[nodiscard]] MeshHandle LoadMeshAsync(std::filesystem::path path);
    // Completion covers the render task, not GPU execution or display scanout.
    JobHandle SubmitFrame(RenderFrame frame, std::span<const JobHandle> prerequisites = {});

private:
    void Run();
    void Render(const RenderFrame& frame);
    void RenderShadowMap(const RenderFrame& frame);
    [[nodiscard]] std::expected<void, const char*> InitializeMeshRendering();
    struct Geometry
    {
        unsigned vao = 0, vertices = 0, normals = 0, texcoords = 0, indices = 0;
        int count = 0;
    };
    struct Resource
    {
        std::weak_ptr<const MeshData> owner;
        std::vector<Geometry> geometries;
        std::vector<unsigned> textures;
        const char* error = nullptr;
        bool error_reported = false;
    };
    [[nodiscard]] std::expected<Resource*, const char*> Upload(const std::shared_ptr<const MeshData>& data);
    static void Release(Resource& resource);
    void CollectUnusedMeshes();
    void ReleaseMeshRendering();
    std::unordered_map<const MeshData*, Resource> meshes_;
    unsigned program_ = 0;
    unsigned shadow_program_ = 0, shadow_fbo_ = 0, shadow_depth_ = 0;
    static constexpr int kShadowSize = 1024;
    struct ShadowLocations
    {
        int light_view_projection, model, base_alpha, alpha_cutoff, has_base_texture;
    } shadow_uniforms_{};
    struct ShaderLocations
    {
        int view_projection, model, normal_matrix, camera, base_color;
        int metallic, roughness, alpha_cutoff, has_normals, has_base_texture, has_metallic_roughness_texture;
        int light_view_projection, direction_to_light, shadows_enabled;
    } uniforms_{};

    const std::thread::id owner_thread_{ std::this_thread::get_id() };
    Application* application_ = nullptr;
    JobSystem* jobs_ = nullptr;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::array<JobHandle, 2> frame_fences_;
    std::size_t next_frame_ = 0;
    std::expected<void, const char*> initialization_result_;
    bool ready_ = false;
    std::jthread thread_;
};
