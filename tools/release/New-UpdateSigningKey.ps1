<#
.SYNOPSIS
  Generates an ECDSA P-256 key pair for signing FarSightVNC update manifests.

.DESCRIPTION
  Writes update-key-<Id>.private.blob (BCRYPT_ECCPRIVATE_BLOB, ACL restricted
  to the current user) and update-key-<Id>.public.blob (BCRYPT_ECCPUBLIC_BLOB,
  72 bytes) into -OutDirectory, then prints the public blob as a C++ byte
  array to paste into updater-lib/TrustedKeys.cpp.

  The private key must never enter the repository: the script refuses an
  output directory inside the repository and refuses to overwrite files.
  Keep an offline backup of the private blob; losing it means users must
  update manually once.

.PARAMETER OutDirectory
  Where the key files are written (created if missing), for example
  $env:USERPROFILE\.farsightvnc-signing

.PARAMETER Id
  Key id used in the file names (default 1).

.EXAMPLE
  .\tools\release\New-UpdateSigningKey.ps1 -OutDirectory "$env:USERPROFILE\.farsightvnc-signing"
#>
param(
  [Parameter(Mandatory = $true)]
  [string]$OutDirectory,
  [ValidatePattern('^[A-Za-z0-9]+$')]
  [string]$Id = '1'
)

$ErrorActionPreference = 'Stop'

function Test-IsInside([string]$Path, [string]$Root) {
  $full = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
  $base = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
  return ($full.Equals($base, [System.StringComparison]::OrdinalIgnoreCase) -or
    $full.StartsWith($base + '\', [System.StringComparison]::OrdinalIgnoreCase))
}

try {
  $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
  if (Test-IsInside $OutDirectory $repoRoot) {
    throw "Refusing to write a private key inside the repository ($repoRoot). Choose a directory outside it."
  }
  $outFull = [System.IO.Path]::GetFullPath($OutDirectory)
  $privatePath = Join-Path $outFull "update-key-$Id.private.blob"
  $publicPath = Join-Path $outFull "update-key-$Id.public.blob"
  foreach ($path in @($privatePath, $publicPath)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite existing file: $path" }
  }
  New-Item -ItemType Directory -Force -Path $outFull | Out-Null

  # ECDsaCng(256) creates an ECDSA P-256 key. (CngKey.Create with explicit
  # parameters fails with "Object already exists" on some systems.)
  $ecdsa = New-Object System.Security.Cryptography.ECDsaCng 256
  try {
    $privateBlob = $ecdsa.Key.Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
    $publicBlob = $ecdsa.Key.Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
  } finally {
    $ecdsa.Dispose()
  }
  if ($publicBlob.Length -ne 72) { throw "Unexpected public blob size $($publicBlob.Length) (expected 72)." }

  [System.IO.File]::WriteAllBytes($privatePath, $privateBlob)
  [System.IO.File]::WriteAllBytes($publicPath, $publicBlob)

  # Private key: current user only.
  $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
  & icacls.exe $privatePath /inheritance:r /grant:r "*${sid}:(R,W)" | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "icacls failed (exit $LASTEXITCODE); the private key ACL was not restricted: $privatePath" }

  Write-Host "Private key : $privatePath (keep offline, back it up, never commit it)"
  Write-Host "Public key  : $publicPath"
  Write-Host ''
  Write-Host "// Key $Id - generated $(Get-Date -Format 'yyyy-MM-dd'). BCRYPT_ECCPUBLIC_BLOB, ECDSA P-256."
  Write-Host "constexpr std::uint8_t kKey$Id[72] = {"
  $lines = @()
  for ($i = 0; $i -lt $publicBlob.Length; $i += 12) {
    $count = [Math]::Min(12, $publicBlob.Length - $i)
    $lines += '    ' + ((0..($count - 1) | ForEach-Object { '0x{0:x2}' -f $publicBlob[$i + $_] }) -join ', ')
  }
  Write-Host (($lines -join ",`n") + '};')
} catch {
  [Console]::Error.WriteLine("New-UpdateSigningKey failed: $($_.Exception.Message)")
  exit 1
}
