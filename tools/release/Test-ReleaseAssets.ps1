<#
.SYNOPSIS
  Checks that a published GitHub release can be installed by the in-app updater.

.DESCRIPTION
  Read-only against GitHub (uses the gh CLI, which must be signed in or able to
  read the public repository). For the release -Tag of -Repository it checks:

    1. the three assets exist: FarSightVNC-v<version>.zip,
       farsightvnc-update.manifest and farsightvnc-update.manifest.sig;
    2. the signature (64 bytes, raw P1363) verifies over the exact manifest
       bytes with one of the public keys compiled into the viewer
       (updater-lib/TrustedKeys.cpp), using ECDsaCng / SHA-256;
    3. the manifest header is "FarSightVNC-Update-Manifest 1", its version
       equals the tag without the "v", and its package line names the zip
       asset with exactly the asset's size;
    4. with -DownloadZip, the SHA-256 of the downloaded zip equals the
       manifest's package hash.

  The manifest and the signature (and the zip with -DownloadZip) are
  downloaded into a new folder in %TEMP%, which is removed at the end. Nothing
  is uploaded or changed on GitHub, and no private key is read.

.PARAMETER Tag
  Release tag, for example v1.2.1a.

.PARAMETER Repository
  owner/name (default TheSampaio/FarSightVNC).

.PARAMETER DownloadZip
  Also download the zip and compare its SHA-256 with the manifest.

.PARAMETER LocalDirectory
  Test mode, no GitHub: check the three files in this folder instead of the
  release assets (sizes are the file sizes; the zip hash is always checked).
  Test-ReleaseScripts.ps1 uses it.

.PARAMETER PublicKeyPath
  Test mode: verify with this BCRYPT_ECCPUBLIC_BLOB file instead of the keys of
  updater-lib/TrustedKeys.cpp.

.EXAMPLE
  .\tools\release\Test-ReleaseAssets.ps1 -Tag v1.2.1a
#>
param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^v\d+\.\d+\.\d+[ab]?$')]
  [string]$Tag,
  [string]$Repository = 'TheSampaio/FarSightVNC',
  [switch]$DownloadZip,
  [string]$LocalDirectory,
  [string]$PublicKeyPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$version = $Tag.Substring(1)
$zipName = "FarSightVNC-v$version.zip"
$manifestName = 'farsightvnc-update.manifest'
$signatureName = 'farsightvnc-update.manifest.sig'
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('fsv-release-assets-' + [Guid]::NewGuid().ToString('N'))
$script:failures = 0

function Assert-That([bool]$Condition, [string]$Message) {
  if ($Condition) {
    Write-Host "  ok   $Message"
  } else {
    Write-Host "  FAIL $Message"
    $script:failures++
  }
}

# The public blobs of updater-lib/TrustedKeys.cpp (every "kKeyN[72] = {...};").
function Get-TrustedKeys {
  if ($PublicKeyPath) { return , @(, [System.IO.File]::ReadAllBytes($PublicKeyPath)) }
  $source = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'updater-lib\TrustedKeys.cpp'))
  $keys = @()
  foreach ($match in [regex]::Matches($source, 'kKey\w*\[72\]\s*=\s*\{([^}]*)\}')) {
    $bytes = [byte[]]@([regex]::Matches($match.Groups[1].Value, '0x([0-9a-fA-F]{2})') |
        ForEach-Object { [Convert]::ToByte($_.Groups[1].Value, 16) })
    if ($bytes.Length -eq 72) { $keys += , $bytes }
  }
  return , $keys
}

