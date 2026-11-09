param(
    [string]$ObjPath = (Join-Path $env:USERPROFILE 'Downloads\Hardwood.obj'),
    [string]$TexturesPath = (Join-Path $env:USERPROFILE 'AppData\Local\Temp'),
    [switch]$Capture
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'build\lit-tree-preview'
$vcpkg = Join-Path $repo 'third_party\vcpkg_installed\x64-windows'
$source = Join-Path $PSScriptRoot 'lit_tree_preview.cpp'
$executable = Join-Path $output 'LitTreePreview.exe'
$stagedTextures = Join-Path $output 'Textures'
$stagedObj = Join-Path $output 'Hardwood.obj'
$shaders = Join-Path $repo 'contents\shaders'

if (!(Test-Path -LiteralPath (Join-Path $vcpkg 'lib\glfw3dll.lib'))) {
    throw 'Build tino.slnx for x64 first to install the vcpkg dependencies.'
}
New-Item -ItemType Directory -Force -Path $stagedTextures | Out-Null
Copy-Item -LiteralPath $ObjPath -Destination $stagedObj
foreach ($file in @('T_HardwoodTree_Canopy.png', 'T_HardwoodTree_Trunk.png',
                    'T_HardwoodTree_Leaf.png', 'T_HardwoodTree_Leaf_O.png')) {
    Copy-Item -LiteralPath (Join-Path $TexturesPath $file) -Destination $stagedTextures
}

$vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { throw 'Visual Studio C++ tools were not found.' }
& (Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -NoLogo

Push-Location -LiteralPath $output
try {
    $compilerArgs = @('/nologo', '/std:c++20', '/EHsc', '/utf-8', '/W4', '/O2', '/MD',
        "/I$vcpkg\include", $source, "/Fe:$executable", '/link', "/LIBPATH:$vcpkg\lib",
        'glfw3dll.lib', 'glew32.lib', 'opengl32.lib', 'windowscodecs.lib', 'ole32.lib')
    & cl.exe @compilerArgs
    if ($LASTEXITCODE -ne 0) { throw "Preview build failed with exit code $LASTEXITCODE." }
    Copy-Item -LiteralPath (Join-Path $vcpkg 'bin\glfw3.dll'),(Join-Path $vcpkg 'bin\glew32.dll') -Destination $output
    $previewArgs = @($stagedObj, $stagedTextures, $shaders)
    if ($Capture) { $previewArgs += $output }
    & $executable @previewArgs
    if ($LASTEXITCODE -ne 0) { throw "Preview failed with exit code $LASTEXITCODE." }
}
finally {
    Pop-Location
}
