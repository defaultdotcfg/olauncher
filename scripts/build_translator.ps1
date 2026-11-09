# Packages the shader translator (scripts/translator/*.py) as translator\olauncher-translator.exe
# (PyInstaller, one folder) together with DXC (dxc.exe / dxcompiler.dll / dxil.dll).
# The launcher runs it while "Preparando shaders" is shown, so players need neither Python nor the SDK.
param(
    [string]$Src = "",          # folder with translate_all.py and the other translator modules
    [string]$Out = "",          # output folder (gets translator\)
    [string]$DxcDir = ""        # folder with dxc.exe/dxcompiler.dll/dxil.dll (LICENSE-*.txt two levels up, as in the
                                # official DirectXShaderCompiler release zip); default: newest Windows SDK
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Src) {
    $Src = Join-Path $PSScriptRoot "translator"
    if (-not (Test-Path (Join-Path $Src "translate_all.py"))) { $Src = Join-Path $PSScriptRoot "diag" }
}
if (-not $Out) { $Out = Join-Path $root "bin" }
$Src = (Resolve-Path $Src).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path

# PyInstaller works in a clean temp copy: scripts\diag has helpers (dis.py, ...) that shadow stdlib modules
$work = Join-Path ([IO.Path]::GetTempPath()) ("olt_" + [guid]::NewGuid().ToString("N"))
$srcCopy = Join-Path $work "src"
New-Item -ItemType Directory -Force $srcCopy | Out-Null
foreach ($f in "translate_all.py", "build_shader_cache.py", "tess_bypass.py", "compile_cso.py",
               "gcn2hlsl.py", "gcn_dis.py", "hlslc.py", "rootsig.py", "shader_patches.json") {
    Copy-Item (Join-Path $Src $f) $srcCopy
}

$py = (Get-Command python -ErrorAction Stop).Source
Push-Location $work
try {
    & $py -I -m PyInstaller --noconfirm --clean --onedir --console --name olauncher-translator `
        --distpath (Join-Path $work "dist") --workpath (Join-Path $work "build") --specpath $work `
        --paths $srcCopy `
        --hidden-import build_shader_cache --hidden-import tess_bypass --hidden-import compile_cso `
        --hidden-import gcn2hlsl --hidden-import gcn_dis --hidden-import hlslc --hidden-import rootsig `
        --add-data "$(Join-Path $srcCopy 'shader_patches.json');." `
        --exclude-module tkinter --exclude-module unittest --exclude-module pydoc `
        (Join-Path $srcCopy "translate_all.py")
    if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed" }
} finally { Pop-Location }

$dest = Join-Path $Out "translator"
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
Copy-Item -Recurse (Join-Path $work "dist\olauncher-translator") $dest

# DXC (Shader Model 6 / DXIL)
if ($DxcDir) { $dxc = (Resolve-Path $DxcDir).Path }
else {
    $kits = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    $dxc = Get-ChildItem $kits -Directory -Filter "10.*" | Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "x64" } | Where-Object { Test-Path (Join-Path $_ "dxc.exe") } | Select-Object -First 1
    if (-not $dxc) { throw "dxc.exe not found in the Windows SDK ($kits)" }
}
foreach ($f in "dxc.exe", "dxcompiler.dll", "dxil.dll") { Copy-Item (Join-Path $dxc $f) $dest }

# licenses of what the translator folder redistributes
$lic = Join-Path $dest "licenses"
New-Item -ItemType Directory -Force $lic | Out-Null
$dxcRoot = Split-Path -Parent (Split-Path -Parent $dxc)
Get-ChildItem $dxcRoot -Filter "LICENSE-*.txt" -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item $_.FullName (Join-Path $lic ("DirectXShaderCompiler-" + $_.Name)) }
$pyLic = & $py -I -c "import sys, os; print(os.path.join(sys.base_prefix, 'LICENSE.txt'))"
if (Test-Path $pyLic) { Copy-Item $pyLic (Join-Path $lic "Python-LICENSE.txt") }

Remove-Item -Recurse -Force $work
Write-Host "translator -> $dest (dxc from $dxc)"
