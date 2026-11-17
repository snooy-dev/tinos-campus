# Build engine/engine.vcxproj with /p:EngineTests=true before running this script.
param([Parameter(Mandatory)][string]$BinDirectory)

$ErrorActionPreference = 'Stop'
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
$stage = Join-Path $tempRoot ("tinos-startup-" + [Guid]::NewGuid())
$process = $null
try {
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -LiteralPath (Join-Path $bin 'engine_startup_tests.exe') -Destination $stage
    Copy-Item -Path (Join-Path $bin '*.dll') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $bin 'content') -Destination $stage -Recurse
    $log = Join-Path $stage 'result.log'
    $process = Start-Process -FilePath (Join-Path $stage 'engine_startup_tests.exe') `
        -ArgumentList '--isolated-fixture' -WorkingDirectory $tempRoot -PassThru `
        -WindowStyle Hidden -RedirectStandardOutput $log
    if (!$process.WaitForExit(30000)) { throw 'Engine startup test exceeded 30 seconds.' }
    Get-Content -LiteralPath $log
    if ($process.ExitCode -ne 0) { throw "Engine startup test exited with code $($process.ExitCode)." }
} finally {
    if ($process -and !$process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    if (Test-Path -LiteralPath $stage) {
        $resolved = (Resolve-Path -LiteralPath $stage).Path
        if ($resolved -eq $stage -and (Split-Path -Parent $resolved) -eq $tempRoot) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
