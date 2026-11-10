// Run from the repository root after tests/make_mesh_fixtures.py.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
import std;
import Engine;
import <cstdio>;
import <GL/glew.h>;
import Application;
import JobSystem;
import Renderer;
import GameFramework;

void RunReflectionTests();

namespace
{
void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
const std::filesystem::path fixtures = "build/mesh-fixtures";
bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

PFNGLGENBUFFERSPROC original_gen_buffers = nullptr;
unsigned partial_buffer = 0;
int buffer_calls = 0;
void GLAPIENTRY FailSecondBuffer(GLsizei count, GLuint* buffers)
{
    if (++buffer_calls == 1)
    {
        original_gen_buffers(count, buffers);
        partial_buffer = buffers[0];
    }
    else
        std::fill_n(buffers, count, 0u);
}

struct EngineProbe : Actor
{
    int ticks = 0;
    const std::thread::id owner = std::this_thread::get_id();
    EngineProbe(Scene& scene, MeshHandle mesh) : Actor(scene)
    {
        AddComponent<MeshComponent>()->SetMesh(std::move(mesh));
    }
    void Update(double delta) override
    {
        Check(std::this_thread::get_id() == owner, "Engine updated scene on a worker");
        Check(std::isfinite(delta) && delta >= 0, "Invalid engine delta");
        if (++ticks == 1) Check(delta == 0, "First engine delta is not zero");
        if (ticks == 8)
            Check(PostMessageW(FindWindowW(nullptr, L"Tino's Campus"), WM_CLOSE, 0, 0) != 0,
                "Cannot close engine test window");
    }
};

struct Target
{
    unsigned fbo = 0, color = 0, depth = 0;
    void Create()
    {
        while (glGetError() != GL_NO_ERROR) {}
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 64, 64);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "FBO incomplete");
    }
    std::array<unsigned char, 4> Pixel()
    {
        std::array<unsigned char, 4> pixel{};
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        Check(glGetError() == GL_NO_ERROR, "OpenGL error during rendering");
        return pixel;
    }
    void Release()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color);
        glDeleteRenderbuffers(1, &depth);
    }
};
}

