// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module;
#include "framework/reflection.hpp"
module GameFramework;

import <cassert>;
import <glm/gtc/matrix_transform.hpp>;

namespace
{
glm::quat NormalizeRotation(const glm::quat& rotation)
{
    const float length_squared = glm::dot(rotation, rotation);
    assert(std::isfinite(length_squared) && length_squared > 0.0f);
    return glm::normalize(rotation);
}
}

CameraComponent::~CameraComponent()
{
    if (auto* actor = dynamic_cast<CameraActor*>(&GetActor()); actor && actor->camera_ == this)
        actor->camera_ = nullptr;
}

glm::mat4 CameraComponent::GetViewMatrix() const
{
    auto rotation = NormalizeRotation(GetLocalTransform().rotation);
    for (auto* parent = GetParent(); parent; parent = parent->GetParent())
        rotation = NormalizeRotation(parent->GetLocalTransform().rotation) * rotation;
    rotation = NormalizeRotation(GetActor().GetLocalTransform().rotation) * rotation;

    // Scale affects the inherited position, but never the camera's orientation.
    const glm::vec3 position{ GetWorldMatrix()[3] };
    return glm::mat4_cast(glm::conjugate(NormalizeRotation(rotation))) *
        glm::translate(glm::mat4{ 1.0f }, -position);
}

glm::mat4 CameraComponent::GetProjectionMatrix(float aspect_ratio) const
{
    assert(std::isfinite(aspect_ratio) && aspect_ratio > 0.0f);
    if (projection_mode_ == ProjectionMode::Perspective)
        return glm::perspectiveRH_NO(fov_y_, aspect_ratio, near_plane_, far_plane_);

    const float half_height = orthographic_size_ * 0.5f;
    const float half_width = half_height * aspect_ratio;
    return glm::orthoRH_NO(-half_width, half_width, -half_height, half_height,
        near_plane_, far_plane_);
}

glm::mat4 CameraComponent::GetViewProjectionMatrix(float aspect_ratio) const
{
    return GetProjectionMatrix(aspect_ratio) * GetViewMatrix();
}

std::expected<void, const char*> CameraComponent::SetPerspective(
    float fov_y_radians, float near_plane, float far_plane)
{
    if (!std::isfinite(fov_y_radians) || fov_y_radians <= 0.0f ||
        fov_y_radians >= std::numbers::pi_v<float> ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) ||
        near_plane <= 0.0f || far_plane <= near_plane)
        return std::unexpected("Invalid perspective projection parameters.");

    projection_mode_ = ProjectionMode::Perspective;
    fov_y_ = fov_y_radians;
    near_plane_ = near_plane;
    far_plane_ = far_plane;
    return {};
}

std::expected<void, const char*> CameraComponent::SetOrthographic(
    float vertical_size, float near_plane, float far_plane)
{
    if (!std::isfinite(vertical_size) || vertical_size <= 0.0f ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) ||
        near_plane <= 0.0f || far_plane <= near_plane)
        return std::unexpected("Invalid orthographic projection parameters.");

    projection_mode_ = ProjectionMode::Orthographic;
    orthographic_size_ = vertical_size;
    near_plane_ = near_plane;
    far_plane_ = far_plane;
    return {};
}

CameraActor::CameraActor(Scene& scene) : Actor(scene), camera_(AddComponent<CameraComponent>())
{
}

CameraComponent& CameraActor::GetCameraComponent()
{
    assert(!IsPendingDestroy());
    if (!camera_ || camera_->IsPendingDestroy())
        camera_ = AddComponent<CameraComponent>();
    return *camera_;
}

std::expected<void, const char*> Scene::SetActiveCamera(CameraComponent* camera)
{
    if (camera && (&camera->GetActor().GetScene() != this ||
        camera->IsPendingDestroy() || camera->GetActor().IsPendingDestroy()))
        return std::unexpected("Camera must belong to this scene and be alive.");
    active_camera_ = camera;
    return {};
}

CameraActor& Scene::GetDefaultCameraActor()
{
    if (!default_camera_actor_)
        default_camera_actor_ = SpawnActor<CameraActor>();
    return *default_camera_actor_;
}

CameraComponent& Scene::GetActiveCamera()
{
    return active_camera_ ? *active_camera_ : GetDefaultCameraActor().GetCameraComponent();
}
