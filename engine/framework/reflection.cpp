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
    registration::class_<MeshComponent>("MeshComponent")
        .property("color", &MeshComponent::GetColor, &MeshComponent::SetColor);
}
