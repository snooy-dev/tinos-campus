// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
#pragma once

#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace level_json
{
inline constexpr std::size_t MaxBytes = 64 * 1024 * 1024;
inline constexpr unsigned MaxDepth = 64;

struct Error
{
    std::size_t offset{};
    std::string message;
};

struct Pose
{
    std::array<float, 3> position{};
    std::array<float, 4> rotation{ 1, 0, 0, 0 };
    std::array<float, 3> scale{ 1, 1, 1 };
};

struct Record
{
    std::string id;
    std::string type;
    std::optional<std::string> owner;
    std::optional<std::string> parent;
    std::optional<std::string> slot;
    Pose transform;
    glz::raw_json properties{ "{}" };
};

struct SchemaVersion
{
    std::uint32_t value{ 1 };
};

struct Document
{
    SchemaVersion schema_version;
    std::optional<std::string> active_camera;
    std::vector<Record> objects;
};

struct Empty {};

struct Mesh
{
    std::array<float, 3> color{ 1, 1, 1 };
    std::string asset;
};

struct Perspective
{
    std::string mode{ "perspective" };
    float fov_y_radians{};
    float near_plane{};
    float far_plane{};
};

struct Orthographic
{
    std::string mode{ "orthographic" };
    float vertical_size{};
    float near_plane{};
    float far_plane{};
};

struct CameraMode
{
    std::string mode;
};
}

template <> struct glz::meta<level_json::Pose>
{
    using T = level_json::Pose;
    static constexpr auto value = glz::object(
        "position", &T::position, "rotation", &T::rotation, "scale", &T::scale);
};

template <> struct glz::meta<level_json::Record>
{
    using T = level_json::Record;
    static constexpr auto value = glz::object(
        "id", &T::id, "type", &T::type, "owner", &T::owner, "parent", &T::parent,
        "slot", &T::slot, "transform", &T::transform, "properties", &T::properties);
};

template <> struct glz::meta<level_json::SchemaVersion>
{
    static constexpr auto value = &level_json::SchemaVersion::value;
};

template <> struct glz::meta<level_json::Document>
{
    using T = level_json::Document;
    static constexpr auto value = glz::object(
        "schema_version", &T::schema_version, "active_camera", &T::active_camera, "objects", &T::objects);
};

template <> struct glz::meta<level_json::Empty>
{
    static constexpr auto value = glz::object();
};

template <> struct glz::meta<level_json::Mesh>
{
    using T = level_json::Mesh;
    static constexpr auto value = glz::object("color", &T::color, "asset", &T::asset);
};

template <> struct glz::meta<level_json::Perspective>
{
    using T = level_json::Perspective;
    static constexpr auto value = glz::object(
        "mode", &T::mode, "fov_y_radians", &T::fov_y_radians, "near_plane", &T::near_plane, "far_plane", &T::far_plane);
};

template <> struct glz::meta<level_json::Orthographic>
{
    using T = level_json::Orthographic;
    static constexpr auto value = glz::object(
        "mode", &T::mode, "vertical_size", &T::vertical_size, "near_plane", &T::near_plane, "far_plane", &T::far_plane);
};

template <> struct glz::meta<level_json::CameraMode>
{
    static constexpr auto value = glz::object("mode", &level_json::CameraMode::mode);
};

template <> struct glz::from<glz::JSON, level_json::SchemaVersion>
{
    template <auto Opts>
    static void op(level_json::SchemaVersion& value, auto& ctx, auto& it, auto end)
    {
        glz::raw_json_view token;
        glz::from<glz::JSON, glz::raw_json_view>::template op<Opts>(token, ctx, it, end);
        if (ctx.error != glz::error_code::none)
            return;
        const auto* last = token.str.data() + token.str.size();
        const auto result = std::from_chars(token.str.data(), last, value.value);
        if (result.ec != std::errc{} || result.ptr != last)
        {
            ctx.error = glz::error_code::parse_number_failure;
            ctx.custom_error_message = "schema_version must be an unsigned integer";
        }
    }
};

// The range reader accepts short std::arrays. The tuple reader enforces the field arity.
template <std::size_t N> struct glz::from<glz::JSON, std::array<float, N>>
{
    template <auto Opts>
    static void op(std::array<float, N>& value, auto& ctx, auto& it, auto end)
    {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            auto elements = std::tie(value[I]...);
            glz::from<glz::JSON, decltype(elements)>::template op<Opts>(elements, ctx, it, end);
        }(std::make_index_sequence<N>{});
        if (ctx.error == glz::error_code::none)
            for (float element : value)
                if (!std::isfinite(element))
                    ctx.error = glz::error_code::parse_number_failure;
    }
};

namespace level_json
{
struct ReadOptions : glz::opts
{
    bool validate_trailing_whitespace = true;
    bool error_on_missing_array_elements = true;
    bool validate_skipped = true;
};

inline constexpr ReadOptions StrictRead{
    glz::opts{ .null_terminated = false, .skip_null_members = false, .error_on_missing_keys = true }
};

struct WriteOptions : glz::opts
{
    bool escape_control_characters = true;
};

template <class T>
void ForEachProperty(T& value, auto&& visitor)
{
    using Meta = glz::reflect<std::remove_cv_t<T>>;
    glz::for_each<Meta::size>([&]<std::size_t I> {
        visitor(Meta::keys[I], glz::get_member(value, glz::get<I>(Meta::values)));
    });
}

class StructureScan
{
public:
    explicit StructureScan(std::string_view input) : input_(input) {}

    std::expected<void, Error> Check()
    {
        if (input_.size() > MaxBytes)
            return std::unexpected(Error{ MaxBytes, "level exceeds 64 MiB" });
        auto result = Value(0);
        if (!result)
            return result;
        SkipWhitespace();
        if (offset_ != input_.size())
            return Fail("trailing data");
        return {};
    }

