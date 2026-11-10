#include <rttr/type>
#include <rttr/property.h>
#include <rttr/instance.h>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include "codec.hpp"

using namespace probe;
void Check(bool ok, const char* message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

void Run()
{
    Scene source;
    auto first = std::make_unique<Pawn>(source);
    auto second = std::make_unique<Pawn>(source);
    auto* a = first.get();
    auto* b = second.get();
    a->name = "first";
    b->name = "second";
    a->stats = {75, {1, 4, 9}};
    a->history = {{20, {2}}, {30, {3, 5}}};
    a->target = b;
    b->target = a;
    a->runtime_only = 999;
    a->enabled = false;
    a->speed = 3.25;
    Transform transform;
    transform.position = {1, 2, 3};
    transform.scale = {-1, 2, 0.5};
    transform.rotation = glm::quat(0.5f, 0.5f, 0.5f, 0.5f);
    a->SetTransform(transform);
    auto component = std::make_unique<Component>(*a, nullptr);
    component->name = "root";
    component->asset = "meshes/root.glb";
    auto child = std::make_unique<Component>(*a, component.get());
    child->name = "child";
    child->asset = "meshes/child.glb";
    component->children.push_back(std::move(child));
    a->components.push_back(std::move(component));
    source.actors.push_back(std::move(first));
    source.actors.push_back(std::move(second));

    Object& base = *a;
    auto type = rttr::instance(base).get_derived_type();
    Check(type.get_name() == "Pawn", "Dynamic type registration missing");
    Check(type.get_property("name").set_value(base, std::string("edited")), "Inherited property edit failed");
    Check(a->name == "edited", "Editor edit did not reach instance");
    Check(rttr::type::get<Stats>().get_property("health").get_metadata("editor_label").to_string() == "Health",
        "Editor metadata missing");
    Check(!type.get_property("runtime_only").is_valid(), "Runtime field registered");
    auto stats = type.get_property("stats").get_value(base);
    Check(stats.get_type().get_property("health").set_value(stats, 80), "Nested property edit failed");
    Check(type.get_property("stats").set_value(base, stats) && a->stats.health == 80,
        "Nested property writeback failed");
    auto saved = Capture(source);
    Check(saved.has_value(), saved ? "" : saved.error().c_str());
    auto restored = Restore(Json::parse(saved->dump()));
    Check(restored.has_value(), restored ? "" : restored.error().c_str());
    auto captured_again = Capture(**restored);
    Check(captured_again && *captured_again == *saved, "Full saved state changed after roundtrip");
    Pawn* copy = nullptr;
    for (const auto& actor : (*restored)->actors)
        if (actor->name == "edited") copy = dynamic_cast<Pawn*>(actor.get());
    Check(copy && &copy->scene == restored->get(), "Scene ownership broken");
    Check(copy->stats.health == 80 && copy->stats.scores == a->stats.scores, "Nested struct roundtrip failed");
    Check(copy->history.size() == 2 && copy->history[1].scores == a->history[1].scores, "Nested vector roundtrip failed");
    Check(copy->target && dynamic_cast<Pawn*>(copy->target)->target == copy, "Mutual reference fixup failed");
    Check(copy->runtime_only == 17 && !copy->enabled && copy->speed == 3.25, "Scalar or excluded field failed");
    Check(copy->GetTransform().position == transform.position && copy->GetTransform().scale == transform.scale &&
        copy->GetTransform().rotation == transform.rotation && copy->invalidations == 1, "Transform setter roundtrip failed");
    auto* root = copy->components.at(0).get();
    Check(&root->actor == copy && !root->parent && root->children.at(0)->parent == root &&
        root->children[0]->asset == "meshes/child.glb", "Component ownership or asset path failed");

    // All invalid cases must leave the existing scene intact and destroy staging objects.
    auto reject = [&](Json bad, const char* expected) {
        const int live = Object::live;
        std::ostringstream errors;
        std::ostringstream output;
        auto* previous = std::cerr.rdbuf(errors.rdbuf());
        auto* previous_output = std::cout.rdbuf(output.rdbuf());
        auto result = Restore(bad);
        std::cerr.rdbuf(previous);
        std::cout.rdbuf(previous_output);
        Check(!result, "Invalid data accepted");
        Check(result.error().find(expected) != std::string::npos, result.error().c_str());
        Check(errors.str().empty() && output.str().empty(), "Recoverable failure produced output");
        Check(Object::live == live, "Partial objects leaked");
        Check(a->name == "edited" && a->target == b && b->target == a, "Existing scene modified");
    };
    Json bad = *saved;
    bad["objects"][0]["type"] = "Unknown";
    reject(bad, "unknown object type");
    bad = *saved; bad["objects"].push_back(bad["objects"][0]); reject(bad, "duplicate ID");
    bad = *saved; bad["objects"][0]["properties"]["stats"]["health"] = "bad"; reject(bad, "expected int");
    bad = *saved; bad["objects"][0]["properties"]["target"] = {{"ref", 999}}; reject(bad, "missing reference");
    bad = *saved; bad["objects"][0]["properties"]["unknown"] = 1; reject(bad, "unknown property");
    bad = *saved; bad["objects"][0]["properties"]["stats"]["health"] = 2147483648LL; reject(bad, "expected int");
    bad = *saved; bad["objects"][1]["parent"] = 3; reject(bad, "cyclic or missing parent");
    bad = *saved; bad["objects"][1]["owner"] = 999; reject(bad, "missing owning actor");
    bad = *saved; bad.erase("objects"); reject(bad, "objects");
    bad = *saved; bad["objects"][0]["properties"]["target"] = nullptr;
    auto with_null = Restore(bad);
    Check(with_null.has_value(), "Null reference rejected");
    for (const auto& actor : (*with_null)->actors)
        if (actor->name == "edited") Check(dynamic_cast<Pawn*>(actor.get())->target == nullptr, "Null reference changed");
    Pawn outside(source);
    a->target = &outside;
    Check(!Capture(source), "External scene reference accepted");
    a->target = b;
}

int main()
{
    Run();
    Check(Object::live == 0, "Objects leaked after roundtrip");
    std::cout << "PASS: module registration, editor metadata, inherited edits, nested structs/arrays, Transform, "
        "ownership, cyclic/null references, validation, rollback and silent failures.\n";
}
