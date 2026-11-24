// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Renderer;

import <GL/glew.h>;
import "core/log.hpp";

std::expected<void, const char*> Renderer::InitializeUiRendering(unsigned program)
{
    ui_program_ = program;
    ui_uniforms_ = { glGetUniformLocation(program, "uWindow"), glGetUniformLocation(program, "uRect"),
        glGetUniformLocation(program, "uColor"), glGetUniformLocation(program, "uHasTexture") };
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uTexture"), 0);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &ui_texture_limit_);
    glGenVertexArrays(1, &ui_vao_);
    if (!ui_vao_ || glGetError() != GL_NO_ERROR)
        return std::unexpected("Cannot initialize OpenGL UI rendering.");
    return {};
}

void Renderer::ReleaseUiRendering()
{
    for (const auto& entry : ui_textures_)
        if (entry.second.texture) glDeleteTextures(1, &entry.second.texture);
    ui_textures_.clear();
    if (ui_vao_) glDeleteVertexArrays(1, &ui_vao_);
    if (ui_program_) glDeleteProgram(ui_program_);
    ui_vao_ = ui_program_ = 0;
}

unsigned Renderer::UploadUiTexture(const UiTextureHandle& owner)
{
    const auto [entry, inserted] = ui_textures_.try_emplace(owner.get(), UiGpuTexture{ owner });
    auto& texture = entry->second.texture;
    if (!inserted)
        return texture;

    const auto width = owner->PixelWidth();
    const auto height = owner->PixelHeight();
    const auto limit = static_cast<unsigned>(ui_texture_limit_);
    if (width && height && width <= limit && height <= limit &&
        owner->Pixels().size() == std::size_t{width} * height * 4)
    {
        while (glGetError() != GL_NO_ERROR) {}
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(width), static_cast<int>(height),
            0, GL_BGRA, GL_UNSIGNED_BYTE, owner->Pixels().data());
        if (glGetError() != GL_NO_ERROR)
        {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
    }
#ifdef _DEBUG
    if (!texture) LOG(Error, "{}", "Cannot upload UI texture.");
#endif
    return texture;
}

void Renderer::RenderUi(const RenderFrame& frame)
{
    std::erase_if(ui_textures_, [](const auto& entry) {
        if (!entry.second.owner.expired()) return false;
        if (entry.second.texture) glDeleteTextures(1, &entry.second.texture);
        return true;
    });

    const auto& ui = frame.ui;
    if (ui.quads.empty() || ui.window.width <= 0 || ui.window.height <= 0)
        return;

    glViewport(0, 0, frame.framebuffer_width, frame.framebuffer_height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(ui_program_);
    glBindVertexArray(ui_vao_);
    glUniform2f(ui_uniforms_.window, ui.window.width, ui.window.height);
    for (const auto& quad : ui.quads)
    {
        const auto texture = quad.texture ? UploadUiTexture(quad.texture) : 0u;
        if (quad.texture && !texture) continue;
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(ui_uniforms_.has_texture, texture != 0);
        glUniform4f(ui_uniforms_.rect, quad.rect.x, quad.rect.y, quad.rect.width, quad.rect.height);
        glUniform4fv(ui_uniforms_.color, 1, quad.color.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