    std::string_view Input() const { return normalized_.empty() ? input_ : std::string_view(normalized_); }

private:
    char Peek() const { return offset_ < input_.size() ? input_[offset_] : '\0'; }

    static bool Whitespace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

    void SkipWhitespace()
    {
        while (Whitespace(Peek()))
            ++offset_;
    }

    bool Consume(char expected)
    {
        if (Peek() != expected)
            return false;
        ++offset_;
        return true;
    }

    std::unexpected<Error> Fail(std::string message) const
    {
        return std::unexpected(Error{ offset_, std::move(message) });
    }

    std::expected<std::string, Error> String()
    {
        const auto start = offset_;
        if (!Consume('"'))
            return Fail("expected a quoted key");
        while (offset_ < input_.size())
        {
            const char c = input_[offset_++];
            if (c == '\\' && offset_ < input_.size())
                ++offset_;
            if (c != '"')
                continue;
            std::string value;
            const auto error = glz::read<StrictRead>(value, input_.substr(start, offset_ - start));
            if (error)
                return std::unexpected(Error{ start + error.count, glz::format_error(error) });
            return value;
        }
        return Fail("unterminated string");
    }

    std::expected<void, Error> Value(unsigned depth)
    {
        SkipWhitespace();
        const char first = Peek();
        if (first == '"')
        {
            const auto value = String();
            if (!value)
                return std::unexpected(value.error());
            return {};
        }
        if (first != '{' && first != '[')
        {
            const auto start = offset_;
            while (offset_ < input_.size() && !Whitespace(Peek()) && Peek() != ',' && Peek() != '}' && Peek() != ']')
                ++offset_;
            if (start == offset_)
                return Fail("expected a JSON value");
            glz::skip token;
            const auto error = glz::read<StrictRead>(token, input_.substr(start, offset_ - start));
            if (error)
                return std::unexpected(Error{ start + error.count, glz::format_error(error) });
            return {};
        }
        if (depth == MaxDepth)
            return Fail("JSON nesting exceeds 64 levels");

        ++offset_;
        const bool object = first == '{';
        const char close = object ? '}' : ']';
        SkipWhitespace();
        if (Consume(close))
            return {};
        std::unordered_set<std::string> keys;
        for (;;)
        {
            if (object)
            {
                const auto key_offset = offset_;
                auto key = String();
                if (!key)
                    return std::unexpected(std::move(key.error()));
                if (!keys.insert(*key).second)
                    return std::unexpected(Error{ key_offset, "duplicate JSON key: " + *key });
                const auto token_length = offset_ - key_offset;
                if (input_.substr(key_offset, token_length).contains('\\'))
                {
                    // Glaze's typed member lookup uses raw key bytes. Preserve byte offsets for diagnostics.
                    const auto encoded = glz::write<WriteOptions{}>(*key);
                    if (!encoded || encoded->size() > token_length)
                        return Fail("could not normalize escaped JSON key");
                    if (normalized_.empty())
                        normalized_ = input_;
                    normalized_.replace(key_offset, encoded->size(), *encoded);
                    normalized_.replace(key_offset + encoded->size(), token_length - encoded->size(),
                        token_length - encoded->size(), ' ');
                }
                SkipWhitespace();
                if (!Consume(':'))
                    return Fail("expected ':' after key");
            }
            auto result = Value(depth + 1);
            if (!result)
                return result;
            SkipWhitespace();
            if (Consume(close))
                return {};
            if (!Consume(','))
                return Fail("expected ',' or end of collection");
            SkipWhitespace();
        }
    }

    std::string_view input_;
    std::string normalized_;
    std::size_t offset_{};
};

template <class T>
std::expected<T, Error> Read(std::string_view input)
{
    StructureScan scan(input);
    if (auto checked = scan.Check(); !checked)
        return std::unexpected(std::move(checked.error()));
    T value;
    const auto error = glz::read<StrictRead>(value, scan.Input());
    if (error)
        return std::unexpected(Error{ error.count, glz::format_error(error) });
    std::optional<Error> invalid;
    ForEachProperty(value, [&](std::string_view name, const auto& field) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(field)>, float>)
            if (!std::isfinite(field) && !invalid)
                invalid = Error{ 0, "non-finite " + std::string(name) };
    });
    if (invalid)
        return std::unexpected(std::move(*invalid));
    return value;
}

inline std::expected<std::string, Error> ReadCameraMode(std::string_view input)
{
    StructureScan scan(input);
    if (auto checked = scan.Check(); !checked)
        return std::unexpected(std::move(checked.error()));
    CameraMode value;
    constexpr auto mode_options = [] {
        auto result = StrictRead;
        result.error_on_unknown_keys = false;
        return result;
    }();
    const auto error = glz::read<mode_options>(value, scan.Input());
    if (error)
        return std::unexpected(Error{ error.count, glz::format_error(error) });
    return value.mode;
}

inline std::expected<Document, Error> ReadDocument(std::string_view input)
{
    auto document = Read<Document>(input);
    if (document && document->schema_version.value != 1)
        return std::unexpected(Error{ 0,
            "unsupported schema_version: " + std::to_string(document->schema_version.value) });
    return document;
}

template <class T>
std::expected<std::string, Error> Write(const T& value)
{
    constexpr WriteOptions options{
        glz::opts{ .skip_null_members = false, .prettify = std::is_same_v<T, Document> }
    };
    auto output = glz::write<options>(value);
    if (!output)
        return std::unexpected(Error{ output.error().count, glz::format_error(output.error()) });
    return std::move(*output);
}
}
