#include "framework/reflection.hpp"
#include <cstdio>
#include <cstdlib>
import std;

import GameFramework;

namespace
{
class TestActor : public Actor
{
public:
    using Actor::Actor;
    int health = 100;
    RTTR_ENABLE(Actor)
};

void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
}

RTTR_REGISTRATION
{
    rttr::registration::class_<TestActor>("TestActor")
        .property("health", &TestActor::health);
}

void RunReflectionTests()
{
    Scene scene;
    auto* actor = scene.SpawnActor<TestActor>();
    auto* parent = actor->AddComponent<ActorComponent>();
    auto* mesh = parent->AddComponent<MeshComponent>();
    GameObject& object = *actor;
    const auto actor_type = rttr::instance(object).get_derived_type();
    Check(actor_type.get_name() == "TestActor", "Derived actor reflection failed");
    Check(actor_type.is_derived_from<GameObject>() && actor_type.is_derived_from<Actor>(), "Actor ancestry missing");
    Check(actor_type.get_property("health").set_value(object, 42) && actor->health == 42, "Derived property edit failed");
    Check(actor_type.get_properties().size() == 2, "Inherited property missing or runtime state exposed");

    scene.UpdateWorldTransforms();
    Transform transform;
    transform.position.x = 3.0f;
    auto property = actor_type.get_property("local_transform");
    Check(property.set_value(object, transform), "Actor transform property edit failed");
    Check(mesh->GetWorldMatrix()[3].x == 3.0f, "Actor transform edit bypassed invalidation");
    auto value = property.get_value(object);
    Check(value.is_type<Transform>() && value.get_value<Transform>().position.x == 3.0f, "Transform read failed");

    GameObject& component = *parent;
    auto component_type = rttr::instance(component).get_derived_type();
    transform.position.x = 2.0f;
    Check(component_type.get_property("local_transform").set_value(component, transform), "Component transform edit failed");
    Check(mesh->GetWorldMatrix()[3].x == 5.0f, "Component transform edit bypassed invalidation");

    GameObject& mesh_object = *mesh;
    auto mesh_type = rttr::instance(mesh_object).get_derived_type();
    Check(mesh_type.get_name() == "MeshComponent" && mesh_type.is_derived_from<ActorComponent>(), "Mesh type or ancestry missing");
    Check(mesh_type.get_properties().size() == 2, "Mesh properties incorrect");
    auto color = mesh_type.get_property("color");
    Check(color.set_value(mesh_object, glm::vec3{0.25f, 0.5f, 1.0f}), "Mesh color edit failed");
    Check(mesh->GetColor() == glm::vec3(0.25f, 0.5f, 1.0f), "Mesh color not applied");
    Check(!color.set_value(mesh_object, std::string("invalid")), "Wrong property type accepted");
    Check(mesh->GetColor() == glm::vec3(0.25f, 0.5f, 1.0f), "Rejected edit modified mesh");
    Check(!mesh_type.get_property("mesh_").is_valid() && !mesh_type.get_property("world_").is_valid(), "Runtime state exposed");
    Check(!rttr::type::get<Actor>().create().is_valid(), "Reflection bypassed scene ownership");

    auto transform_type = rttr::type::get_by_name("Transform");
    Check(transform_type == rttr::type::get<Transform>() && transform_type.get_properties().size() == 3, "Transform registration missing");
    auto position = transform_type.get_property("position").get_value(transform);
    Check(position.get_type().get_property("y").set_value(position, 7.0f), "Nested vector edit failed");
    Check(transform_type.get_property("position").set_value(transform, position) && transform.position.y == 7.0f, "Nested property writeback failed");
    Check(rttr::type::get<glm::quat>().get_properties().size() == 4, "Quaternion registration missing");
}
