# 构建 Windows Release 并组装 dist/Bookkeeping/（供 Inno Setup 打包）
# 用法: .\PC\scripts\build-release.ps1 [-VcpkgRoot C:\vcpkg] [-BuildDir build-win]

param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$BuildDir = "build-win",
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
)

$ErrorActionPreference = "Stop"
$PcDir = Join-Path $RepoRoot "PC"
$DistDir = Join-Path $RepoRoot "dist\Bookkeeping"
$BuildPath = Join-Path $RepoRoot $BuildDir

Write-Host "==> 仓库根目录: $RepoRoot"
Write-Host "==> 构建目录:   $BuildPath"
Write-Host "==> 输出目录:   $DistDir"

if (-not (Test-Path (Join-Path $PcDir "libs\httplib.h"))) {
    Write-Error "缺少 PC\libs\httplib.h，请先按 README 下载依赖头文件"
}

$cmakeArgs = @(
    "-S", $PcDir,
    "-B", $BuildPath,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_BUILD_TYPE=Release"
)
if ($VcpkgRoot -and (Test-Path $VcpkgRoot)) {
    $toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    Write-Host "==> 使用 vcpkg: $VcpkgRoot"
} else {
    Write-Warning "未设置 VCPKG_ROOT，请确保 SQLite3/ZLIB 可被 CMake 找到"
}

cmake @cmakeArgs
cmake --build $BuildPath --config Release -j

$ReleaseDir = Join-Path $BuildPath "Release"
$ExeSrc = Join-Path $ReleaseDir "bookkeeping.exe"
if (-not (Test-Path $ExeSrc)) {
    Write-Error "未找到 $ExeSrc，请检查构建是否成功"
}

if (Test-Path $DistDir) { Remove-Item $DistDir -Recurse -Force }
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

Copy-Item $ExeSrc (Join-Path $DistDir "bookkeeping-server.exe")
Copy-Item (Join-Path $PcDir "categories.json") $DistDir
Copy-Item (Join-Path $PcDir "frontend") (Join-Path $DistDir "frontend") -Recurse
Copy-Item (Join-Path $PcDir "packaging\launcher\启动个人记账.bat") $DistDir

$license = Join-Path $RepoRoot "LICENSE"
if (Test-Path $license) { Copy-Item $license $DistDir }

Write-Host ""
Write-Host "==> 已生成安装 payload: $DistDir"
Write-Host "    下一步: 用 Inno Setup 编译 PC\packaging\bookkeeping.iss"
Write-Host "    或先测试: cd dist\Bookkeeping && .\启动个人记账.bat"
