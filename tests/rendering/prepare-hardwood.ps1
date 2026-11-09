param(
    [string]$ObjPath = (Join-Path $env:USERPROFILE 'Downloads\Hardwood.obj'),
    [string]$TexturesPath = (Join-Path $env:USERPROFILE 'AppData\Local\Temp')
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$output = Join-Path $repo 'build\lit-tree-preview'
$stagedTextures = Join-Path $output 'Textures'

$material = Get-Content -Raw -Encoding UTF8 -LiteralPath ([IO.Path]::ChangeExtension($ObjPath, '.mtl'))
$material = $material -replace '(?m)^(newmtl Oak_leaves_[^\r\n]*)',
    ('$1' + [Environment]::NewLine + 'map_d Textures/T_HardwoodTree_Leaf_O.png')
New-Item -ItemType Directory -Force -Path $stagedTextures | Out-Null
Copy-Item -LiteralPath $ObjPath -Destination (Join-Path $output 'Hardwood.obj')
[IO.File]::WriteAllText((Join-Path $output 'Hardwood.mtl'), $material)
foreach ($file in @('T_HardwoodTree_Canopy.png', 'T_HardwoodTree_Trunk.png',
                    'T_HardwoodTree_Leaf.png', 'T_HardwoodTree_Leaf_O.png')) {
    Copy-Item -LiteralPath (Join-Path $TexturesPath $file) -Destination $stagedTextures
}
