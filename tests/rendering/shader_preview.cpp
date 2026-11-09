#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GLFW_INCLUDE_NONE
#include <Windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct Material
{
    glm::vec3 color{1.0f};
    fs::path colorTexture;
    fs::path alphaMask;
};

struct Mesh
{
    std::vector<Vertex> vertices;
    Material material;
};

struct PreviewFiles
{
    fs::path model;
    fs::path vertexShader;
    fs::path fragmentShader;
};

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::string ReadValue(std::istream& row)
{
    std::string value;
    std::getline(row >> std::ws, value);
    if (const auto end = value.find_last_not_of(" \t\r"); end != std::string::npos)
        value.erase(end + 1);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return value;
}

fs::path PathFromUtf8(const std::string& value)
{
    return fs::path(std::u8string(value.begin(), value.end()));
}

PreviewFiles ReadSettings(const fs::path& path)
{
    std::ifstream input(path);
    Require(input.good(), "Cannot open preview settings: " + path.string());
    PreviewFiles files;
    for (std::string line; std::getline(input, line);)
    {
        std::istringstream row(line);
        std::string key;
        if (!std::getline(row >> std::ws, key, '=') || key.starts_with('#'))
            continue;
        key.erase(key.find_last_not_of(" \t") + 1);
        const auto value = ReadValue(row);
        Require(!value.empty(), "Missing path for " + key);
        const auto file = path.parent_path() / PathFromUtf8(value);
        if (key == "model")
            files.model = file;
        else if (key == "vertex")
            files.vertexShader = file;
        else if (key == "fragment")
            files.fragmentShader = file;
        else
            throw std::runtime_error("Unknown preview setting: " + key);
    }
    Require(!files.model.empty() && !files.vertexShader.empty() && !files.fragmentShader.empty(),
        "Set model, vertex, and fragment in the preview settings");
    return files;
}

fs::path ResolveTexture(const fs::path& directory, const std::string& value)
{
    Require(!value.empty(), "MTL texture path is missing");
    const auto file = PathFromUtf8(value);
    const auto original = directory / file;
    if (fs::is_regular_file(original))
        return original;
    return directory / "Textures" / file.filename();
}

void ReadMaterials(const fs::path& path, std::map<std::string, Material>& materials)
{
    std::ifstream input(path);
    Require(input.good(), "Cannot open MTL: " + path.string());
    Material* material = nullptr;
    for (std::string line; std::getline(input, line);)
    {
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag == "newmtl")
            material = &materials[ReadValue(row)];
        else if (material && tag == "Kd")
            Require(bool(row >> material->color.r >> material->color.g >> material->color.b),
                "Invalid MTL diffuse color");
        else if (material && tag == "map_Kd")
            material->colorTexture = ResolveTexture(path.parent_path(), ReadValue(row));
        else if (material && tag == "map_d")
            material->alphaMask = ResolveTexture(path.parent_path(), ReadValue(row));
    }
}

std::size_t ObjIndex(const std::string& token, std::size_t count)
{
    std::size_t end = 0;
    const auto index = std::stoll(token, &end);
    const auto resolved = index > 0 ? index - 1 : static_cast<long long>(count) + index;
    Require(end == token.size() && index != 0 && resolved >= 0 &&
        static_cast<std::size_t>(resolved) < count, "OBJ index out of range: " + token);
    return static_cast<std::size_t>(resolved);
}

std::vector<Mesh> ReadMeshes(const fs::path& path)
{
    std::ifstream input(path);
    Require(input.good(), "Cannot open OBJ: " + path.string());
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    std::map<std::string, Material> materials;
    std::map<std::string, std::vector<Vertex>> batches;
    std::string materialName;

    for (std::string line; std::getline(input, line);)
    {
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag == "v" || tag == "vn")
        {
            glm::vec3 value{};
            Require(bool(row >> value.x >> value.y >> value.z), "Invalid OBJ position or normal");
            (tag == "v" ? positions : normals).push_back(value);
        }
        else if (tag == "vt")
        {
            glm::vec2 value{};
            Require(bool(row >> value.x >> value.y), "Invalid OBJ UV");
            uvs.push_back(value);
        }
        else if (tag == "mtllib")
            ReadMaterials(path.parent_path() / PathFromUtf8(ReadValue(row)), materials);
        else if (tag == "usemtl")
            materialName = ReadValue(row);
        else if (tag == "f")
        {
            auto& batch = batches[materialName];
            const auto material = materials.find(materialName);
            const bool textured = material != materials.end() &&
                (!material->second.colorTexture.empty() || !material->second.alphaMask.empty());
            for (int corner = 0; corner < 3; ++corner)
            {
                std::string token;
                Require(bool(row >> token), "Incomplete OBJ triangle");
                const auto slash1 = token.find('/');
                const auto slash2 = slash1 == std::string::npos ? slash1 : token.find('/', slash1 + 1);
                Require(slash2 != std::string::npos && slash2 + 1 < token.size(),
                    "Export the OBJ with Normals enabled");
                const auto uv = token.substr(slash1 + 1, slash2 - slash1 - 1);
                Require(!textured || !uv.empty(), "Textured OBJ material needs UV coordinates");
                batch.push_back({ positions.at(ObjIndex(token.substr(0, slash1), positions.size())),
                    normals.at(ObjIndex(token.substr(slash2 + 1), normals.size())),
                    uv.empty() ? glm::vec2(0.0f) : uvs.at(ObjIndex(uv, uvs.size())) });
            }
            std::string extra;
            Require(!(row >> extra) || extra.starts_with('#'),
                "Export the OBJ with Triangulated Mesh enabled");
        }
    }

    Require(!batches.empty(), "OBJ contains no triangles");
    std::vector<Mesh> meshes;
    for (auto& [name, vertices] : batches)
    {
        Require(name.empty() || materials.contains(name), "Undefined OBJ material: " + name);
        meshes.push_back({ std::move(vertices), name.empty() ? Material{} : materials.at(name) });
    }
    return meshes;
}

