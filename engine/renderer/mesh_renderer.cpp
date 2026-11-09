// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module Renderer;
import <GL/glew.h>;
import <cassert>;

namespace
{
void Fail(const std::shared_ptr<MeshDetail::LoadState>& state, std::string error)
{
    state->error = std::move(error);
    state->status.store(MeshStatus::Failed, std::memory_order_release);
    state->status.notify_all();
}

unsigned Compile(unsigned type, const char* source)
{
    const auto shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
}

MeshHandle Renderer::LoadMeshAsync(std::filesystem::path path)
{
    assert(std::this_thread::get_id() == owner_thread_);
    auto state = std::make_shared<MeshDetail::LoadState>();
    if (!thread_.joinable() || !jobs_)
    {
        Fail(state, "Mesh loading requires an initialized renderer.");
        return MeshHandle(state);
    }
    jobs_->SubmitIo([this, state, path = std::move(path)]
    {
        auto bytes = MeshLoading::Read(path);
        if (!bytes)
        {
            Fail(state, std::move(bytes.error()));
            return;
        }
        jobs_->SubmitCpu([this, state, bytes = std::move(*bytes)]
        {
            auto data = MeshLoading::Parse(bytes);
            if (!data)
            {
                Fail(state, std::move(data.error()));
                return;
            }
            // Submit only prepared data: an IO prerequisite here would stall later frames.
            jobs_->SubmitRender([this, state, data = std::move(*data)]
            {
                auto uploaded = Upload(data);
                if (!uploaded)
                {
                    Fail(state, uploaded.error());
                    return;
                }
                state->data = data;
                state->status.store(MeshStatus::Ready, std::memory_order_release);
                state->status.notify_all();
            });
        });
    });
    return MeshHandle(state);
}

std::expected<void, const char*> Renderer::InitializeMeshRendering()
{
    constexpr const char* vertex = R"(#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 transform;
void main() { gl_Position = transform * vec4(position, 1.0); }
)";
    constexpr const char* fragment = R"(#version 330 core
uniform vec3 color;
out vec4 output_color;
void main() { output_color = vec4(color, 1.0); }
)";
    const auto vs = Compile(GL_VERTEX_SHADER, vertex);
    const auto fs = Compile(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return std::unexpected("Mesh shader compilation failed.");
    }
    program_ = glCreateProgram();
    if (program_)
    {
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    int linked = 0;
    if (program_) glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) return std::unexpected("Mesh shader linking failed.");
    transform_location_ = glGetUniformLocation(program_, "transform");
    color_location_ = glGetUniformLocation(program_, "color");
    if (transform_location_ < 0 || color_location_ < 0)
        return std::unexpected("Mesh shader uniforms are unavailable.");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    return {};
}

std::expected<Renderer::Resource*, const char*> Renderer::Upload(const std::shared_ptr<const MeshData>& data)
{
    assert(std::this_thread::get_id() != owner_thread_);
    if (!data) return std::unexpected("Missing mesh data.");
    const auto existing = meshes_.find(data.get());
    if (existing != meshes_.end())
    {
        if (!existing->second.owner.expired())
        {
            if (existing->second.error) return std::unexpected(existing->second.error);
            return &existing->second;
        }
        Release(existing->second);
        meshes_.erase(existing);
    }

    // CPU data stays available so another initialization can recreate its GPU objects.
    Resource resource;
    resource.owner = data;
    while (glGetError() != GL_NO_ERROR) {}
    for (const auto& source : data->geometries)
    {
        auto& geometry = resource.geometries.emplace_back();
        glGenVertexArrays(1, &geometry.vao);
        glGenBuffers(1, &geometry.vertices);
        glGenBuffers(1, &geometry.indices);
        glBindVertexArray(geometry.vao);
        glBindBuffer(GL_ARRAY_BUFFER, geometry.vertices);
        glBufferData(GL_ARRAY_BUFFER, source.positions.size() * sizeof(glm::vec3),
            source.positions.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, source.indices.size() * sizeof(std::uint32_t),
            source.indices.data(), GL_STATIC_DRAW);
        geometry.count = static_cast<int>(source.indices.size());
        if (!geometry.vao || !geometry.vertices || !geometry.indices || glGetError() != GL_NO_ERROR)
        {
            glBindVertexArray(0);
            Release(resource);
            resource.error = "Mesh GPU upload failed.";
            meshes_.emplace(data.get(), std::move(resource));
            return std::unexpected("Mesh GPU upload failed.");
        }
    }
    glBindVertexArray(0);
    return &meshes_.emplace(data.get(), std::move(resource)).first->second;
}

void Renderer::Release(Resource& resource)
{
    for (auto& geometry : resource.geometries)
    {
        glDeleteBuffers(1, &geometry.indices);
        glDeleteBuffers(1, &geometry.vertices);
        glDeleteVertexArrays(1, &geometry.vao);
    }
    resource.geometries.clear();
}

void Renderer::CollectUnusedMeshes()
{
    for (auto it = meshes_.begin(); it != meshes_.end(); )
        if (it->second.owner.expired())
        {
            Release(it->second);
            it = meshes_.erase(it);
        }
        else
            ++it;
}

void Renderer::ReleaseMeshRendering()
{
    for (auto& [key, resource] : meshes_)
        Release(resource);
    meshes_.clear();
    if (program_) glDeleteProgram(program_);
    program_ = 0;
    transform_location_ = color_location_ = -1;
}
