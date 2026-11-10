#include <rttr/type>
#include <rttr/property.h>
#include <rttr/instance.h>
#include <rttr/variant_sequential_view.h>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include "codec.hpp"

namespace probe
{
namespace
{
using Ids = std::unordered_map<const Object*, int>;
using Objects = std::unordered_map<int, Object*>;
template<class T> using Result = std::expected<T, std::string>;

Result<Json> WriteValue(rttr::variant value, const Ids& ids);
Result<void> ReadValue(const Json& data, rttr::variant& value, const Objects& objects);

Result<Json> WriteProperties(rttr::instance object, const Ids& ids)
{
    Json result = Json::object();
    for (auto property : object.get_derived_type().get_properties())
    {
        const auto name = property.get_name().to_string();
        auto value = WriteValue(property.get_value(object), ids);
        if (!value) return std::unexpected(name + ": " + value.error());
        result[name] = std::move(*value);
    }
    return result;
}

Result<Json> WriteValue(rttr::variant value, const Ids& ids)
{
    if (value.get_type().is_wrapper()) value = value.extract_wrapped_value();
    const auto type = value.get_type();
    if (type == rttr::type::get<int>()) return Json(value.get_value<int>());
    if (type == rttr::type::get<bool>()) return Json(value.get_value<bool>());
    if (type == rttr::type::get<std::string>()) return Json(value.get_value<std::string>());
    if (type == rttr::type::get<float>() || type == rttr::type::get<double>())
    {
        const double number = value.to_double();
        if (!std::isfinite(number)) return std::unexpected("non-finite number");
        return Json(number);
    }
    if (type == rttr::type::get<Object*>())
    {
        const auto* pointer = value.get_value<Object*>();
        if (!pointer) return Json(nullptr);
        const auto found = ids.find(pointer);
        if (found == ids.end()) return std::unexpected("reference outside scene");
        return Json{{"ref", found->second}};
    }
    if (value.is_sequential_container())
    {
        Json result = Json::array();
        auto view = value.create_sequential_view();
        for (std::size_t i = 0; i < view.get_size(); ++i)
        {
            auto item = WriteValue(view.get_value(i), ids);
            if (!item) return std::unexpected(item.error());
            result.push_back(std::move(*item));
        }
        return result;
    }
    if (!type.get_properties().empty()) return WriteProperties(value, ids);
    return std::unexpected("unsupported value type: " + type.get_name().to_string());
}

Result<void> ReadProperties(const Json& data, rttr::instance object, const Objects& objects)
{
    if (!data.is_object()) return std::unexpected("expected object properties");
    for (const auto& [name, input] : data.items())
    {
        auto property = object.get_derived_type().get_property(name);
        if (!property.is_valid()) return std::unexpected("unknown property: " + name);
        auto value = property.get_value(object);
        auto result = ReadValue(input, value, objects);
        if (!result) return std::unexpected(name + ": " + result.error());
        if (!property.set_value(object, value)) return std::unexpected("cannot set property: " + name);
    }
    return {};
}

Result<int> ReadId(const Json& data)
{
    if (!data.is_number_integer() || data < 0 || data > std::numeric_limits<int>::max())
        return std::unexpected("invalid integer or ID");
    return data.get<int>();
}

Result<void> ReadValue(const Json& data, rttr::variant& value, const Objects& objects)
{
    if (value.get_type().is_wrapper()) value = value.extract_wrapped_value();
    const auto type = value.get_type();
    if (type == rttr::type::get<int>())
    {
        if (!data.is_number_integer() || data < std::numeric_limits<int>::min() ||
            data > std::numeric_limits<int>::max()) return std::unexpected("expected int");
        value = data.get<int>();
    }
    else if (type == rttr::type::get<bool>())
    {
        if (!data.is_boolean()) return std::unexpected("expected bool");
        value = data.get<bool>();
    }
    else if (type == rttr::type::get<std::string>())
    {
        if (!data.is_string()) return std::unexpected("expected string");
        value = data.get<std::string>();
    }
    else if (type == rttr::type::get<float>() || type == rttr::type::get<double>())
    {
        if (!data.is_number()) return std::unexpected("expected number");
        const auto number = data.get<double>();
        if (!std::isfinite(number)) return std::unexpected("non-finite number");
        if (type == rttr::type::get<float>())
        {
            if (std::abs(number) > std::numeric_limits<float>::max()) return std::unexpected("float overflow");
            value = static_cast<float>(number);
        }
        else value = number;
    }
    else if (type == rttr::type::get<Object*>())
    {
        if (data.is_null()) { value = static_cast<Object*>(nullptr); return {}; }
        if (!data.is_object() || data.size() != 1 || !data.contains("ref"))
            return std::unexpected("expected object reference");
        auto id = ReadId(data.at("ref"));
        if (!id) return std::unexpected(id.error());
        auto found = objects.find(*id);
        if (found == objects.end()) return std::unexpected("missing reference target");
        value = found->second;
    }
    else if (value.is_sequential_container())
    {
        if (!data.is_array()) return std::unexpected("expected array");
        auto view = value.create_sequential_view();
        if (!view.set_size(data.size())) return std::unexpected("cannot resize array");
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            auto element = view.get_value(i).extract_wrapped_value();
            auto result = ReadValue(data[i], element, objects);
            if (!result) return std::unexpected("[" + std::to_string(i) + "]: " + result.error());
            if (!view.set_value(i, element)) return std::unexpected("cannot set array element");
        }
    }
    else if (!type.get_properties().empty()) return ReadProperties(data, value, objects);
    else return std::unexpected("unsupported value type");
    return {};
}

void EnumerateComponent(Component& component, std::vector<Object*>& ordered)
{
    ordered.push_back(&component);
    for (const auto& child : component.children) EnumerateComponent(*child, ordered);
}
}

