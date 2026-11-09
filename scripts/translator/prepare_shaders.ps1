# prepare_shaders.ps1 - builds <GameDir>\shaders\translated from the shaders the game dumped while running
#   1. build_shader_cache.py : translates every dumped GCN VS/PS (shaders\pso_*.dxbc) to HLSL
#   2. tess_bypass.py        : folds LS+DS of tessellated draws into one VS (shaders\tess\*.dxbc -> tess_*.dxil.cso)
#   3. compile_cso.py        : compiles the HLSL to .cso / .dxil.cso
# Needs Python 3 and the Windows SDK (dxc.exe; or set $env:DXC). SM5.1 uses Windows' own D3DCompiler_47.dll.
param(
    [string]$GameDir = (Join-Path $PSScriptRoot "..\..\dist_game"),
    [switch]$Clean
)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$GameDir = (Resolve-Path $GameDir).Path
$shaders = Join-Path $GameDir "shaders"
$tr = Join-Path $shaders "translated"
if (-not (Test-Path $shaders)) { throw "No dumped shaders in $shaders - run the game once first (it dumps the shaders it uses)." }
if ($Clean -and (Test-Path $tr)) { Remove-Item "$tr\*" -Force }
$env:NOCHECK = "1"
try { python -I "$here\build_shader_cache.py" $shaders } finally { Remove-Item Env:\NOCHECK -ErrorAction SilentlyContinue }
if ($LASTEXITCODE) { throw "build_shader_cache.py failed" }
python -I "$here\tess_bypass.py" $GameDir
if ($LASTEXITCODE) { throw "tess_bypass.py failed" }
python -I "$here\compile_cso.py" $tr
if ($LASTEXITCODE) { throw "compile_cso.py failed" }