function Test-Signature([byte[]]$Data, [byte[]]$Signature, $Keys) {
  foreach ($blob in $Keys) {
    $key = [System.Security.Cryptography.CngKey]::Import($blob, [System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    $verifier = New-Object System.Security.Cryptography.ECDsaCng $key
    try {
      $verifier.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256
      if ($verifier.VerifyData($Data, $Signature)) { return $true }
    } finally {
      $verifier.Dispose()
      $key.Dispose()
    }
  }
  return $false
}

try {
  $previous = $ErrorActionPreference
  $assets = @{}
  if ($LocalDirectory) {
    Write-Host "Local assets in $LocalDirectory (test mode, no GitHub)"
    foreach ($file in Get-ChildItem -LiteralPath $LocalDirectory -File) {
      $assets[$file.Name] = [pscustomobject]@{ name = $file.Name; size = $file.Length }
    }
  } else {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'The gh CLI is required (https://cli.github.com).' }
    Write-Host "Release $Tag of $Repository"
    $ErrorActionPreference = 'Continue'
    $json = & gh release view $Tag --repo $Repository --json 'tagName,isDraft,isPrerelease,assets' 2>&1
    $viewExit = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($viewExit -ne 0) { throw "gh release view failed: $json" }
    $release = ($json | Out-String) | ConvertFrom-Json
    Assert-That (-not $release.isDraft) 'release is published (not a draft)'
    Assert-That ($release.isPrerelease -eq ($version -match '[ab]$')) 'pre-release flag matches the version channel'
    foreach ($asset in $release.assets) { $assets[$asset.name] = $asset }
  }
  foreach ($name in @($zipName, $manifestName, $signatureName)) {
    Assert-That $assets.ContainsKey($name) "asset $name exists"
  }
  if (-not ($assets.ContainsKey($manifestName) -and $assets.ContainsKey($signatureName) -and $assets.ContainsKey($zipName))) {
    throw 'missing assets; the viewer would offer only the releases page for this release'
  }

  New-Item -ItemType Directory -Force -Path $work | Out-Null
  if ($LocalDirectory) {
    $DownloadZip = $true
    foreach ($name in @($manifestName, $signatureName, $zipName)) {
      Copy-Item -LiteralPath (Join-Path $LocalDirectory $name) -Destination $work
    }
  } else {
    $patterns = @('--pattern', $manifestName, '--pattern', $signatureName)
    if ($DownloadZip) { $patterns += @('--pattern', $zipName) }
    $ErrorActionPreference = 'Continue'
    $downloadOutput = & gh release download $Tag --repo $Repository --dir $work @patterns 2>&1
    $downloadExit = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($downloadExit -ne 0) { throw "gh release download failed: $downloadOutput" }
  }

  $manifestBytes = [System.IO.File]::ReadAllBytes((Join-Path $work $manifestName))
  $signature = [System.IO.File]::ReadAllBytes((Join-Path $work $signatureName))
  Assert-That ($signature.Length -eq 64) 'signature is 64 bytes'

  $keys = Get-TrustedKeys
  Assert-That ($keys.Count -gt 0) "public keys to verify with ($($keys.Count))"
  $verified = ($signature.Length -eq 64) -and (Test-Signature $manifestBytes $signature $keys)
  Assert-That $verified 'signature verifies with a compiled-in key'

  $lines = [System.Text.Encoding]::UTF8.GetString($manifestBytes).TrimEnd("`n").Split("`n")
  Assert-That ($lines[0] -ceq 'FarSightVNC-Update-Manifest 1') 'manifest header'
  Assert-That ($lines[1] -ceq "version $version") "manifest version equals the tag ($version)"
  $package = $lines | Where-Object { $_ -like 'package *' } | Select-Object -First 1
  $parts = if ($package) { $package.Split(' ') } else { @() }
  Assert-That ($parts.Count -eq 4 -and $parts[1] -ceq $zipName) "manifest package is $zipName"
  $zipSize = [uint64]$assets[$zipName].size
  Assert-That ($parts.Count -eq 4 -and [uint64]$parts[2] -eq $zipSize) "manifest package size equals the zip asset size ($zipSize bytes)"

  if ($DownloadZip) {
    $hash = (Get-FileHash (Join-Path $work $zipName) -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-That ($parts.Count -eq 4 -and $parts[3] -ceq $hash) 'zip SHA-256 equals the manifest package hash'
  }
} catch {
  Write-Host "  FAIL $($_.Exception.Message)"
  $script:failures++
} finally {
  if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
}

if ($script:failures -gt 0) {
  Write-Host "Test-ReleaseAssets: $($script:failures) failure(s)"
  exit 1
}
Write-Host 'Test-ReleaseAssets: all checks passed'
exit 0