GLuint LoadTexture(IWICImagingFactory& factory, const fs::path& path)
{
    const auto error = "Cannot read texture: " + path.string();
    ComPtr<IWICBitmapDecoder> decoder;
    Require(SUCCEEDED(factory.CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())), error);
    ComPtr<IWICBitmapFrameDecode> frame;
    Require(SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())), error);
    ComPtr<IWICFormatConverter> rgba;
    Require(SUCCEEDED(factory.CreateFormatConverter(rgba.GetAddressOf())), error);
    Require(SUCCEEDED(rgba->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)), error);
    UINT width = 0, height = 0;
    Require(SUCCEEDED(rgba->GetSize(&width, &height)) && width && height &&
        std::uint64_t{width} * height <= std::numeric_limits<UINT>::max() / 4, error);
    const UINT stride = width * 4;
    std::vector<BYTE> pixels(stride * height);
    Require(SUCCEEDED(rgba->CopyPixels(nullptr, stride,
        static_cast<UINT>(pixels.size()), pixels.data())), error);
    for (UINT y = 0; y < height / 2; ++y)
        std::swap_ranges(pixels.data() + y * stride, pixels.data() + (y + 1) * stride,
            pixels.data() + (height - 1 - y) * stride);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texture;
}

GLuint Compile(GLenum stage, const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    Require(file.good(), "Cannot open shader: " + path.string());
    std::ostringstream source;
    source << file.rdbuf();
    const std::string code = source.str();
    const char* text = code.c_str();
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[4096]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        throw std::runtime_error(path.string() + ": " + log);
    }
    return shader;
}

GLuint CreateProgram(const PreviewFiles& files)
{
    const GLuint program = glCreateProgram();
    for (const auto& [stage, path] : std::array{
        std::pair{GL_VERTEX_SHADER, files.vertexShader}, std::pair{GL_FRAGMENT_SHADER, files.fragmentShader} })
    {
        const GLuint shader = Compile(stage, path);
        glAttachShader(program, shader);
        glDeleteShader(shader);
    }
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[4096]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Shader link failed: ") + log);
    }
    return program;
}

struct DrawMesh
{
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei vertices = 0;
    glm::vec3 color{1.0f};
    GLuint colorTexture = 0;
    GLuint alphaMask = 0;
};

