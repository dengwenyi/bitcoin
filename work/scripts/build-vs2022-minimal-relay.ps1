param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = 'build-node-relay-vs2022',
    [string]$Target
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer helper not found: $vswhere"
}

$vsRoot = (& $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'
$solution = Join-Path (Join-Path $repoRoot $BuildDirectory) 'BitcoinCore.sln'
foreach ($requiredPath in @($msbuild, $solution)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required build input not found: $requiredPath"
    }
}

# Some automation hosts expose both Path and PATH. MSBuild then fails while
# copying the environment to CL.exe. Normalize the child environment only; do
# not change the user's process or persistent configuration.
$cleanEnvRunner = Join-Path $PSScriptRoot 'run-with-clean-windows-env.py'
$targetArgument = if ($Target) { "/t:$Target" } else { '/t:Build' }
Set-Location -LiteralPath $repoRoot
& python $cleanEnvRunner $msbuild $solution $targetArgument /m:1 "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE"
}

$artifactName = if ($Target) { "$Target.exe" } else { 'bitcoind.exe' }
$binary = Join-Path (Join-Path (Join-Path $repoRoot $BuildDirectory) 'bin') "$Configuration\$artifactName"
if (-not (Test-Path -LiteralPath $binary)) {
    throw "Expected build artifact was not produced: $binary"
}
Write-Host "Built $binary"
