<#
.SYNOPSIS
  Creates and signs the FarSightVNC update manifest for a release.

.DESCRIPTION
  Lists every file under -ShippingDirectory (relative '/' paths, ordinal
  order) with size and SHA-256, adds the package (-ZipPath), writes
  farsightvnc-update.manifest (UTF-8, no BOM, LF) and signs the exact bytes
  with ECDSA P-256 / SHA-256 (raw 64-byte IEEE P1363 signature) into
  farsightvnc-update.manifest.sig.

  Before finishing it verifies the signature with the public key derived from
  the private key, and verifies that the zip contains exactly the listed files
  with identical sizes and hashes. Any mismatch exits with code 1. The
  private key is only read, never printed or copied.

  Grammar: see updater-lib/Manifest.h.

.PARAMETER ShippingDirectory
  The folder that was zipped (x64\, Win32\, LICENSE.txt, README.txt, ...).

.PARAMETER ZipPath
  The release zip, for example FarSightVNC-v0.3.0a.zip.

.PARAMETER Version
  Release version without a leading 'v', for example 0.3.0a.

.PARAMETER PrivateKeyPath
  update-key-<Id>.private.blob from New-UpdateSigningKey.ps1.

.PARAMETER OutDirectory
  Where the manifest and signature are written (default: the zip's folder).

.EXAMPLE
  .\tools\release\New-UpdateManifest.ps1 -ShippingDirectory $env:USERPROFILE\Desktop\FarSightVNC-v0.3.0a `
    -ZipPath $env:USERPROFILE\Desktop\FarSightVNC-v0.3.0a.zip -Version 0.3.0a `
    -PrivateKeyPath $env:USERPROFILE\.farsightvnc-signing\update-key-1.private.blob
#>
param(
  [Parameter(Mandatory = $true)][string]$ShippingDirectory,
  [Parameter(Mandatory = $true)][string]$ZipPath,
  [Parameter(Mandatory = $true)][string]$Version,
  [Parameter(Mandatory = $true)][string]$PrivateKeyPath,
  [string]$OutDirectory
)

$ErrorActionPreference = 'Stop'

$MaxPackageBytes = 128MB
$MaxFileBytes = 64MB
$MaxFiles = 64
$ManifestName = 'farsightvnc-update.manifest'
$SignatureName = 'farsightvnc-update.manifest.sig'

# Same rules as Version::parse (no leading 'v' here).
function Test-VersionText([string]$Text) {
  return $Text -cmatch '^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})[ab]?$'
}

# Mirrors isSafeRelativePath in updater-lib/Manifest.cpp.
function Test-SafeSegment([string]$Segment) {
  if ($Segment.Length -eq 0 -or $Segment.StartsWith('.') -or $Segment.EndsWith('.')) { return $false }
  if ($Segment -cnotmatch '^[A-Za-z0-9._-]+$') { return $false }
  $base = $Segment.Split('.')[0].ToLowerInvariant()
  if ($base -in @('con', 'prn', 'aux', 'nul')) { return $false }
  if ($base -match '^(com|lpt)[1-9]$') { return $false }
  return $true
}

function Test-SafeRelativePath([string]$Path) {
  if ($Path.Length -eq 0 -or $Path.Length -gt 200) { return $false }
  $segments = $Path.Split('/')
  if ($segments.Count -gt 4) { return $false }
  foreach ($segment in $segments) {
    if (-not (Test-SafeSegment $segment)) { return $false }
  }
  return $true
}

function Get-StreamHash([System.IO.Stream]$Stream) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    return ([System.BitConverter]::ToString($sha.ComputeHash($Stream)) -replace '-', '').ToLowerInvariant()
  } finally {
    $sha.Dispose()
  }
}

function Get-FileSha256([string]$Path) {
  $stream = [System.IO.File]::OpenRead($Path)
  try { return Get-StreamHash $stream } finally { $stream.Dispose() }
}

function Fail([string]$Message) { throw $Message }

