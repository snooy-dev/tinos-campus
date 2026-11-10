param(
    [string]$VisualStudioRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$artifacts = Join-Path $repo 'build/serialization-probe'
$vcpkg = Join-Path $VisualStudioRoot 'VC/vcpkg/vcpkg.exe'
$msbuild = Join-Path $VisualStudioRoot 'MSBuild/Current/Bin/MSBuild.exe'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
& $vcpkg install --triplet x64-windows "--x-manifest-root=$PSScriptRoot" "--x-install-root=$artifacts/vcpkg_installed" *> (Join-Path $artifacts 'dependencies.log')
if ($LASTEXITCODE -ne 0) { throw "Dependency install failed: $artifacts/dependencies.log" }
foreach ($configuration in @('Debug', 'Release')) {
    $buildLog = Join-Path $artifacts "$($configuration.ToLower())-build.log"
    & $msbuild (Join-Path $PSScriptRoot 'probe.vcxproj') /t:Rebuild "/p:Configuration=$configuration" /p:Platform=x64 /v:minimal /nologo *> $buildLog
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $buildLog" }
    $runLog = Join-Path $artifacts "$($configuration.ToLower())-run.log"
    & (Join-Path $artifacts "$configuration/SerializationProbe.exe") *> $runLog
    if ($LASTEXITCODE -ne 0) { throw "Probe failed: $runLog" }
    Write-Output "$configuration passed. Logs: $artifacts"
}