int main()
{
    RunReflectionTests();
    const auto bytes = MeshLoading::Read(fixtures / "multi.glb");
    Check(bytes.has_value(), "Fixture read failed");
    const auto parsed = MeshLoading::Parse(*bytes);
    Check(parsed.has_value(), "Multi-node parse failed");
    Check((*parsed)->geometries.size() == 2 && (*parsed)->instances.size() == 4, "Geometry sharing failed");
    Check(Near((*parsed)->instances[0].transform[3].x, 0.4f), "Node transform failed");
    Check((*parsed)->geometries[1].indices == std::vector<std::uint32_t>{0, 1, 2}, "Non-indexed primitive failed");
    for (const auto* name : {"broken.glb", "overflow.glb", "range.glb", "index.glb", "nan.glb", "extension.glb", "lines.glb"})
    {
        auto invalid = MeshLoading::Read(fixtures / name);
        Check(invalid.has_value() && !MeshLoading::Parse(*invalid), name);
    }
    Check(!MeshLoading::Read(fixtures / "missing.glb"), "Missing file accepted");
    Application app;
    JobSystem jobs;
    Renderer renderer;
    Check(app.Initialize().has_value(), "Application init failed");
    jobs.Initialize(2, 1);
    Check(renderer.Initialize(app, jobs).has_value(), "Renderer init failed");

    std::atomic<bool> gate{ false };
    auto blocked = jobs.SubmitIo([&] { gate.wait(false); });
    auto mesh = renderer.LoadMeshAsync(fixtures / "triangle.glb");
    Check(mesh.Status() == MeshStatus::Loading, "Delayed load was not pending");
    renderer.SubmitFrame({64, 64}).Wait();
    Check(mesh.Status() == MeshStatus::Loading, "Render frame waited for IO");
    gate.store(true);
    gate.notify_all();
    blocked.Wait();
    Check(mesh.Wait().has_value(), "Mesh upload failed");
    auto invalid = renderer.LoadMeshAsync(fixtures / "missing.glb");
    Check(!invalid.Wait() && !invalid.Error().empty(), "Async failure not reported");

    jobs.SubmitRender([] {
        original_gen_buffers = glGenBuffers;
        glGenBuffers = FailSecondBuffer;
    }).Wait();
    auto failed_upload = renderer.LoadMeshAsync(fixtures / "triangle.glb");
    Check(!failed_upload.Wait() && !failed_upload.Error().empty(), "GPU upload failure was lost");
    jobs.SubmitRender([] {
        glGenBuffers = original_gen_buffers;
        Check(partial_buffer != 0 && !glIsBuffer(partial_buffer), "Partial GPU resource leaked");
    }).Wait();

    Target target;
    jobs.SubmitRender([&] { target.Create(); }).Wait();
    Scene scene;
    auto* actor = scene.SpawnActor<Actor>();
    auto* parent = actor->AddComponent<ActorComponent>();
    auto* front = parent->AddComponent<MeshComponent>();
    front->SetMesh(mesh);
    front->SetColor({1, 0, 0});
    auto* back = actor->AddComponent<MeshComponent>();
    back->SetMesh(mesh);
    back->SetColor({0, 1, 0});
    Transform t;
    t.position.z = -0.5f;
    t.rotation = glm::angleAxis(0.4f, glm::vec3{0, 0, 1});
    t.scale = {-0.8f, 1.1f, 1.0f};
    parent->SetLocalTransform(t);
    t = {};
    t.position.z = 0.5f;
    back->SetLocalTransform(t);
    scene.UpdateWorldTransforms();
    auto draws = scene.CollectRenderItems();
    Check(draws.size() == 2 && draws[0].data == draws[1].data, "Mesh sharing failed");
    renderer.SubmitFrame({64, 64, glm::mat4{1.0f}, std::move(draws)}).Wait();
    jobs.SubmitRender([&] {
        const auto p = target.Pixel();
        Check(p[0] == 255 && p[1] == 0, "Color or depth test failed");
    }).Wait();

    t.position = {2, 0, -0.5f};
    parent->SetLocalTransform(t);
    scene.UpdateWorldTransforms();
    renderer.SubmitFrame({64, 64, glm::mat4{1.0f}, scene.CollectRenderItems()}).Wait();
    jobs.SubmitRender([&] {
        const auto p = target.Pixel();
        Check(p[0] == 0 && p[1] == 255, "Parent transform failed");
    }).Wait();

    glm::mat4 view_projection{1.0f};
    view_projection[3].x = 3.0f;
    renderer.SubmitFrame({64, 64, view_projection, scene.CollectRenderItems()}).Wait();
    jobs.SubmitRender([&] {
        const auto p = target.Pixel();
        Check(p[0] < 40 && p[1] < 40 && p[2] < 40, "View projection was ignored");
    }).Wait();

    auto pending_mesh = renderer.LoadMeshAsync(fixtures / "multi.glb");
    auto* temporary = actor->AddComponent<MeshComponent>();
    temporary->SetMesh(pending_mesh);
    temporary->Destroy();
    scene.Update(0);
    Check(pending_mesh.Wait().has_value(), "Deleting loading component broke load");
    auto snapshot = scene.CollectRenderItems();
    front->SetMesh({});
    actor->Destroy();
    scene.Update(0);
    Check(scene.CollectRenderItems().empty(), "Destroyed mesh was collected");
    renderer.SubmitFrame({64, 64, glm::mat4{1.0f}, std::move(snapshot)}).Wait();
    jobs.SubmitRender([&] {
        const auto p = target.Pixel();
        Check(p[1] == 255, "Snapshot did not survive component destruction");
        target.Release();
    }).Wait();

    auto during_shutdown = renderer.LoadMeshAsync(fixtures / "multi.glb");
    renderer.Shutdown();
    Check(during_shutdown.Wait().has_value(), "Shutdown lost descendant load jobs");
    jobs.Shutdown();
    jobs.Initialize(2, 1);
    Check(renderer.Initialize(app, jobs).has_value(), "Renderer reinit failed");
    std::ostringstream errors;
    auto* previous_errors = std::cerr.rdbuf(errors.rdbuf());
    jobs.SubmitRender([&] {
        target.Create();
        buffer_calls = 0;
        glGenBuffers = FailSecondBuffer;
    }).Wait();
    for (int frame = 0; frame < 2; ++frame)
        renderer.SubmitFrame({64, 64, glm::mat4{1.0f}, {{mesh.Data(), glm::mat4{1.0f}, {0, 0, 1}}}}).Wait();
    jobs.SubmitRender([&] {
        glGenBuffers = original_gen_buffers;
        Check(buffer_calls == 2, "Failed recreation retried every frame");
        Check(partial_buffer != 0 && !glIsBuffer(partial_buffer), "Failed recreation leaked a buffer");
        while (glGetError() != GL_NO_ERROR) {}
        Check(target.Pixel()[2] < 40, "Failed recreation rendered invalid geometry");
        target.Release();
    }).Wait();
    std::cerr.rdbuf(previous_errors);
#ifdef _DEBUG
    Check(errors.str() == "Mesh GPU upload failed.\n", "Recreation failure must be reported once in Debug");
#else
    Check(errors.str().empty(), "Recoverable recreation failure must be silent in Release");
#endif
    renderer.Shutdown();
    jobs.Shutdown();
    jobs.Initialize(2, 1);
    Check(renderer.Initialize(app, jobs).has_value(), "Renderer recovery failed");
    jobs.SubmitRender([&] { target.Create(); }).Wait();
    renderer.SubmitFrame({64, 64, glm::mat4{1.0f}, {{mesh.Data(), glm::mat4{1.0f}, {0, 0, 1}}}}).Wait();
    jobs.SubmitRender([&] {
        const auto p = target.Pixel();
        Check(p[2] == 255, "GPU resource recreation failed");
        target.Release();
    }).Wait();
    const auto window = FindWindowW(nullptr, L"Tino's Campus");
    Check(window != nullptr, "Missing test window");
    ShowWindow(window, SW_MINIMIZE);
    bool minimized = false;
    for (int attempt = 0; attempt < 50 && !minimized; ++attempt)
    {
        minimized = app.ProcessEvents().minimized;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Check(minimized, "Minimize event failed");
    renderer.SubmitFrame({0, 0}).Wait();
    ShowWindow(window, SW_RESTORE);
    bool restored = false;
    for (int attempt = 0; attempt < 50 && !restored; ++attempt)
    {
        restored = !app.ProcessEvents().minimized;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Check(restored, "Restore event failed");
    Check(SetWindowPos(window, nullptr, 0, 0, 900, 600, SWP_NOMOVE | SWP_NOZORDER) != 0,
        "Resize failed");
    const auto state = app.ProcessEvents();
    Check(state.framebuffer_width > 0 && state.framebuffer_width < 900, "Framebuffer resize not observed");
    renderer.SubmitFrame({state.framebuffer_width, state.framebuffer_height}).Wait();
    renderer.Shutdown();
    jobs.Shutdown();
    app.Shutdown();

    Engine engine;
    Check(engine.Initialize().has_value(), "Engine init failed");
    auto engine_mesh = engine.LoadMeshAsync(fixtures / "triangle.glb");
    Check(engine_mesh.Wait().has_value(), "Engine mesh request failed");
    auto* probe = engine.GetScene().SpawnActor<EngineProbe>(engine_mesh);
    engine.SetViewProjection(glm::mat4{1.0f});
    engine.Run();
    Check(probe->ticks == 8, "Engine scene pipeline failed");
    std::puts("Reflection, mesh parser, async progress, lifetime, depth, transform and reinitialization checks passed.");
}
