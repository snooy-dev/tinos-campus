# Tino's Campus
a game in development.

## Requirements

- Windows 10/11
- OpenGL 3.3 compatible graphics card with the latest drivers.

## Visual Studio Setup

Select the following in Visual Studio Installer:

- Visual Studio Community 2026
- **Workload**: Desktop development with C++
- **Individual components**: MSVC x64/x86 build tools, Windows 11 SDK

## vcpkg Setup and Visual Studio Integration

Choose one of the following options.

### Bundled with Visual Studio (Recommended)

Install **vcpkg package manager** from **Individual components** in Visual Studio Installer. Then run the following command once in **Developer PowerShell for Visual Studio 2026** to enable integration:

```powershell
vcpkg integrate install
```

### Standalone Installation

Run the following in PowerShell with Git installed. Replace `C:\dev\vcpkg` with your preferred installation path:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
Set-Location C:\dev\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg.exe integrate install
```

See the [Tutorial: Install and use packages with MSBuild in Visual Studio](https://learn.microsoft.com/vcpkg/get_started/get-started-msbuild) for details.

## Framework reflection

RTTR is installed through the root vcpkg manifest. The framework registers `GameObject`,
`Actor`, `ActorComponent`, `MeshComponent`, `Transform`, `glm::vec3` (`Vec3`) and
`glm::quat` (`Quaternion`). Actor and Component expose `local_transform`; MeshComponent
also exposes `color`. Property writes use the existing setters, including transform
invalidation. Mesh handles, world matrices, ownership and destruction state are not registered.
Construct objects through `Scene::SpawnActor` and `AddComponent`; reflection constructors
are deliberately not registered.

Code using RTTR directly must include `framework/reflection.hpp` **before** importing
`GameFramework` or `Engine`. In a module implementation, include it in the global module
fragment. Ordinary framework consumers do not need this header. The engine uses out-of-line
RTTR type-query functions: putting RTTR's inline template calls in the exported class
definitions caused MSVC C1116 during actual engine integration. Exported derived classes
should follow that pattern; ordinary `.cpp` classes can use `RTTR_ENABLE(Base)` and
`RTTR_REGISTRATION` (see `tests/reflection_tests.cpp`). Registration code is linked directly
into the executable, not an unreferenced static library.

`reflection.hpp` locally suppresses C4996 for RTTR's C++23-deprecated `aligned_storage` use
and C4251 for RTTR's exported registration classes containing internal implementation types.
Both diagnostics are restored after the includes; engine warning levels and SDL checks remain
enabled. Framework implementation units include this guard before importing their module's
RTTR declarations, because MSVC can otherwise re-emit C4251 during module import.
This suppresses library diagnostics, not DLL compatibility risks: keep RTTR and its clients
on matching compiler/runtime configurations and repeat the tests when upgrading them.
Level serialization, GameMode and editor UI are separate follow-up work; the standalone
serialization experiment remains under `tests/serialization_probe`.

Run the actual framework reflection and rendering regression tests from the repository root:

```powershell
python tests/make_mesh_fixtures.py
MSBuild engine/mesh_tests.vcxproj /p:Configuration=Debug /p:Platform=x64 "/p:SolutionDir=$((Get-Location).Path)\"
./bin/x64/Debug/MeshTests.exe
```

Repeat with `Release` to check optimized registration and rendering.
