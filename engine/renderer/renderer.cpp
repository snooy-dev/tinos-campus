// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Renderer;

import <GL/glew.h>;
import <cassert>;
import "core/log.hpp";

Renderer::~Renderer()
{
    Shutdown();
}

std::expected<void, const char*> Renderer::Initialize(Application& application, JobSystem& jobs)
{
    assert(std::this_thread::get_id() == owner_thread_);
    if (thread_.joinable())
    {
        assert(application_ == &application && jobs_ == &jobs);
        return {};
    }

    application_ = &application;
    jobs_ = &jobs;
    const auto terminate_handler = std::get_terminate();
    thread_ = std::jthread([this, terminate_handler]
        {
            std::set_terminate(terminate_handler);
            Run();
        });

    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return ready_; });
    const auto result = initialization_result_;
    lock.unlock();
    if (!result)
        Shutdown();
    return result;
}

void Renderer::Shutdown()
{
    assert(std::this_thread::get_id() == owner_thread_);
    if (!thread_.joinable())
        return;

    jobs_->Drain();
    jobs_->StopRenderTasks();
    thread_.join();
    application_ = nullptr;
    jobs_ = nullptr;
    frame_fences_ = {};
    next_frame_ = 0;
    ready_ = false;
    initialization_result_ = {};
}

void Renderer::WaitForFrameSlot()
{
    assert(std::this_thread::get_id() == owner_thread_);
    assert(thread_.joinable());
    frame_fences_[next_frame_].Wait();
}

JobHandle Renderer::SubmitFrame(RenderFrame frame, std::span<const JobHandle> prerequisites)
{
    assert(frame.framebuffer_width >= 0 && frame.framebuffer_height >= 0);
    WaitForFrameSlot();
    auto handle = jobs_->SubmitRender([this, frame = std::move(frame)] { Render(frame); }, prerequisites);
    frame_fences_[next_frame_] = handle;
    next_frame_ = (next_frame_ + 1) % frame_fences_.size();
    return handle;
}

void Renderer::Run()
{
    const auto context_result = application_->ActivateContext();
    auto result = context_result;
    if (result)
    {
        glewExperimental = GL_TRUE;
        const GLenum error_code = glewInit();
        if (error_code != GLEW_OK)
            result = std::unexpected(
                reinterpret_cast<const char*>(glewGetErrorString(error_code)));
        else
        {
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            result = InitializeMeshRendering();
        }
    }

    if (result)
        jobs_->AttachRenderThread();

    {
        const std::lock_guard lock(mutex_);
        initialization_result_ = result;
        ready_ = true;
    }
    condition_.notify_one();

    if (result)
        jobs_->RunRenderTasks();

    if (context_result)
    {
        ReleaseMeshRendering();
        application_->ReleaseContext();
    }
}