try {
  if (-not (Test-VersionText $Version)) { Fail "Invalid version '$Version' (expected MAJOR.MINOR.FIX[a|b], no leading 'v')." }
  if (-not (Test-Path -LiteralPath $ShippingDirectory -PathType Container)) { Fail "ShippingDirectory not found: $ShippingDirectory" }
  if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) { Fail "ZipPath not found: $ZipPath" }
  if (-not (Test-Path -LiteralPath $PrivateKeyPath -PathType Leaf)) { Fail "PrivateKeyPath not found: $PrivateKeyPath" }

  $root = (Resolve-Path -LiteralPath $ShippingDirectory).Path.TrimEnd('\')
  $zipFull = (Resolve-Path -LiteralPath $ZipPath).Path
  if (-not $OutDirectory) { $OutDirectory = Split-Path -Parent $zipFull }
  New-Item -ItemType Directory -Force -Path $OutDirectory | Out-Null
  $outFull = (Resolve-Path -LiteralPath $OutDirectory).Path

  $packageName = [System.IO.Path]::GetFileName($zipFull)
  if (-not $packageName.EndsWith('.zip', [System.StringComparison]::OrdinalIgnoreCase) -or
      -not (Test-SafeSegment $packageName)) {
    Fail "Package file name '$packageName' is not a safe '*.zip' name."
  }
  $packageSize = (Get-Item -LiteralPath $zipFull).Length
  if ($packageSize -gt $MaxPackageBytes) { Fail "Package is larger than $MaxPackageBytes bytes." }

  # --- enumerate the shipping directory
  $files = @{}   # relative path -> @{ Size; Hash }
  foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -File -Force) {
    $relative = $item.FullName.Substring($root.Length + 1).Replace('\', '/')
    if (-not (Test-SafeRelativePath $relative)) { Fail "Path is not allowed in an update manifest: $relative" }
    if ($item.Length -gt $MaxFileBytes) { Fail "File is larger than $MaxFileBytes bytes: $relative" }
    if ($files.ContainsKey($relative.ToLowerInvariant())) { Fail "Paths differ only by case: $relative" }
    $files[$relative.ToLowerInvariant()] = @{
      Path = $relative
      Size = $item.Length
      Hash = (Get-FileSha256 $item.FullName)
    }
  }
  if ($files.Count -eq 0) { Fail "ShippingDirectory contains no files." }
  if ($files.Count -gt $MaxFiles) { Fail "ShippingDirectory has $($files.Count) files (maximum $MaxFiles)." }
  $paths = [string[]]($files.Values | ForEach-Object { $_.Path })
  [System.Array]::Sort($paths, [System.StringComparer]::Ordinal)

  # --- manifest text (LF, UTF-8 without BOM, final newline)
  $lines = New-Object System.Collections.Generic.List[string]
  $lines.Add('FarSightVNC-Update-Manifest 1')
  $lines.Add("version $Version")
  $lines.Add("package $packageName $packageSize $(Get-FileSha256 $zipFull)")
  foreach ($path in $paths) {
    $entry = $files[$path.ToLowerInvariant()]
    $lines.Add("file $($entry.Path) $($entry.Size) $($entry.Hash)")
  }
  $text = ($lines -join "`n") + "`n"
  $manifestBytes = (New-Object System.Text.UTF8Encoding $false).GetBytes($text)
  if ($manifestBytes.Length -gt 64KB) { Fail "Manifest is larger than 64 KiB." }

  # --- sign
  $privateBlob = [System.IO.File]::ReadAllBytes($PrivateKeyPath)
  $privateKey = [System.Security.Cryptography.CngKey]::Import($privateBlob, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
  $signer = New-Object System.Security.Cryptography.ECDsaCng $privateKey
  $signer.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256
  $signature = $signer.SignData($manifestBytes)
  if ($signature.Length -ne 64) { Fail "Unexpected signature length $($signature.Length)." }

  # --- self-verify with the public half of the same key
  $publicBlob = $privateKey.Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
  $publicKey = [System.Security.Cryptography.CngKey]::Import($publicBlob, [System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
  $verifier = New-Object System.Security.Cryptography.ECDsaCng $publicKey
  $verifier.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256
  if (-not $verifier.VerifyData($manifestBytes, $signature)) { Fail "Self-verification of the signature failed." }
  $tampered = [byte[]]$manifestBytes.Clone()
  $tampered[0] = $tampered[0] -bxor 1
  if ($verifier.VerifyData($tampered, $signature)) { Fail "Self-verification accepted a tampered manifest." }
  $signer.Dispose(); $verifier.Dispose(); $privateKey.Dispose(); $publicKey.Dispose()

  # --- the zip must contain exactly the same files with identical content
  Add-Type -AssemblyName System.IO.Compression
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archive = [System.IO.Compression.ZipFile]::OpenRead($zipFull)
  try {
    $seen = @{}
    foreach ($zipEntry in $archive.Entries) {
      $name = $zipEntry.FullName.Replace('\', '/')
      if ($name.EndsWith('/')) { continue }   # directory entry
      $key = $name.ToLowerInvariant()
      if ($seen.ContainsKey($key)) { Fail "Zip contains '$name' more than once." }
      $seen[$key] = $true
      if (-not $files.ContainsKey($key)) { Fail "Zip contains a file that is not in the shipping directory: $name" }
      $expected = $files[$key]
      if ($expected.Path -cne $name) { Fail "Zip entry '$name' differs in case from '$($expected.Path)'." }
      if ($zipEntry.Length -ne $expected.Size) { Fail "Size mismatch for '$name' in the zip." }
      $stream = $zipEntry.Open()
      try { $actual = Get-StreamHash $stream } finally { $stream.Dispose() }
      if ($actual -ne $expected.Hash) { Fail "Content mismatch for '$name' between the zip and the shipping directory." }
    }
    foreach ($key in $files.Keys) {
      if (-not $seen.ContainsKey($key)) { Fail "Zip is missing '$($files[$key].Path)'." }
    }
  } finally {
    $archive.Dispose()
  }

  # --- write outputs
  $manifestPath = Join-Path $outFull $ManifestName
  $signaturePath = Join-Path $outFull $SignatureName
  [System.IO.File]::WriteAllBytes($manifestPath, $manifestBytes)
  [System.IO.File]::WriteAllBytes($signaturePath, $signature)

  Write-Host "Manifest  : $manifestPath ($($paths.Count) files, version $Version)"
  Write-Host "Signature : $signaturePath (64 bytes, verified)"
  Write-Host "Package   : $packageName ($packageSize bytes)"
  Write-Host "Upload $ManifestName, $SignatureName and $packageName to the GitHub release."
} catch {
  [Console]::Error.WriteLine("New-UpdateManifest failed: $($_.Exception.Message)")
  exit 1
}
