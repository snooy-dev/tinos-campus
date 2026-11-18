// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

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
        char log[2048]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        OutputDebugStringA(log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

std::expected<unsigned, const char*> LoadProgram(const std::filesystem::path& vertex_path,
    const std::filesystem::path& fragment_path)
{
    auto vertex = MeshLoading::Read(vertex_path);
    auto fragment = MeshLoading::Read(fragment_path);
    if (!vertex || !fragment)
        return std::unexpected("Cannot read mesh shader files beside the executable.");
    vertex->push_back(std::byte{});
    fragment->push_back(std::byte{});
    const auto vs = Compile(GL_VERTEX_SHADER, reinterpret_cast<const char*>(vertex->data()));
    const auto fs = Compile(GL_FRAGMENT_SHADER, reinterpret_cast<const char*>(fragment->data()));
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return std::unexpected("Mesh shader compilation failed.");
    }
    const auto program = glCreateProgram();
    if (program)
    {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    int linked = 0;
    if (program) glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        if (program)
        {
            char log[2048]{};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            OutputDebugStringA(log);
        }
        if (program) glDeleteProgram(program);
        return std::unexpected("Mesh shader linking failed.");
    }
    return program;
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
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size())
        return std::unexpected("Cannot locate the executable for PBR shader loading.");
    executable.resize(length);
    const auto shaders = std::filesystem::path(executable).parent_path() / "contents" / "shaders";
    auto pbr = LoadProgram(shaders / "pbr.vert", shaders / "pbr.frag");
    if (!pbr) return std::unexpected(pbr.error());
    program_ = *pbr;
    auto shadow = LoadProgram(shaders / "shadow.vert", shaders / "shadow.frag");
    if (!shadow) return std::unexpected(shadow.error());
    shadow_program_ = *shadow;
    const auto uniform = [this](const char* name) { return glGetUniformLocation(program_, name); };
    uniforms_ = { uniform("uViewProjection"), uniform("uModel"), uniform("uNormalMatrix"),
        uniform("uCamera"), uniform("uBaseColor"), uniform("uMetallic"), uniform("uRoughness"),
        uniform("uAlphaCutoff"), uniform("uHasNormals"), uniform("uHasBaseTexture"),
        uniform("uHasMetallicRoughnessTexture"), uniform("uLightViewProjection"),
        uniform("uDirectionToLight"), uniform("uShadowsEnabled"),
        uniform("uSkyAmbient"), uniform("uGroundAmbient") };
    glUseProgram(program_);
    glUniform1i(uniform("uBaseTexture"), 0);
    glUniform1i(uniform("uMetallicRoughnessTexture"), 1);
    glUniform1i(uniform("uShadowMap"), 2);
    glUseProgram(shadow_program_);
    const auto shadow_uniform = [this](const char* name) { return glGetUniformLocation(shadow_program_, name); };
    shadow_uniforms_ = { shadow_uniform("uLightViewProjection"), shadow_uniform("uModel"),
        shadow_uniform("uBaseAlpha"), shadow_uniform("uAlphaCutoff"), shadow_uniform("uHasBaseTexture") };
    glUniform1i(shadow_uniform("uBaseTexture"), 0);

    glGenTextures(1, &shadow_depth_);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize,
        0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glGenFramebuffers(1, &shadow_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_depth_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = shadow_fbo_ && shadow_depth_ &&
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!complete)
        return std::unexpected("Shadow framebuffer creation failed.");
    glDisable(GL_FRAMEBUFFER_SRGB);
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
    glActiveTexture(GL_TEXTURE0);
    while (glGetError() != GL_NO_ERROR) {}
    const auto fail_upload = [&]() -> std::expected<Resource*, const char*>
    {
        glBindVertexArray(0);
        Release(resource);
        resource.error = "Mesh GPU upload failed.";
        meshes_.emplace(data.get(), std::move(resource));
        return std::unexpected("Mesh GPU upload failed.");
    };
    for (const auto& image : data->textures)
    {
        auto& texture = resource.textures.emplace_back(0);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, image.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
            image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, image.min_filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, image.mag_filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, image.wrap_s);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, image.wrap_t);
        if (image.min_filter >= GL_NEAREST_MIPMAP_NEAREST) glGenerateMipmap(GL_TEXTURE_2D);
        if (!texture || glGetError() != GL_NO_ERROR) return fail_upload();
    }
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
        const auto upload_attribute = [](unsigned& buffer, unsigned location, int components, const auto& values)
        {
            if (values.empty()) return;
            glGenBuffers(1, &buffer);
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glBufferData(GL_ARRAY_BUFFER, values.size() * sizeof(values.front()), values.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(location);
            glVertexAttribPointer(location, components, GL_FLOAT, GL_FALSE, sizeof(values.front()), nullptr);
        };
        upload_attribute(geometry.normals, 1, 3, source.normals);
        upload_attribute(geometry.texcoords, 2, 2, source.texcoords);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, source.indices.size() * sizeof(std::uint32_t),
            source.indices.data(), GL_STATIC_DRAW);
        geometry.count = static_cast<int>(source.indices.size());
        if (!geometry.vao || !geometry.vertices || !geometry.indices ||
            (!source.normals.empty() && !geometry.normals) ||
            (!source.texcoords.empty() && !geometry.texcoords) || glGetError() != GL_NO_ERROR)
            return fail_upload();
    }
    glBindVertexArray(0);
    return &meshes_.emplace(data.get(), std::move(resource)).first->second;
}

void Renderer::Release(Resource& resource)
{
    for (auto& geometry : resource.geometries)
    {
        glDeleteBuffers(1, &geometry.indices);
        glDeleteBuffers(1, &geometry.texcoords);
        glDeleteBuffers(1, &geometry.normals);
        glDeleteBuffers(1, &geometry.vertices);
        glDeleteVertexArrays(1, &geometry.vao);
    }
    resource.geometries.clear();
    for (auto texture : resource.textures)
        glDeleteTextures(1, &texture);
    resource.textures.clear();
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
    glDeleteFramebuffers(1, &shadow_fbo_);
    glDeleteTextures(1, &shadow_depth_);
    if (shadow_program_) glDeleteProgram(shadow_program_);
    if (program_) glDeleteProgram(program_);
    program_ = shadow_program_ = shadow_fbo_ = shadow_depth_ = 0;
}