DrawMesh UploadMesh(const std::vector<Vertex>& vertices)
{
    DrawMesh mesh;
    mesh.vertices = static_cast<GLsizei>(vertices.size());
    glGenVertexArrays(1, &mesh.vao);
    glBindVertexArray(mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
        vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    for (GLuint location = 0; location < 3; ++location)
        glEnableVertexAttribArray(location);
    return mesh;
}

void Capture(const fs::path& path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    const auto stride = static_cast<std::size_t>(width) * 3;
    for (int y = height; y-- > 0;)
        out.write(reinterpret_cast<const char*>(pixels.data() + y * stride), stride);
    Require(out.good(), "Cannot write capture: " + path.string());
}

void Preview(const PreviewFiles& files, const fs::path& captureDirectory)
{
    const auto meshes = ReadMeshes(files.model);
    auto minimum = meshes.front().vertices.front().position;
    auto maximum = minimum;
    std::size_t triangleCount = 0;
    for (const auto& mesh : meshes)
    {
        triangleCount += mesh.vertices.size() / 3;
        for (const auto& vertex : mesh.vertices)
        {
            minimum = glm::min(minimum, vertex.position);
            maximum = glm::max(maximum, vertex.position);
        }
    }
    const auto center = (minimum + maximum) * 0.5f;
    const float radius = glm::length(maximum - minimum) * 0.5f;
    Require(std::isfinite(radius) && radius > 0.0f, "Invalid model bounds");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, captureDirectory.empty() ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1120, 600,
        "Shader Preview | Left light / Right light | M: alpha mask | Esc: exit", nullptr, nullptr);
    Require(window != nullptr, "Cannot open an OpenGL 3.3 window");
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    Require(glewInit() == GLEW_OK, "GLEW initialization failed");
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.09f, 0.11f, 0.14f, 1.0f);

    ComPtr<IWICImagingFactory> wic;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))), "WIC initialization failed");
    std::vector<DrawMesh> drawMeshes;
    for (const auto& mesh : meshes)
    {
        auto drawMesh = UploadMesh(mesh.vertices);
        drawMesh.color = mesh.material.color;
        if (!mesh.material.colorTexture.empty())
            drawMesh.colorTexture = LoadTexture(*wic.Get(), mesh.material.colorTexture);
        if (!mesh.material.alphaMask.empty())
            drawMesh.alphaMask = LoadTexture(*wic.Get(), mesh.material.alphaMask);
        drawMeshes.push_back(drawMesh);
    }

    const GLuint program = CreateProgram(files);
    glUseProgram(program);
    const auto uniform = [program](const char* name)
    {
        return glGetUniformLocation(program, name);
    };
    const auto viewLocation = uniform("uView");
    const auto projection = uniform("uProjection");
    const auto light = uniform("uLightDirection");
    const auto useMask = uniform("uUseAlphaMask");
    const auto baseColor = uniform("uBaseColor");
    const auto useColorTexture = uniform("uUseBaseColorTexture");
    const glm::mat4 model{1.0f};
    glUniformMatrix4fv(uniform("uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform1i(uniform("uBaseColorTexture"), 0);
    glUniform1i(uniform("uAlphaMask"), 1);

    const auto draw = [&](int width, int height, bool mask)
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const int half = width / 2;
        const float aspect = static_cast<float>(half) / height;
        const float halfFov = glm::radians(16.0f);
        const float fitAngle = std::min(halfFov, std::atan(std::tan(halfFov) * aspect));
        const float distance = 1.1f * radius / std::sin(fitAngle);
        const auto view = glm::lookAt(center + glm::normalize(glm::vec3(0, 0.06f, 1)) * distance,
            center, glm::vec3(0, 1, 0));
        const auto perspective = glm::perspective(2.0f * halfFov, aspect,
            distance - radius * 1.5f, distance + radius * 1.5f);
        glUniformMatrix4fv(viewLocation, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projection, 1, GL_FALSE, glm::value_ptr(perspective));
        for (int side = 0; side < 2; ++side)
        {
            glViewport(side * half, 0, half, height);
            glUniform3f(light, side == 0 ? -1.0f : 1.0f, 0.55f, 0.5f);
            for (const auto& mesh : drawMeshes)
            {
                glUniform4f(baseColor, mesh.color.r, mesh.color.g, mesh.color.b, 1.0f);
                glUniform1i(useColorTexture, mesh.colorTexture != 0);
                glUniform1i(useMask, mask && mesh.alphaMask != 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mesh.colorTexture);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, mesh.alphaMask);
                glBindVertexArray(mesh.vao);
                glDrawArrays(GL_TRIANGLES, 0, mesh.vertices);
            }
        }
    };

    std::cout << "OpenGL " << glGetString(GL_VERSION) << '\n';
    std::cout << "Loaded " << triangleCount << " triangles from " << files.model.string() << '\n';
    std::cout << "Shaders: " << files.vertexShader.string() << " + " << files.fragmentShader.string() << '\n';
    bool mask = true;
    glfwSetWindowUserPointer(window, &mask);
    glfwSetKeyCallback(window, [](GLFWwindow* current, int key, int, int action, int)
    {
        if (action == GLFW_PRESS && key == GLFW_KEY_M)
        {
            auto& enabled = *static_cast<bool*>(glfwGetWindowUserPointer(current));
            enabled = !enabled;
            glfwSetWindowTitle(current, enabled ?
                "Shader Preview | Left light / Right light | Mask ON (M) | Esc: exit" :
                "Shader Preview | Left light / Right light | Mask OFF (M) | Esc: exit");
        }
        if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
            glfwSetWindowShouldClose(current, GLFW_TRUE);
    });
    while (!glfwWindowShouldClose(window))
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width >= 2 && height > 0)
        {
            draw(width, height, mask);
            if (!captureDirectory.empty())
            {
                Capture(captureDirectory / "mask-on.ppm", width, height);
                draw(width, height, false);
                Capture(captureDirectory / "mask-off.ppm", width, height);
                Require(glGetError() == GL_NO_ERROR, "OpenGL preview failed");
                break;
            }
            glfwSwapBuffers(window);
        }
        glfwWaitEvents();
    }
    glfwDestroyWindow(window);
}
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "Usage: ShaderPreview <preview.ini> [capture-directory]\n";
        return 2;
    }
    const bool initializedCom = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    int status = 0;
    try
    {
        Require(initializedCom, "COM initialization failed");
        Require(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        Preview(ReadSettings(argv[1]), argc == 3 ? fs::path(argv[2]) : fs::path{});
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    glfwTerminate();
    if (initializedCom)
        CoUninitialize();
    return status;
}
