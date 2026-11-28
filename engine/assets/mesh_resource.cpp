// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#define CGLTF_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable: 4996)
#include <cgltf.h>
#pragma warning(pop)
module MeshResource;

namespace
{
struct ComApartment
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

std::expected<MeshTexture, std::string> DecodeTexture(const cgltf_texture& texture, bool srgb)
{
    using Microsoft::WRL::ComPtr;
    const auto* image = texture.image;
    if (!image || !image->buffer_view || !image->mime_type ||
        (std::string_view(image->mime_type) != "image/png" &&
         std::string_view(image->mime_type) != "image/jpeg"))
        return std::unexpected("GLB textures must be embedded PNG or JPEG images.");
    const auto& view = *image->buffer_view;
    if (!view.size || view.size > std::numeric_limits<DWORD>::max())
        return std::unexpected("Invalid GLB image size.");

    const ComApartment com;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> rgba;
    if ((FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(view.buffer->data) + view.offset,
            static_cast<DWORD>(view.size))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
            decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
        FAILED(factory->CreateFormatConverter(rgba.GetAddressOf())) ||
        FAILED(rgba->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
            nullptr, 0, WICBitmapPaletteTypeCustom)))
        return std::unexpected("Cannot decode embedded GLB texture.");

    UINT width = 0, height = 0;
    if (FAILED(rgba->GetSize(&width, &height)) || !width || !height || width > 8192 || height > 8192)
        return std::unexpected("GLB texture dimensions must be within 1–8192 pixels.");
    MeshTexture result;
    result.width = static_cast<int>(width);
    result.height = static_cast<int>(height);
    result.srgb = srgb;
    if (const auto* sampler = texture.sampler)
    {
        if (sampler->min_filter) result.min_filter = sampler->min_filter;
        if (sampler->mag_filter) result.mag_filter = sampler->mag_filter;
        result.wrap_s = sampler->wrap_s;
        result.wrap_t = sampler->wrap_t;
    }
    result.rgba.resize(width * height * 4);
    if (FAILED(rgba->CopyPixels(nullptr, width * 4,
        static_cast<UINT>(result.rgba.size()), result.rgba.data())))
        return std::unexpected("Cannot read GLB texture pixels.");
    return result;
}

bool ReadAttribute(const cgltf_accessor& accessor, cgltf_type type, std::size_t count,
    std::size_t components, std::vector<float>& values)
{
    if (accessor.type != type || accessor.count != count ||
        count > values.max_size() / components)
        return false;
    values.resize(count * components);
    return cgltf_accessor_unpack_floats(&accessor, values.data(), values.size()) == values.size() &&
        std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
}
}

std::expected<std::vector<std::byte>, std::string> MeshLoading::Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return std::unexpected("Cannot open mesh file.");
    const auto length = input.tellg();
    if (length <= 0 || static_cast<std::uintmax_t>(length) > std::numeric_limits<std::size_t>::max())
        return std::unexpected("Invalid mesh file size.");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::unexpected("Cannot read mesh file.");
    return bytes;
}