Result<Json> Capture(const Scene& scene)
{
    try
    {
        std::vector<Object*> ordered;
        for (const auto& actor : scene.actors)
        {
            ordered.push_back(actor.get());
            for (const auto& child : actor->components) EnumerateComponent(*child, ordered);
        }
        Ids ids;
        for (auto* object : ordered) ids.emplace(object, static_cast<int>(ids.size()) + 1);
        Json records = Json::array();
        for (auto* object : ordered)
        {
            auto properties = WriteProperties(*object, ids);
            if (!properties) return std::unexpected(properties.error());
            Json record{{"id", ids.at(object)}, {"type", object->get_type().get_name().to_string()},
                {"properties", std::move(*properties)}};
            if (auto* component = dynamic_cast<Component*>(object))
            {
                record["owner"] = ids.at(&component->actor);
                record["parent"] = component->parent ? ids.at(component->parent) : 0;
            }
            records.push_back(std::move(record));
        }
        return Json{{"version", 1}, {"objects", std::move(records)}};
    }
    catch (const std::exception& error) { return std::unexpected(error.what()); }
}

Result<std::unique_ptr<Scene>> Restore(const Json& data)
{
    try
    {
        if (data.at("version") != 1 || !data.at("objects").is_array())
            return std::unexpected("invalid level envelope");
        auto scene = std::make_unique<Scene>();
        Objects objects;
        std::map<int, const Json*> pending;
        for (const auto& record : data.at("objects"))
        {
            auto id = ReadId(record.at("id"));
            if (!id || *id == 0) return std::unexpected("invalid object ID");
            if (!pending.emplace(*id, &record).second) return std::unexpected("duplicate ID");
            const auto type = rttr::type::get_by_name(record.at("type").get<std::string>());
            if (type != rttr::type::get<Pawn>() && type != rttr::type::get<Component>())
                return std::unexpected("unknown object type");
            if (!record.at("properties").is_object()) return std::unexpected("invalid properties");
        }
        // Ownership is engine policy; RTTR constructs instances but does not adopt them.
        for (auto it = pending.begin(); it != pending.end(); )
        {
            const auto& record = *it->second;
            if (record.at("type") != "Pawn") { ++it; continue; }
            auto value = rttr::type::get<Pawn>().create({*scene});
            if (!value.is_type<Pawn*>()) return std::unexpected("actor construction failed");
            std::unique_ptr<Actor> actor(value.get_value<Pawn*>());
            objects.emplace(it->first, actor.get());
            scene->actors.push_back(std::move(actor));
            it = pending.erase(it);
        }
        while (!pending.empty())
        {
            const auto before = pending.size();
            for (auto it = pending.begin(); it != pending.end(); )
            {
                const auto& record = *it->second;
                auto owner_id = ReadId(record.at("owner"));
                auto parent_id = ReadId(record.at("parent"));
                if (!owner_id || !parent_id) return std::unexpected("invalid ownership ID");
                auto owner = objects.find(*owner_id);
                auto* actor = owner == objects.end() ? nullptr : dynamic_cast<Actor*>(owner->second);
                if (!actor) return std::unexpected("missing owning actor");
                Component* parent = nullptr;
                if (*parent_id)
                {
                    auto found = objects.find(*parent_id);
                    if (found == objects.end()) { ++it; continue; }
                    parent = dynamic_cast<Component*>(found->second);
                    if (!parent || &parent->actor != actor) return std::unexpected("invalid component parent");
                }
                auto value = rttr::type::get<Component>().create({*actor, parent});
                if (!value.is_type<Component*>()) return std::unexpected("component construction failed");
                std::unique_ptr<Component> component(value.get_value<Component*>());
                objects.emplace(it->first, component.get());
                (parent ? parent->children : actor->components).push_back(std::move(component));
                it = pending.erase(it);
            }
            if (before == pending.size()) return std::unexpected("cyclic or missing parent");
        }
        for (const auto& record : data.at("objects"))
        {
            const int id = record.at("id").get<int>();
            auto result = ReadProperties(record.at("properties"), *objects.at(id), objects);
            if (!result) return std::unexpected("object " + std::to_string(id) + ": " + result.error());
        }
        return scene;
    }
    catch (const std::exception& error) { return std::unexpected(error.what()); }
}
}
