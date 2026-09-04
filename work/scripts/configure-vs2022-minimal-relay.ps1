param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = 'build-node-relay-vs2022',
    [switch]$BuildTests,
    [switch]$BuildFuzz
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer helper not found: $vswhere"
}

$vsRoot = (& $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vsRoot) {
    throw 'Visual Studio with the C++ x64 toolchain was not found.'
}

$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$vcpkgRoot = Join-Path $vsRoot 'VC\vcpkg'
$toolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
foreach ($requiredPath in @($cmake, $toolchain)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required VS2022 build tool not found: $requiredPath"
    }
}

$buildDir = Join-Path $repoRoot $BuildDirectory
$cacheRoot = Join-Path $repoRoot '.tmp'
$env:VCPKG_VISUAL_STUDIO_PATH = $vsRoot
$env:VCPKG_ROOT = $vcpkgRoot
$env:VCPKG_DOWNLOADS = Join-Path $cacheRoot 'vcpkg-downloads'
$env:X_VCPKG_REGISTRIES_CACHE = Join-Path $cacheRoot 'vcpkg-registries-vs2022'
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $cacheRoot 'vcpkg-bincache'
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'http.version'
$env:GIT_CONFIG_VALUE_0 = 'HTTP/1.1'
$manifestFeatures = if ($BuildTests -or $BuildFuzz) { 'tests' } else { '' }
$buildTestsValue = if ($BuildTests) { 'ON' } else { 'OFF' }
$buildFuzzValue = if ($BuildFuzz) { 'ON' } else { 'OFF' }

New-Item -ItemType Directory -Force -Path `
    $env:VCPKG_DOWNLOADS, `
    $env:X_VCPKG_REGISTRIES_CACHE, `
    $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null

$cleanPath = @(Split-Path -Parent $cmake)
$cachedPwsh = Get-ChildItem -Path (Join-Path $env:VCPKG_DOWNLOADS 'tools\powershell-core-*-windows\pwsh.exe') -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($cachedPwsh) {
    $cleanPath += $cachedPwsh.DirectoryName
    $env:VCPKG_FORCE_SYSTEM_BINARIES = '1'
} else {
    $env:VCPKG_FORCE_SYSTEM_BINARIES = $null
}
$env:CODEX_CLEAN_PATH_PREPEND = $cleanPath -join [IO.Path]::PathSeparator

Set-Location -LiteralPath $repoRoot
$cleanEnvRunner = Join-Path $PSScriptRoot 'run-with-clean-windows-env.py'
& python $cleanEnvRunner $cmake `
    -S . `
    -B $buildDir `
    -G 'Visual Studio 17 2022' `
    -A x64 `
    -T v143 `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
    "-DVCPKG_MANIFEST_FEATURES=$manifestFeatures" `
    -DBUILD_BITCOIN_BIN=OFF `
    -DBUILD_DAEMON=ON `
    -DBUILD_GUI=OFF `
    -DBUILD_CLI=OFF `
    "-DBUILD_TESTS=$buildTestsValue" `
    -DBUILD_TX=OFF `
    -DBUILD_UTIL=OFF `
    -DBUILD_UTIL_CHAINSTATE=OFF `
    -DBUILD_KERNEL_LIB=OFF `
    -DBUILD_KERNEL_TEST=OFF `
    -DBUILD_WALLET_TOOL=OFF `
    -DBUILD_GUI_TESTS=OFF `
    -DBUILD_BENCH=OFF `
    "-DBUILD_FUZZ_BINARY=$buildFuzzValue" `
    -DENABLE_WALLET=OFF `
    -DENABLE_EXTERNAL_SIGNER=OFF `
    -DENABLE_IPC=OFF `
    -DWITH_ZMQ=OFF `
    -DWITH_EMBEDDED_ASMAP=ON `
    -DWITH_QRENCODE=OFF `
    -DWITH_DBUS=OFF `
    -DWITH_USDT=OFF `
    -DWITH_CCACHE=OFF `
    -DINSTALL_MAN=OFF

if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}

Write-Host "Configured $BuildDirectory for $Configuration|x64."
