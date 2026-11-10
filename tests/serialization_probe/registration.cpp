module;
#include <rttr/registration>
module SerializationProbe;

RTTR_REGISTRATION
{
    using namespace rttr;
    using namespace probe;
    registration::class_<glm::vec3>("Vec3")
        .property("x", &glm::vec3::x).property("y", &glm::vec3::y).property("z", &glm::vec3::z);
    registration::class_<glm::quat>("Quaternion")
        .property("w", &glm::quat::w).property("x", &glm::quat::x)
        .property("y", &glm::quat::y).property("z", &glm::quat::z);
    registration::class_<Transform>("Transform")
        .property("position", &Transform::position).property("rotation", &Transform::rotation)
        .property("scale", &Transform::scale);
    registration::class_<Stats>("Stats")
        .property("health", &Stats::health)(metadata("editor_label", "Health"))
        .property("scores", &Stats::scores);
    registration::class_<Object>("Object").property("name", &Object::name);
    registration::class_<Actor>("Actor")
        .property("transform", &Actor::GetTransform, &Actor::SetTransform);
    registration::class_<Pawn>("Pawn")
        .constructor<Scene&>()(policy::ctor::as_raw_ptr)
        .property("stats", &Pawn::stats).property("history", &Pawn::history)
        .property("target", &Pawn::target).property("enabled", &Pawn::enabled)
        .property("speed", &Pawn::speed);
    registration::class_<Component>("Component")
        .constructor<Actor&, Component*>()(policy::ctor::as_raw_ptr)
        .property("asset", &Component::asset);
}
