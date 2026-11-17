<#
.SYNOPSIS
  Validates the flat x64 shipping payload and builds its per-user Inno installer.
.EXAMPLE
  .\tools\installer\Build-Installer.ps1 -ShippingDirectory .\build\shipping\FarSightVNC-v1.3.0a
.EXAMPLE
  .\tools\installer\Build-Installer.ps1 -ShippingDirectory C:\release\FarSightVNC-v1.3.0a -ValidateOnly
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$ShippingDirectory,
  [string]$CompilerPath,
  [string]$OutputDirectory,
  [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$payload = (Resolve-Path -LiteralPath $ShippingDirectory).Path
if (-not (Test-Path -LiteralPath $payload -PathType Container)) {
  throw 'ShippingDirectory must be a directory.'
}

# The allowlist is deliberate: a retained TightVNC server must never enter Setup.
$expected = @('fsvncviewer.exe', 'fsvncupdater.exe', 'LEIAME.txt', 'LICENSE.txt', 'README.txt')
$entries = @(Get-ChildItem -LiteralPath $payload -Force)
if ($entries.Count -ne $expected.Count) { throw 'ShippingDirectory must contain exactly the five flat shipping files.' }
foreach ($entry in $entries) {
  if ($entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
      $entry.Name -cnotin $expected -or $entry.Length -eq 0) {
    throw "Unexpected, linked or empty shipping entry: $($entry.Name)"
  }
}

function Assert-X64Executable([string]$Path) {
  $stream = [IO.File]::OpenRead($Path)
  $reader = New-Object IO.BinaryReader($stream)
  try {
    if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE executable: $Path" }
    $stream.Position = 0x3c
    $peOffset = $reader.ReadUInt32()
    if ($peOffset -gt $stream.Length - 6) { throw "Invalid PE header: $Path" }
    $stream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
      throw "Expected an x64 executable: $Path"
    }
  } finally {
    $reader.Dispose()
  }
}

$viewerPath = Join-Path $payload 'fsvncviewer.exe'
$updaterPath = Join-Path $payload 'fsvncupdater.exe'
Assert-X64Executable $viewerPath
Assert-X64Executable $updaterPath
$viewer = [Diagnostics.FileVersionInfo]::GetVersionInfo($viewerPath)
$updater = [Diagnostics.FileVersionInfo]::GetVersionInfo($updaterPath)
$version = $viewer.FileVersion
if ($version -cnotmatch '^\d+\.\d+\.\d+[ab]?$') { throw "Unsupported viewer version: $version" }
if ($updater.FileVersion -cne $version) { throw 'Viewer and updater versions must match.' }
$numeric = '{0}.{1}.{2}.{3}' -f $viewer.FileMajorPart, $viewer.FileMinorPart, $viewer.FileBuildPart, $viewer.FilePrivatePart
if ($numeric -cne (($version -replace '[ab]$', '') + '.0')) {
  throw 'Viewer numeric and textual versions do not match.'
}
$updaterNumeric = '{0}.{1}.{2}.{3}' -f $updater.FileMajorPart, $updater.FileMinorPart, $updater.FileBuildPart, $updater.FilePrivatePart
if ($updaterNumeric -cne $numeric) { throw 'Viewer and updater numeric versions must match.' }
Write-Host "Validated FarSightVNC v$version x64 viewer-only payload."
if ($ValidateOnly) { return }

if (-not $CompilerPath) {
  $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if ($command) { $CompilerPath = $command.Source }
}
if (-not $CompilerPath) {
  foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, (Join-Path $env:LOCALAPPDATA 'Programs'))) {
    foreach ($major in @('7', '6')) {
      $candidate = Join-Path $base "Inno Setup $major\ISCC.exe"
      if (Test-Path -LiteralPath $candidate -PathType Leaf) { $CompilerPath = $candidate; break }
    }
    if ($CompilerPath) { break }
  }
}
if (-not $CompilerPath -or -not (Test-Path -LiteralPath $CompilerPath -PathType Leaf)) {
  throw 'Inno Setup 6.3+ compiler (ISCC.exe) is required. Install it separately or pass -CompilerPath.'
}
$compiler = (Resolve-Path -LiteralPath $CompilerPath).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build\installer' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$payloadRoot = $payload.TrimEnd('\')
if ($output.TrimEnd('\').Equals($payloadRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($payloadRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Installer output must stay outside the flat shipping directory.'
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $compiler "/DPayloadDir=$payload" "/DAppVersion=$version" "/DNumericVersion=$numeric" "/DInstallerOutputDir=$output" (Join-Path $PSScriptRoot 'FarSightVNC.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed (exit $LASTEXITCODE)." }
$installer = Join-Path $output "FarSightVNC-v$version-Setup-x64.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'Compiler succeeded but the installer was not found.' }
Get-Item -LiteralPath $installer | Select-Object FullName, Length
Get-FileHash -LiteralPath $installer -Algorithm SHA256