std::expected<std::shared_ptr<const MeshData>, std::string> MeshLoading::Parse(std::span<const std::byte> bytes)
{
    cgltf_options options{};
    cgltf_data* parsed = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &parsed) != cgltf_result_success)
        return std::unexpected("Invalid glTF file.");
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> source(parsed, cgltf_free);
    if (source->file_type != cgltf_file_type_glb || source->buffers_count != 1 ||
        source->buffers[0].uri || !source->bin || source->buffers[0].size > source->bin_size)
        return std::unexpected("Only self-contained GLB geometry is supported.");
    // shortcut: one clip per asset, add clip selection when characters need more than one action.
    if (source->extensions_required_count || source->animations_count > 1 ||
        (source->animations_count && !source->skins_count))
        return std::unexpected("Only one skeletal animation and no required GLB extensions are supported.");
    for (std::size_t i = 0; i < source->meshes_count; ++i)
        for (std::size_t j = 0; j < source->meshes[i].primitives_count; ++j)
        {
            const auto& p = source->meshes[i].primitives[j];
            if (p.has_draco_mesh_compression || p.targets_count > MeshMorphLimit ||
                (p.targets_count && !source->skins_count))
                return std::unexpected("Unsupported compressed geometry or morph targets.");
        }
    for (std::size_t i = 0; i < source->buffer_views_count; ++i)
        if (source->buffer_views[i].has_meshopt_compression)
            return std::unexpected("Compressed geometry is not supported.");
    // Check ranges without overflowing before the library validator reads buffer contents.
    for (std::size_t i = 0; i < source->buffer_views_count; ++i)
    {
        const auto& view = source->buffer_views[i];
        if (!view.buffer || view.offset > view.buffer->size ||
            view.size > view.buffer->size - view.offset)
            return std::unexpected("Buffer view outside GLB buffer.");
    }
    for (std::size_t i = 0; i < source->accessors_count; ++i)
    {
        const auto& accessor = source->accessors[i];
        if (accessor.is_sparse || !accessor.buffer_view)
            return std::unexpected("Sparse or implicit-zero accessors are not supported.");
        const auto size = cgltf_calc_size(accessor.type, accessor.component_type);
        const auto component = cgltf_component_size(accessor.component_type);
        const auto available = accessor.buffer_view->size;
        if (!size || !component || !accessor.count || accessor.stride < size ||
            accessor.offset > available || size > available - accessor.offset ||
            accessor.count - 1 > (available - accessor.offset - size) / accessor.stride ||
            accessor.offset % component || accessor.buffer_view->offset % component ||
            accessor.stride % component)
            return std::unexpected("Invalid accessor range or alignment.");
    }
    if (cgltf_load_buffers(&options, source.get(), nullptr) != cgltf_result_success ||
        cgltf_validate(source.get()) != cgltf_result_success)
        return std::unexpected("Invalid GLB buffer or accessor.");
    const auto* scene = source->scene ? source->scene :
        (source->scenes_count ? &source->scenes[0] : nullptr);
    if (!scene) return std::unexpected("GLB contains no scene.");

    auto result = std::make_shared<MeshData>();
    std::array<std::unordered_map<const cgltf_texture*, std::size_t>, 2> textures;
    const auto read_texture = [&](const cgltf_texture_view& view, bool srgb,
        std::optional<std::size_t>& index) -> std::expected<void, std::string>
    {
        if (!view.texture) return {};
        if (view.texcoord != 0 || view.has_transform)
            return std::unexpected("PBR textures require TEXCOORD_0 without texture transforms.");
        auto& cache = textures[srgb];
        if (auto found = cache.find(view.texture); found != cache.end())
        {
            index = found->second;
            return {};
        }
        auto image = DecodeTexture(*view.texture, srgb);
        if (!image) return std::unexpected(std::move(image.error()));
        index = result->textures.size();
        result->textures.push_back(std::move(*image));
        cache.emplace(view.texture, *index);
        return {};
    };
    std::unordered_map<const cgltf_primitive*, std::size_t> geometries;
    std::unordered_map<const cgltf_node*, std::size_t> pose_indices;
    struct Node
    {
        const cgltf_node* node;
        glm::mat4 parent;
        std::optional<std::size_t> pose_parent;
    };
    std::vector<Node> pending;
    std::unordered_set<const cgltf_node*> visited;
    for (std::size_t i = scene->nodes_count; i > 0; --i)
        pending.push_back({ scene->nodes[i - 1], glm::mat4{ 1.0f } });
    while (!pending.empty())
    {
        const auto [node, parent, pose_parent] = pending.back();
        pending.pop_back();
        if (!visited.insert(node).second)
            return std::unexpected("GLB scene hierarchy is not a tree.");
        glm::mat4 local;
        cgltf_node_transform_local(node, &local[0][0]);
        const auto world = parent * local;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(world[column][row]))
                    return std::unexpected("Non-finite node transform.");
        std::optional<std::size_t> pose_node, skin;
        if (source->skins_count)
        {
            pose_node = result->pose_nodes.size();
            pose_indices.emplace(node, *pose_node);
            MeshPoseNode pose;
            pose.parent = pose_parent;
            if (node->has_matrix)
                pose.matrix = local;
            else
            {
                pose.rest.position = { node->translation[0], node->translation[1], node->translation[2] };
                pose.rest.rotation = glm::quat(node->rotation[3], node->rotation[0],
                    node->rotation[1], node->rotation[2]);
                pose.rest.scale = { node->scale[0], node->scale[1], node->scale[2] };
                const float rotation_length = glm::length(pose.rest.rotation);
                if (!std::isfinite(rotation_length) || rotation_length <= 0.0f)
                    return std::unexpected("Invalid GLB node rotation.");
                pose.rest.rotation = glm::normalize(pose.rest.rotation);
            }
            if (node->mesh && node->mesh->primitives_count)
            {
                const auto count = node->mesh->primitives[0].targets_count;
                const auto* weights = node->weights ? node->weights : node->mesh->weights;
                const auto weights_count = node->weights ? node->weights_count : node->mesh->weights_count;
                if (weights && (weights_count != count ||
                    !std::ranges::all_of(std::span(weights, count), [](float w) { return std::isfinite(w); })))
                    return std::unexpected("Invalid GLB default morph weights.");
                pose.morph_weights.resize(count);
                if (weights) std::copy_n(weights, count, pose.morph_weights.begin());
            }
            result->pose_nodes.push_back(std::move(pose));
        }
        if (node->skin)
            skin = node->skin - source->skins;
        if (node->has_mesh_gpu_instancing)
            return std::unexpected("GPU instancing extension is not supported.");
        if (node->mesh)
            for (std::size_t p = 0; p < node->mesh->primitives_count; ++p)
            {
                const auto* primitive = &node->mesh->primitives[p];
                auto found = geometries.find(primitive);
                if (found == geometries.end())
                {
                    if (primitive->type != cgltf_primitive_type_triangles)
                        return std::unexpected("Only triangle primitives are supported.");
                    const cgltf_accessor* positions = nullptr;
                    const cgltf_accessor* normals = nullptr;
                    const cgltf_accessor* texcoords = nullptr;
                    const cgltf_accessor* joints = nullptr;
                    const cgltf_accessor* weights = nullptr;
                    for (std::size_t a = 0; a < primitive->attributes_count; ++a)
                    {
                        const auto& attribute = primitive->attributes[a];
                        if (attribute.type == cgltf_attribute_type_position) positions = attribute.data;
                        if (attribute.type == cgltf_attribute_type_normal) normals = attribute.data;
                        if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0)
                            texcoords = attribute.data;
                        if (attribute.type == cgltf_attribute_type_joints ||
                            attribute.type == cgltf_attribute_type_weights)
                        {
                            if (attribute.index != 0)
                                return std::unexpected("Only four bone influences per vertex are supported.");
                            if (attribute.type == cgltf_attribute_type_joints)
                                joints = attribute.data;
                            else
                                weights = attribute.data;
                        }
                    }
                    if (!positions || positions->type != cgltf_type_vec3 ||
                        positions->component_type != cgltf_component_type_r_32f ||
                        !positions->count || positions->count > std::numeric_limits<std::uint32_t>::max() ||
                        positions->count > std::numeric_limits<std::ptrdiff_t>::max() / sizeof(glm::vec3))
                        return std::unexpected("Invalid position accessor.");
                    const auto index_count = primitive->indices ? primitive->indices->count : positions->count;
                    if (!index_count || index_count % 3 || index_count > std::numeric_limits<int>::max() ||
                        index_count > std::numeric_limits<std::ptrdiff_t>::max() / sizeof(std::uint32_t))
                        return std::unexpected("Invalid triangle index count.");
                    if (primitive->indices &&
                        (primitive->indices->type != cgltf_type_scalar ||
                         (primitive->indices->component_type != cgltf_component_type_r_8u &&
                          primitive->indices->component_type != cgltf_component_type_r_16u &&
                          primitive->indices->component_type != cgltf_component_type_r_32u)))
                        return std::unexpected("Invalid index accessor.");
                    MeshGeometry geometry;
                    geometry.positions.resize(positions->count);
                    std::vector<float> unpacked;
                    if (!ReadAttribute(*positions, cgltf_type_vec3, positions->count, 3, unpacked))
                        return std::unexpected("Cannot unpack positions.");
                    for (std::size_t i = 0; i < positions->count; ++i)
                        geometry.positions[i] = glm::vec3(unpacked[i * 3], unpacked[i * 3 + 1], unpacked[i * 3 + 2]);
                    if (normals)
                    {
                        if (!ReadAttribute(*normals, cgltf_type_vec3, positions->count, 3, unpacked))
                            return std::unexpected("Invalid GLB normals.");
                        geometry.normals.reserve(positions->count);
                        for (std::size_t i = 0; i < positions->count; ++i)
                            geometry.normals.emplace_back(unpacked[i * 3], unpacked[i * 3 + 1], unpacked[i * 3 + 2]);
                    }
                    if (texcoords)
                    {
                        if (!ReadAttribute(*texcoords, cgltf_type_vec2, positions->count, 2, unpacked))
                            return std::unexpected("Invalid GLB texture coordinates.");
                        geometry.texcoords.reserve(positions->count);
                        for (std::size_t i = 0; i < positions->count; ++i)
                            geometry.texcoords.emplace_back(unpacked[i * 2], unpacked[i * 2 + 1]);
                    }
                    if (joints || weights)
                    {
                        if (!joints || !weights || joints->normalized ||
                            (weights->component_type != cgltf_component_type_r_32f &&
                             !(weights->normalized && (weights->component_type == cgltf_component_type_r_8u ||
                                weights->component_type == cgltf_component_type_r_16u))) ||
                            (joints->component_type != cgltf_component_type_r_8u &&
                             joints->component_type != cgltf_component_type_r_16u) ||
                            !ReadAttribute(*joints, cgltf_type_vec4, positions->count, 4, unpacked))
                            return std::unexpected("Invalid GLB joint indices.");
                        for (std::size_t i = 0; i < unpacked.size(); i += 4)
                            geometry.joints.emplace_back(unpacked[i], unpacked[i + 1], unpacked[i + 2], unpacked[i + 3]);
                        if (!ReadAttribute(*weights, cgltf_type_vec4, positions->count, 4, unpacked))
                            return std::unexpected("Invalid GLB joint weights.");
                        for (std::size_t i = 0; i < unpacked.size(); i += 4)
                        {
                            const glm::vec4 weight(unpacked[i], unpacked[i + 1], unpacked[i + 2], unpacked[i + 3]);
                            const float sum = glm::dot(weight, glm::vec4(1.0f));
                            if (glm::any(glm::lessThan(weight, glm::vec4(0.0f))) ||
                                !std::isfinite(sum) || sum <= 0.0f)
                                return std::unexpected("Invalid GLB vertex weight sum.");
                            geometry.weights.push_back(weight / sum);
                        }
                    }
                    if (primitive->targets_count)
                    {
                        constexpr std::size_t deltas_per_vertex = 2;
                        const auto stride = primitive->targets_count * deltas_per_vertex;
                        if (positions->count > geometry.morph_deltas.max_size() / stride)
                            return std::unexpected("GLB morph data is too large.");
                        geometry.morph_deltas.resize(positions->count * stride, glm::vec4(0.0f));
                        for (std::size_t t = 0; t < primitive->targets_count; ++t)
                            for (std::size_t a = 0; a < primitive->targets[t].attributes_count; ++a)
                            {
                                const auto& delta = primitive->targets[t].attributes[a];
                                if (delta.type != cgltf_attribute_type_position &&
                                    delta.type != cgltf_attribute_type_normal)
                                    continue;
                                if (delta.data->component_type != cgltf_component_type_r_32f ||
                                    !ReadAttribute(*delta.data, cgltf_type_vec3, positions->count, 3, unpacked))
                                    return std::unexpected("Invalid GLB morph deltas.");
                                const std::size_t offset = delta.type == cgltf_attribute_type_normal ? 1 : 0;
                                for (std::size_t v = 0; v < positions->count; ++v)
                                    geometry.morph_deltas[(t * positions->count + v) * 2 + offset] =
                                        glm::vec4(unpacked[v * 3], unpacked[v * 3 + 1], unpacked[v * 3 + 2], 0.0f);
                            }
                    }
                    if (const auto* material = primitive->material)
                    {
                        auto& surface = geometry.material;
                        surface.double_sided = material->double_sided;
                        if (material->alpha_mode == cgltf_alpha_mode_mask)
                            surface.alpha_cutoff = material->alpha_cutoff;
                        // shortcut: blend materials render opaque until a sorted transparent pass exists.
                        if (material->has_pbr_metallic_roughness)
                        {
                            const auto& pbr = material->pbr_metallic_roughness;
                            surface.base_color = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                pbr.base_color_factor[2], pbr.base_color_factor[3]);
                            surface.metallic = pbr.metallic_factor;
                            surface.roughness = pbr.roughness_factor;
                            if (!std::ranges::all_of(pbr.base_color_factor, [](float x) { return std::isfinite(x); }) ||
                                !std::isfinite(surface.metallic) || !std::isfinite(surface.roughness) ||
                                !std::isfinite(surface.alpha_cutoff))
                                return std::unexpected("Non-finite GLB material factor.");
                            if (!texcoords && (pbr.base_color_texture.texture || pbr.metallic_roughness_texture.texture))
                                return std::unexpected("Textured GLB material requires UV coordinates.");
                            if (auto loaded = read_texture(pbr.base_color_texture, true, surface.base_texture); !loaded)
                                return std::unexpected(std::move(loaded.error()));
                            if (auto loaded = read_texture(pbr.metallic_roughness_texture, false,
                                surface.metallic_roughness_texture); !loaded)
                                return std::unexpected(std::move(loaded.error()));
                        }
                    }
                    geometry.indices.resize(index_count);
                    for (std::size_t i = 0; i < index_count; ++i)
                    {
                        const auto index = primitive->indices ?
                            cgltf_accessor_read_index(primitive->indices, i) : i;
                        if (index >= positions->count) return std::unexpected("Index outside vertex buffer.");
                        geometry.indices[i] = static_cast<std::uint32_t>(index);
                    }
                    const auto index = result->geometries.size();
                    result->geometries.push_back(std::move(geometry));
                    found = geometries.emplace(primitive, index).first;
                }
                if (node->skin)
                {
                    const auto& indices = result->geometries[found->second].joints;
                    if (indices.empty() || std::ranges::any_of(indices, [&](const glm::vec4& index) {
                        return glm::any(glm::greaterThanEqual(index, glm::vec4(float(node->skin->joints_count))));
                    }))
                        return std::unexpected("GLB vertex joint index outside skin.");
                }
                result->instances.push_back({ found->second, world, skin, pose_node });
            }
        for (std::size_t i = node->children_count; i > 0; --i)
            pending.push_back({ node->children[i - 1], world, pose_node });
    }
    if (result->instances.empty()) return std::unexpected("GLB scene contains no mesh.");
    for (std::size_t s = 0; s < source->skins_count; ++s)
    {
        const auto& skin = source->skins[s];
        if (!skin.joints_count || skin.joints_count > MeshJointLimit)
            return std::unexpected("GLB skin must contain 1 to 32 joints.");
        std::vector<float> inverse_bind;
        if (skin.inverse_bind_matrices &&
            (skin.inverse_bind_matrices->component_type != cgltf_component_type_r_32f ||
             !ReadAttribute(*skin.inverse_bind_matrices, cgltf_type_mat4, skin.joints_count, 16, inverse_bind)))
            return std::unexpected("Invalid GLB inverse bind matrices.");
        auto& joints = result->skins.emplace_back();
        for (std::size_t j = 0; j < skin.joints_count; ++j)
        {
            const auto node = pose_indices.find(skin.joints[j]);
            if (node == pose_indices.end())
                return std::unexpected("GLB skin joint outside active scene.");
            auto& joint = joints.emplace_back(MeshJoint{ node->second });
            if (!inverse_bind.empty())
                std::copy_n(inverse_bind.data() + j * 16, 16, &joint.inverse_bind[0][0]);
        }
    }

    if (source->animations_count)
    {
        const auto& animation = source->animations[0];
        float start = std::numeric_limits<float>::max();
        float end = 0.0f;
        for (std::size_t i = 0; i < animation.channels_count; ++i)
        {
            const auto& channel = animation.channels[i];
            const auto node = pose_indices.find(channel.target_node);
            const auto& sampler = *channel.sampler;
            if (node == pose_indices.end() || result->pose_nodes[node->second].matrix ||
                (sampler.interpolation != cgltf_interpolation_type_linear &&
                 sampler.interpolation != cgltf_interpolation_type_step))
                return std::unexpected("GLB animation requires scene TRS nodes and linear or step sampling.");
            using Property = MeshAnimationTrack::Property;
            Property property;
            switch (channel.target_path)
            {
            case cgltf_animation_path_type_translation: property = Property::Translation; break;
            case cgltf_animation_path_type_rotation: property = Property::Rotation; break;
            case cgltf_animation_path_type_scale: property = Property::Scale; break;
            case cgltf_animation_path_type_weights: property = Property::MorphWeights; break;
            default: return std::unexpected("Unsupported GLB animation target.");
            }
            if (std::ranges::any_of(result->tracks, [&](const MeshAnimationTrack& track) {
                return track.node == node->second && track.property == property;
            }))
                return std::unexpected("Duplicate GLB animation track.");
            auto& track = result->tracks.emplace_back(MeshAnimationTrack{
                node->second, property, sampler.interpolation == cgltf_interpolation_type_step });
            const auto& input = *sampler.input;
            const auto& output = *sampler.output;
            const bool rotation = property == Property::Rotation;
            std::vector<float> values;
            if (input.component_type != cgltf_component_type_r_32f ||
                output.component_type != cgltf_component_type_r_32f ||
                !ReadAttribute(input, cgltf_type_scalar, input.count, 1, track.times) ||
                track.times.front() < 0.0f ||
                std::adjacent_find(track.times.begin(), track.times.end(), std::greater_equal<>{}) != track.times.end())
                return std::unexpected("Invalid GLB animation keys.");
            const std::size_t components = rotation ? 4 : 3;
            if (property == Property::MorphWeights)
            {
                const auto count = result->pose_nodes[node->second].morph_weights.size();
                if (!count || output.count % count || output.count / count != input.count ||
                    !ReadAttribute(output, cgltf_type_scalar, output.count, 1, track.morph_values))
                    return std::unexpected("Invalid GLB morph weight keys.");
            }
            else if (!ReadAttribute(output, rotation ? cgltf_type_vec4 : cgltf_type_vec3,
                input.count, components, values))
                return std::unexpected("Invalid GLB animation values.");
            for (std::size_t k = 0; k < values.size(); k += components)
            {
                glm::vec4 value(values[k], values[k + 1], values[k + 2], rotation ? values[k + 3] : 0.0f);
                if (rotation)
                {
                    const float length = glm::length(value);
                    if (!std::isfinite(length) || length <= 0.0f)
                        return std::unexpected("Invalid GLB animation quaternion.");
                    value /= length;
                }
                track.values.push_back(value);
            }
            start = std::min(start, track.times.front());
            end = std::max(end, track.times.back());
        }
        if (!(start < end))
            return std::unexpected("GLB animation must have a positive duration.");
        result->animation_start = start;
        result->animation_duration = end - start;
    }
    return result;
}
