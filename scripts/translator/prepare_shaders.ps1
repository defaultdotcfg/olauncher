# prepare_shaders.ps1 - builds <GameDir>\shaders\translated from the console shaders in <GameDir>\shaders
# (extracted from the game's packs by the launcher, or dumped by the bridge) with translate_all.py: one parallel
# pass that translates every VS/PS/CS, compiles it with dxc and builds the tessellation pairs (tess_*.dxil.cso).
# Needs Python 3 and the Windows SDK (dxc.exe; or set $env:DXC). The launcher runs the packaged translator instead.
param(
    [string]$GameDir = (Join-Path $PSScriptRoot "..\..\dist_game"),
    [switch]$Clean
)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$GameDir = (Resolve-Path $GameDir).Path
$shaders = Join-Path $GameDir "shaders"
$tr = Join-Path $shaders "translated"
if (-not (Test-Path $shaders)) { throw "No shaders in $shaders - press JOGAR in the launcher once (it reads them from the game's packs)." }
if ($Clean -and (Test-Path $tr)) { Remove-Item "$tr\*" -Force }
python -I "$here\translate_all.py" $GameDir
if ($LASTEXITCODE) { throw "translate_all.py failed" }
