<#
.SYNOPSIS
  Builds the FarsightVNC viewer (tvnviewer) with MSBuild.

.DESCRIPTION
  Locates MSBuild through vswhere, builds the requested configurations of the
  tvnviewer project from tightvnc2017.sln and writes one log per configuration.
  Requires Visual Studio with the MSVC v145 toolset and a Windows 10/11 SDK.

.EXAMPLE
  .\tools\build.ps1
  .\tools\build.ps1 -Configurations Release -Platforms x64 -Target Rebuild
#>
param(
  [string[]]$Configurations = @('Release', 'Debug'),
  [string[]]$Platforms = @('x64', 'Win32'),
  [ValidateSet('Build', 'Rebuild', 'Clean')]
  [string]$Target = 'Build',
  [string]$Project = 'tvnviewer',
  [string]$LogDir = (Join-Path $PSScriptRoot '..\build-logs')
)

$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$solution = Join-Path $repoRoot 'tightvnc2017.sln'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: $vswhere" }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
  -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found. Install Visual Studio with the C++ workload.' }

New-Item -ItemType Directory -Force $LogDir | Out-Null
$msbuildTarget = if ($Project) { "${Project}:$Target" } else { $Target }
$failed = @()

foreach ($configuration in $Configurations) {
  foreach ($platform in $Platforms) {
    $log = Join-Path $LogDir "$platform-$configuration.log"
    Write-Host "==> $configuration|$platform ($msbuildTarget)"
    & $msbuild $solution "/t:$msbuildTarget" "/p:Configuration=$configuration" `
      "/p:Platform=$platform" /m /nologo /v:minimal "/flp:logfile=$log;verbosity=normal"
    if ($LASTEXITCODE -ne 0) { $failed += "$configuration|$platform" }
  }
}

if ($failed.Count -gt 0) {
  Write-Error "Build failed for: $($failed -join ', '). See logs in $LogDir"
  exit 1
}
Write-Host 'All requested configurations built successfully.'
