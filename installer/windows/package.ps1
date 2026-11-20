# Baut aus einem fertigen Release-Build das Windows-Paket:
#   dist\LoggX Studio\                       (lauffähiger Ordner)
#   dist\LoggX-Studio-<ver>-Windows-portable.zip
#   dist\LoggX-Studio-<ver>-Windows-Setup.exe  (wenn Inno Setup vorhanden ist)
#
# Aufruf (aus dem Repository-Ordner):
#   .\installer\windows\package.ps1 -BuildDir build -QtDir C:\Qt\6.7.3\msvc2019_64 `
#       -VcpkgInstalled C:\vcpkg\installed\x64-windows
param(
    [string]$BuildDir = "build",
    [Parameter(Mandatory = $true)][string]$QtDir,
    [Parameter(Mandatory = $true)][string]$VcpkgInstalled,
    [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(LoggXStudio VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$dist = Join-Path $root "dist"
$app = Join-Path $dist "LoggX Studio"
Write-Host "LoggX Studio $version -> $app"

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $app | Out-Null

# 1. Programme und die von vcpkg mitkopierten DLLs (FFmpeg, Rubber Band, ...)
$appBin = Join-Path $BuildDir "app\$Config"
Copy-Item (Join-Path $appBin "LoggX Studio.exe") $app
Copy-Item (Join-Path $appBin "*.dll") $app
Copy-Item (Join-Path $BuildDir "$Config\loggx-render.exe") $app
Copy-Item (Join-Path $BuildDir "$Config\*.dll") $app -ErrorAction SilentlyContinue

# 2. Qt-Bibliotheken, Plugins und QML-Module
& (Join-Path $QtDir "bin\windeployqt.exe") --release --qmldir app\qml --no-translations `
    --no-system-d3d-compiler --no-opengl-sw (Join-Path $app "LoggX Studio.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt ist fehlgeschlagen" }

# 3. Lizenztexte der mitgelieferten Bibliotheken
$licenses = Join-Path $app "Lizenzen"
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
Get-ChildItem (Join-Path $VcpkgInstalled "share") -Directory |
    Where-Object { $_.Name -notlike "vcpkg-*" -and (Test-Path (Join-Path $_.FullName "copyright")) } |
    ForEach-Object { Copy-Item (Join-Path $_.FullName "copyright") (Join-Path $licenses "$($_.Name).txt") }
foreach ($f in @("LICENSE.LGPLv3", "LICENSE.GPL3", "LICENSE.GPL3-EXCEPT")) {
    $src = Join-Path $QtDir "..\..\Licenses\$f"
    if (Test-Path $src) { Copy-Item $src (Join-Path $licenses "Qt-$f.txt") }
}

if ($env:ONNXRUNTIME_DIR) {
    foreach ($f in @("LICENSE", "ThirdPartyNotices.txt")) {
        $src = Join-Path $env:ONNXRUNTIME_DIR $f
        if (Test-Path $src) { Copy-Item $src (Join-Path $licenses "onnxruntime-$f.txt") }
    }
}
foreach ($f in @("third_party\pocketfft\LICENSE.md")) {
    if (Test-Path $f) { Copy-Item $f (Join-Path $licenses "pocketfft.txt") }
}
# Plugin-Schnittstellen (MIT)
Copy-Item "third_party\clap\LICENSE" (Join-Path $licenses "CLAP.txt") -ErrorAction SilentlyContinue
Copy-Item "third_party\vst3\LICENSE.txt" (Join-Path $licenses "VST3-pluginterfaces.txt") -ErrorAction SilentlyContinue

# 3b. Vollversion: KI-Modelle (MIT, kuielab MDX-Net) gleich mitliefern – kein Download nötig
if ($env:LOGGX_MODELS_DIR -and (Test-Path $env:LOGGX_MODELS_DIR)) {
    $models = Join-Path $app "models"
    New-Item -ItemType Directory -Force -Path $models | Out-Null
    Copy-Item (Join-Path $env:LOGGX_MODELS_DIR "*.onnx") $models
    Copy-Item "third_party\models\LICENSE-kuielab-mdx-net.txt" (Join-Path $licenses "KI-Modelle-kuielab-mdx-net.txt")
    Copy-Item "third_party\models\LICENSE-u2net.txt" (Join-Path $licenses "KI-Modell-U2-Net-Apache-2.0.txt")
    Write-Host "KI-Modelle mitgeliefert: $((Get-ChildItem $models).Count)"
}

# 4. Portables ZIP
$zip = Join-Path $dist "LXStudio-$version-Windows-portable.zip"
Compress-Archive -Path $app -DestinationPath $zip
Write-Host "ZIP: $zip"

# 5. Installer mit Inno Setup
$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) {
    & $iscc /Qp "/DAppVersion=$version" "/DSourceDir=$app" "/DOutputDir=$dist" installer\windows\LoggXStudio.iss
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup ist fehlgeschlagen" }
} else {
    Write-Warning "Inno Setup nicht gefunden - nur das ZIP wurde erstellt."
}

"version=$version" | Out-File -FilePath (Join-Path $dist "version.txt") -Encoding ascii
Get-ChildItem $dist -File | Format-Table Name, Length
