// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
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

glm::dmat4 Projection(ProjectionMode mode, double size, double near_plane, double far_plane,
    double aspect)
{
    if (mode == ProjectionMode::Perspective)
        return glm::perspectiveRH_NO(size, aspect, near_plane, far_plane);
    const double half_height = size * 0.5;
    const double half_width = half_height * aspect;
    return glm::orthoRH_NO(-half_width, half_width, -half_height, half_height, near_plane, far_plane);
}

bool FitsProjection(ProjectionMode mode, float size, float near_plane, float far_plane)
{
    // Framebuffer dimensions are positive ints. Only the horizontal coefficient depends on aspect.
    constexpr double largest_aspect = std::numeric_limits<int>::max();
    for (const double aspect : { 1.0 / largest_aspect, largest_aspect })
    {
        const auto matrix = Projection(mode, size, near_plane, far_plane, aspect);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (!std::isfinite(matrix[c][r]) ||
                    std::abs(matrix[c][r]) > std::numeric_limits<float>::max())
                    return false;
        if (static_cast<float>(matrix[0][0]) == 0.0f ||
            static_cast<float>(matrix[1][1]) == 0.0f)
            return false;
    }
    return true;
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
    const auto size = projection_mode_ == ProjectionMode::Perspective ? fov_y_ : orthographic_size_;
    return glm::mat4(Projection(projection_mode_, size, near_plane_, far_plane_, aspect_ratio));
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
        near_plane <= 0.0f || far_plane <= near_plane ||
        !FitsProjection(ProjectionMode::Perspective, fov_y_radians, near_plane, far_plane))
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
        near_plane <= 0.0f || far_plane <= near_plane ||
        !FitsProjection(ProjectionMode::Orthographic, vertical_size, near_plane, far_plane))
        return std::unexpected("Invalid orthographic projection parameters.");

    projection_mode_ = ProjectionMode::Orthographic;
    orthographic_size_ = vertical_size;
    near_plane_ = near_plane;
    far_plane_ = far_plane;
    return {};
}

void CameraArmComponent::SetOrbit(float yaw, float pitch)
{
    auto transform = GetLocalTransform();
    transform.rotation = glm::angleAxis(yaw, glm::vec3{ 0.0f, 1.0f, 0.0f }) *
        glm::angleAxis(pitch, glm::vec3{ 1.0f, 0.0f, 0.0f });
    SetLocalTransform(transform);
}

CameraComponent* CameraArmComponent::AttachCamera(float length)
{
    assert(std::isfinite(length) && length > 0.0f);
    auto* camera = AddComponent<CameraComponent>();
    if (camera)
    {
        Transform offset;
        offset.position.z = length;
        camera->SetLocalTransform(offset);
    }
    return camera;
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
