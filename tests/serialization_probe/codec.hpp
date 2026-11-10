#pragma once
#include <nlohmann/json.hpp>
#include <expected>
#include <memory>
#include <string>
import SerializationProbe;

namespace probe
{
using Json = nlohmann::json;
std::expected<Json, std::string> Capture(const Scene& scene);
std::expected<std::unique_ptr<Scene>, std::string> Restore(const Json& data);
}
