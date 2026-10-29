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

### Bundled with Visual Studio

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