void Renderer::RenderShadowMap(const RenderFrame& frame)
{
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.1f, 2.0f);
    glUseProgram(shadow_program_);
    glUniformMatrix4fv(shadow_uniforms_.light_view_projection, 1, GL_FALSE,
        &frame.light_view_projection[0][0]);
    for (const auto& draw : frame.meshes)
    {
        const auto resource = Upload(draw.data);
        if (!resource) continue;
        for (const auto& instance : draw.data->instances)
        {
            const auto model = draw.world * instance.transform;
            const auto& geometry = (*resource)->geometries[instance.geometry];
            const auto& material = draw.data->geometries[instance.geometry].material;
            glUniformMatrix4fv(shadow_uniforms_.model, 1, GL_FALSE, &model[0][0]);
            glUniform1f(shadow_uniforms_.base_alpha, material.base_color.a);
            glUniform1f(shadow_uniforms_.alpha_cutoff, material.alpha_cutoff);
            glUniform1i(shadow_uniforms_.has_base_texture, material.base_texture.has_value());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,
                material.base_texture ? (*resource)->textures[*material.base_texture] : 0);
            glFrontFace(glm::determinant(glm::mat3(model)) < 0.0f ? GL_CW : GL_CCW);
            if (material.double_sided) glDisable(GL_CULL_FACE);
            else glEnable(GL_CULL_FACE);
            glBindVertexArray(geometry.vao);
            glDrawElements(GL_TRIANGLES, geometry.count, GL_UNSIGNED_INT, nullptr);
        }
    }
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::RenderSky(const RenderFrame& frame)
{
    glUseProgram(sky_program_);
    glUniformMatrix4fv(sky_uniforms_.view_projection, 1, GL_FALSE, &frame.sky_view_projection[0][0]);
    glUniform3fv(sky_uniforms_.horizon_color, 1, &frame.horizon_color[0]);
    glUniform3fv(sky_uniforms_.zenith_color, 1, &frame.zenith_color[0]);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(sky_vao_);
    glDrawElements(GL_TRIANGLES, sky_index_count_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void Renderer::Render(const RenderFrame& frame)
{
    assert(std::this_thread::get_id() != owner_thread_);
    CollectUnusedMeshes();
    if (frame.framebuffer_width == 0 || frame.framebuffer_height == 0)
        return;

    if (frame.shadows_enabled)
        RenderShadowMap(frame);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_);
    const auto area = frame.viewport.value_or(RenderViewport{
        0, 0, frame.framebuffer_width, frame.framebuffer_height });
    glViewport(area.x, area.y, area.width, area.height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (frame.sky_enabled)
        RenderSky(frame);
    glUseProgram(program_);
    glUniformMatrix4fv(uniforms_.view_projection, 1, GL_FALSE, &frame.view_projection[0][0]);
    glUniform4fv(uniforms_.camera, 1, &frame.camera[0]);
    glUniform3fv(uniforms_.direction_to_light, 1, &frame.direction_to_light[0]);
    glUniformMatrix4fv(uniforms_.light_view_projection, 1, GL_FALSE, &frame.light_view_projection[0][0]);
    glUniform1i(uniforms_.shadows_enabled, frame.shadows_enabled);
    glUniform3fv(uniforms_.sky_ambient, 1, &frame.sky_ambient[0]);
    glUniform3fv(uniforms_.ground_ambient, 1, &frame.ground_ambient[0]);
    for (const auto& draw : frame.meshes)
    {
        const auto resource = Upload(draw.data);
        if (!resource)
        {
#ifdef _DEBUG
            const auto failed = meshes_.find(draw.data.get());
            if (failed != meshes_.end() && !failed->second.error_reported)
            {
                LOG(Error, "{}", resource.error());
                failed->second.error_reported = true;
            }
#endif
            continue;
        }
        for (const auto& instance : draw.data->instances)
        {
            const auto model = draw.world * instance.transform;
            const auto linear = glm::mat3(model);
            const float determinant = glm::determinant(linear);
            const auto& geometry = (*resource)->geometries[instance.geometry];
            const auto& material = draw.data->geometries[instance.geometry].material;
            const bool has_normals = geometry.normals != 0 && determinant != 0.0f && std::isfinite(determinant);
            const auto normal = has_normals ? glm::transpose(glm::inverse(linear)) : glm::mat3(1.0f);
            const auto color = material.base_color * glm::vec4(draw.color, 1.0f);
            glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, &model[0][0]);
            glUniformMatrix3fv(uniforms_.normal_matrix, 1, GL_FALSE, &normal[0][0]);
            glUniform4fv(uniforms_.base_color, 1, &color[0]);
            glUniform1f(uniforms_.metallic, material.metallic);
            glUniform1f(uniforms_.roughness, material.roughness);
            glUniform1f(uniforms_.alpha_cutoff, material.alpha_cutoff);
            glUniform1i(uniforms_.has_normals, has_normals);
            glUniform1i(uniforms_.has_base_texture, material.base_texture.has_value());
            glUniform1i(uniforms_.has_metallic_roughness_texture, material.metallic_roughness_texture.has_value());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, material.base_texture ? (*resource)->textures[*material.base_texture] : 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D,
                material.metallic_roughness_texture ? (*resource)->textures[*material.metallic_roughness_texture] : 0);
            glFrontFace(determinant < 0.0f ? GL_CW : GL_CCW);
            if (material.double_sided) glDisable(GL_CULL_FACE);
            else glEnable(GL_CULL_FACE);
            glBindVertexArray(geometry.vao);
            glDrawElements(GL_TRIANGLES, geometry.count, GL_UNSIGNED_INT, nullptr);
        }
    }
    glBindVertexArray(0);
    if (frame.draw_overlay)
        frame.draw_overlay();
    application_->Present();
}
