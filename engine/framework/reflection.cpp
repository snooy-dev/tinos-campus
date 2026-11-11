// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module;
#include "framework/reflection.hpp"
module GameFramework;

// Keep RTTR template instantiations out of the exported module interface (MSVC C1116).
rttr::type GameObject::get_type() const { return rttr::type::get<GameObject>(); }
rttr::detail::derived_info GameObject::get_derived_info() { return {this, get_type()}; }
rttr::type Actor::get_type() const { return rttr::type::get<Actor>(); }
rttr::detail::derived_info Actor::get_derived_info() { return {this, get_type()}; }
rttr::type ActorComponent::get_type() const { return rttr::type::get<ActorComponent>(); }
rttr::detail::derived_info ActorComponent::get_derived_info() { return {this, get_type()}; }
rttr::type MeshComponent::get_type() const { return rttr::type::get<MeshComponent>(); }
rttr::detail::derived_info MeshComponent::get_derived_info() { return {this, get_type()}; }

rttr::type CameraComponent::get_type() const { return rttr::type::get<CameraComponent>(); }
rttr::detail::derived_info CameraComponent::get_derived_info() { return {this, get_type()}; }
rttr::type CameraActor::get_type() const { return rttr::type::get<CameraActor>(); }
rttr::detail::derived_info CameraActor::get_derived_info() { return {this, get_type()}; }

RTTR_PLUGIN_REGISTRATION
{
    using namespace rttr;
    registration::class_<glm::vec3>("Vec3")
        .property("x", &glm::vec3::x)
        .property("y", &glm::vec3::y)
        .property("z", &glm::vec3::z);
    registration::class_<glm::quat>("Quaternion")
        .property("w", &glm::quat::w)
        .property("x", &glm::quat::x)
        .property("y", &glm::quat::y)
        .property("z", &glm::quat::z);
    registration::class_<Transform>("Transform")
        .property("position", &Transform::position)
        .property("rotation", &Transform::rotation)
        .property("scale", &Transform::scale);

    // Object construction stays with Scene/Actor so reflection cannot bypass ownership.
    registration::class_<GameObject>("GameObject");
    registration::class_<Actor>("Actor")
        .property("local_transform", &Actor::GetLocalTransform, &Actor::SetLocalTransform);
    registration::class_<ActorComponent>("ActorComponent")
        .property("local_transform", &ActorComponent::GetLocalTransform, &ActorComponent::SetLocalTransform);
    registration::enumeration<ProjectionMode>("ProjectionMode")
        (value("Perspective", ProjectionMode::Perspective), value("Orthographic", ProjectionMode::Orthographic));
    registration::class_<CameraComponent>("CameraComponent")
        .property_readonly("projection_mode", &CameraComponent::GetProjectionMode)
        .property_readonly("field_of_view", &CameraComponent::GetFieldOfView)
        .property_readonly("orthographic_size", &CameraComponent::GetOrthographicSize)
        .property_readonly("near_plane", &CameraComponent::GetNearPlane)
        .property_readonly("far_plane", &CameraComponent::GetFarPlane)
        .method("SetPerspective", &CameraComponent::SetPerspective)
        .method("SetOrthographic", &CameraComponent::SetOrthographic);
    registration::class_<CameraActor>("CameraActor");
    registration::class_<MeshComponent>("MeshComponent")
        .property("color", &MeshComponent::GetColor, &MeshComponent::SetColor);
}
