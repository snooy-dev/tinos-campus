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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

enum Material { Canopy, Trunk, Leaf, MaterialCount };
using Meshes = std::array<std::vector<Vertex>, MaterialCount>;

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Meshes ReadTree(const fs::path& path)
{
    std::ifstream input(path);
    Require(input.good(), "Cannot open OBJ: " + path.string());
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    Meshes meshes;
    int material = -1;

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
        else if (tag == "usemtl")
        {
            std::string name;
            row >> name;
            if (name.starts_with("Hardwood_inner_canopy_"))
                material = Canopy;
            else if (name.starts_with("Hardwood_trunk_"))
                material = Trunk;
            else if (name.starts_with("Oak_leaves_"))
                material = Leaf;
            else
                throw std::runtime_error("Unexpected Hardwood material: " + name);
        }
        else if (tag == "f")
        {
            Require(material >= 0, "OBJ face has no material");
            for (int corner = 0; corner < 3; ++corner)
            {
                std::size_t position = 0, uv = 0, normal = 0;
                char slash1 = 0, slash2 = 0;
                Require(bool(row >> position >> slash1 >> uv >> slash2 >> normal) &&
                    slash1 == '/' && slash2 == '/' && position && uv && normal,
                    "Expected a triangulated OBJ with position/UV/normal indices");
                meshes[material].push_back({ positions.at(position - 1),
                    normals.at(normal - 1), uvs.at(uv - 1) });
            }
            std::string extra;
            Require(!(row >> extra), "Export Hardwood.obj with Triangulated Mesh enabled");
        }
    }

    for (const auto& mesh : meshes)
        Require(!mesh.empty(), "Hardwood OBJ is missing the canopy, trunk, or leaves");
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

GLuint CreateProgram(const fs::path& shaders)
{
    const GLuint program = glCreateProgram();
    for (const auto& [stage, name] : std::array{
        std::pair{GL_VERTEX_SHADER, "lit.vert"}, std::pair{GL_FRAGMENT_SHADER, "lit.frag"} })
    {
        const GLuint shader = Compile(stage, shaders / name);
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

void Preview(const fs::path& obj, const fs::path& textures, const fs::path& shaders,
    const fs::path& captureDirectory)
{
    const auto meshes = ReadTree(obj);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, captureDirectory.empty() ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1120, 600,
        "Hardwood | Left light / Right light | M: leaf mask | Esc: exit", nullptr, nullptr);
    Require(window != nullptr, "Cannot open an OpenGL 3.3 window");
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    Require(glewInit() == GLEW_OK, "GLEW initialization failed");
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.09f, 0.11f, 0.14f, 1.0f);

    ComPtr<IWICImagingFactory> wic;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))), "WIC initialization failed");
    const std::array baseTextures{
        LoadTexture(*wic.Get(), textures / "T_HardwoodTree_Canopy.png"),
        LoadTexture(*wic.Get(), textures / "T_HardwoodTree_Trunk.png"),
        LoadTexture(*wic.Get(), textures / "T_HardwoodTree_Leaf.png")
    };
    const auto maskTexture = LoadTexture(*wic.Get(), textures / "T_HardwoodTree_Leaf_O.png");
    const std::array drawMeshes{ UploadMesh(meshes[Canopy]), UploadMesh(meshes[Trunk]),
        UploadMesh(meshes[Leaf]) };

    const GLuint program = CreateProgram(shaders);
    glUseProgram(program);
    const auto uniform = [program](const char* name)
    {
        const auto location = glGetUniformLocation(program, name);
        Require(location >= 0, std::string("Missing shader uniform: ") + name);
        return location;
    };
    const auto projection = uniform("uProjection");
    const auto light = uniform("uLightDirection");
    const auto useMask = uniform("uUseAlphaMask");
    const glm::mat4 model{1.0f};
    const auto view = glm::lookAt(glm::vec3(0, 3.0f, 11.5f), glm::vec3(0, 2.6f, 0),
        glm::vec3(0, 1, 0));
    glUniformMatrix4fv(uniform("uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(uniform("uView"), 1, GL_FALSE, glm::value_ptr(view));
    glUniform1i(uniform("uUseBaseColorTexture"), GL_TRUE);
    glUniform1i(uniform("uBaseColorTexture"), 0);
    glUniform1i(uniform("uAlphaMask"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, maskTexture);
    glActiveTexture(GL_TEXTURE0);

    const auto draw = [&](int width, int height, bool mask)
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const int half = width / 2;
        const auto perspective = glm::perspective(glm::radians(32.0f),
            static_cast<float>(half) / height, 0.1f, 40.0f);
        glUniformMatrix4fv(projection, 1, GL_FALSE, glm::value_ptr(perspective));
        for (int side = 0; side < 2; ++side)
        {
            glViewport(side * half, 0, half, height);
            glUniform3f(light, side == 0 ? -1.0f : 1.0f, 0.55f, 0.5f);
            for (int part = 0; part < MaterialCount; ++part)
            {
                glUniform1i(useMask, mask && part == Leaf);
                glBindTexture(GL_TEXTURE_2D, baseTextures[part]);
                glBindVertexArray(drawMeshes[part].vao);
                glDrawArrays(GL_TRIANGLES, 0, drawMeshes[part].vertices);
            }
        }
    };

    std::cout << "OpenGL " << glGetString(GL_VERSION) << '\n';
    std::cout << "Loaded " << (meshes[Canopy].size() + meshes[Trunk].size() +
        meshes[Leaf].size()) / 3 << " triangles\n";
    bool mask = true;
    glfwSetWindowUserPointer(window, &mask);
    glfwSetKeyCallback(window, [](GLFWwindow* current, int key, int, int action, int)
    {
        if (action == GLFW_PRESS && key == GLFW_KEY_M)
        {
            auto& enabled = *static_cast<bool*>(glfwGetWindowUserPointer(current));
            enabled = !enabled;
            glfwSetWindowTitle(current, enabled ?
                "Hardwood | Left light / Right light | Mask ON (M) | Esc: exit" :
                "Hardwood | Left light / Right light | Mask OFF (M) | Esc: exit");
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
    if (argc != 1 && argc != 4 && argc != 5)
    {
        std::cerr << "Usage: LitTreePreview <Hardwood.obj> <Textures> <shaders> [capture-directory]\n";
        return 2;
    }
    const bool initializedCom = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    int status = 0;
    try
    {
        Require(initializedCom, "COM initialization failed");
        Require(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        if (argc == 1)
        {
            const auto directory = fs::absolute(argv[0]).parent_path();
            Preview(directory / "Hardwood.obj", directory / "Textures",
                directory / "../../contents/shaders", {});
        }
        else
            Preview(argv[1], argv[2], argv[3], argc == 5 ? fs::path(argv[4]) : fs::path{});
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
