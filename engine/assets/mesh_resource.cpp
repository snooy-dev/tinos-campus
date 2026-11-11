// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module;
#define CGLTF_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable: 4996)
#include <cgltf.h>
#pragma warning(pop)
module MeshResource;

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
    if (source->extensions_required_count || source->skins_count || source->animations_count)
        return std::unexpected("Required extensions, skins and animations are not supported.");
    for (std::size_t i = 0; i < source->meshes_count; ++i)
        for (std::size_t j = 0; j < source->meshes[i].primitives_count; ++j)
        {
            const auto& p = source->meshes[i].primitives[j];
            if (p.targets_count || p.has_draco_mesh_compression)
                return std::unexpected("Morph targets and compressed geometry are not supported.");
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
    std::unordered_map<const cgltf_primitive*, std::size_t> geometries;
    struct Node { const cgltf_node* node; glm::mat4 parent; };
    std::vector<Node> pending;
    std::unordered_set<const cgltf_node*> visited;
    for (std::size_t i = scene->nodes_count; i > 0; --i)
        pending.push_back({ scene->nodes[i - 1], glm::mat4{ 1.0f } });
    while (!pending.empty())
    {
        const auto [node, parent] = pending.back();
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
                    for (std::size_t a = 0; a < primitive->attributes_count; ++a)
                        if (primitive->attributes[a].type == cgltf_attribute_type_position)
                            positions = primitive->attributes[a].data;
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
                    std::vector<float> unpacked(positions->count * 3);
                    if (cgltf_accessor_unpack_floats(positions, unpacked.data(), unpacked.size()) != unpacked.size())
                        return std::unexpected("Cannot unpack positions.");
                    for (std::size_t i = 0; i < positions->count; ++i)
                    {
                        for (int axis = 0; axis < 3; ++axis)
                            if (!std::isfinite(unpacked[i * 3 + axis]))
                                return std::unexpected("Non-finite vertex.");
                        geometry.positions[i] = glm::vec3(unpacked[i * 3], unpacked[i * 3 + 1], unpacked[i * 3 + 2]);
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
                result->instances.push_back({ found->second, world });
            }
        for (std::size_t i = node->children_count; i > 0; --i)
            pending.push_back({ node->children[i - 1], world });
    }
    if (result->instances.empty()) return std::unexpected("GLB scene contains no mesh.");
    return result;
}
