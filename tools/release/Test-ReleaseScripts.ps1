<#
.SYNOPSIS
  End-to-end test of New-UpdateSigningKey.ps1 and New-UpdateManifest.ps1
  against the C++ verifier in updater-test.exe.

.DESCRIPTION
  Creates a throw-away key, a fake shipping folder and zip in %TEMP%, runs
  New-UpdateManifest.ps1, then runs updater-test.exe with FSV_PS_MANIFEST_DIR
  set so that verifyAndParseManifest checks the PowerShell output. Also checks
  that a tampered manifest is rejected by the C++ code and that the scripts
  fail loudly on bad input. Temp files are removed at the end.

.PARAMETER TestExe
  Path to updater-test.exe (default: build\bin\x64\Release\updater-test.exe).
#>
param(
  [string]$TestExe
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $TestExe) { $TestExe = Join-Path $repoRoot 'build\bin\x64\Release\updater-test.exe' }
if (-not (Test-Path -LiteralPath $TestExe)) { throw "updater-test.exe not found: $TestExe (build the updater-test project first)" }

$keyScript = Join-Path $PSScriptRoot 'New-UpdateSigningKey.ps1'
$manifestScript = Join-Path $PSScriptRoot 'New-UpdateManifest.ps1'
$version = '1.0.0a'
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('fsv-release-test-' + [Guid]::NewGuid().ToString('N'))
$failures = 0

function Assert-That([bool]$Condition, [string]$Message) {
  if ($Condition) {
    Write-Host "  ok   $Message"
  } else {
    Write-Host "  FAIL $Message"
    $script:failures++
  }
}

# Runs a script in a separate Windows PowerShell and returns its exit code.
function Invoke-Script([string]$Script, [string[]]$Arguments) {
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'   # native stderr must not become a terminating error
  try {
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $Script @Arguments 2>&1 | Out-Null
    return $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previous
  }
}

function New-TestFile([string]$Path, [int]$Size, [int]$Seed) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
  $bytes = New-Object byte[] $Size
  (New-Object System.Random $Seed).NextBytes($bytes)
  [System.IO.File]::WriteAllBytes($Path, $bytes)
}

try {
  New-Item -ItemType Directory -Force -Path $work | Out-Null
  Write-Host "Working directory: $work"

  # --- key generation
  $keys = Join-Path $work 'keys'
  Assert-That ((Invoke-Script $keyScript @('-OutDirectory', $keys, '-Id', 'test')) -eq 0) 'key script succeeds'
  $privateKey = Join-Path $keys 'update-key-test.private.blob'
  $publicKey = Join-Path $keys 'update-key-test.public.blob'
  Assert-That ((Test-Path $privateKey) -and (Test-Path $publicKey)) 'key files exist'
  Assert-That ((Get-Item $publicKey).Length -eq 72) 'public blob is 72 bytes'
  Assert-That ((Invoke-Script $keyScript @('-OutDirectory', $keys, '-Id', 'test')) -ne 0) 'key script refuses to overwrite'
  $insideRepo = Join-Path $repoRoot 'tmp-must-not-be-created'
  Assert-That ((Invoke-Script $keyScript @('-OutDirectory', $insideRepo)) -ne 0) 'key script refuses a directory inside the repository'
  Assert-That (-not (Test-Path $insideRepo)) 'no key directory was created inside the repository'

  # --- fake shipping folder + zip
  $ship = Join-Path $work 'FarSightVNC-v1.0.0a'
  New-TestFile (Join-Path $ship 'bin\x64\fsvnc-viewer.exe') 150000 1
  New-TestFile (Join-Path $ship 'bin\x64\fsvnc-updater.exe') 40000 2
  New-TestFile (Join-Path $ship 'bin\x86\fsvnc-viewer.exe') 120000 3
  New-TestFile (Join-Path $ship 'bin\x86\fsvnc-updater.exe') 30000 4
  [System.IO.File]::WriteAllText((Join-Path $ship 'LICENSE.txt'), "license`n")
  [System.IO.File]::WriteAllText((Join-Path $ship 'README.txt'), "readme`n")
  [System.IO.File]::WriteAllText((Join-Path $ship 'LEIAME.txt'), "leiame`n")
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip = Join-Path $work 'FarSightVNC-v1.0.0a.zip'
  [System.IO.Compression.ZipFile]::CreateFromDirectory($ship, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)

  # --- manifest + signature
  $out = Join-Path $work 'out'
  $manifestArgs = @('-ShippingDirectory', $ship, '-ZipPath', $zip, '-Version', $version,
    '-PrivateKeyPath', $privateKey, '-OutDirectory', $out)
  Assert-That ((Invoke-Script $manifestScript $manifestArgs) -eq 0) 'manifest script succeeds'
  $manifestFile = Join-Path $out 'farsightvnc-update.manifest'
  $signatureFile = Join-Path $out 'farsightvnc-update.manifest.sig'
  Assert-That ((Test-Path $manifestFile) -and (Test-Path $signatureFile)) 'manifest and signature exist'
  Assert-That ((Get-Item $signatureFile).Length -eq 64) 'signature is 64 bytes'
  $manifestBytes = [System.IO.File]::ReadAllBytes($manifestFile)
  Assert-That (-not ($manifestBytes.Length -ge 3 -and $manifestBytes[0] -eq 0xEF -and $manifestBytes[1] -eq 0xBB)) 'manifest has no BOM'
  Assert-That (-not ($manifestBytes -contains 13)) 'manifest uses LF line endings'
  $lines = [System.Text.Encoding]::UTF8.GetString($manifestBytes).TrimEnd("`n").Split("`n")
  Assert-That ($lines[0] -ceq 'FarSightVNC-Update-Manifest 1') 'manifest header'
  Assert-That ($lines[1] -ceq "version $version") 'manifest version line'
  Assert-That ($lines[2] -like 'package FarSightVNC-v1.0.0a.zip * *') 'manifest package line'
  Assert-That (($lines | Where-Object { $_ -like 'file *' }).Count -eq 7) 'manifest lists 7 files'
  Assert-That ($lines -contains ('file bin/x64/fsvnc-viewer.exe 150000 ' + (Get-FileHash (Join-Path $ship 'bin\x64\fsvnc-viewer.exe') -Algorithm SHA256).Hash.ToLowerInvariant())) 'bin/x64/fsvnc-viewer.exe line is correct'

  # --- C++ verifier accepts the PowerShell output
  Copy-Item $publicKey (Join-Path $out 'update-key-test.public.blob')
  $env:FSV_PS_MANIFEST_DIR = $out
  $env:FSV_PS_EXPECT_VERSION = $version
  & $TestExe *> $null
  Assert-That ($LASTEXITCODE -eq 0) 'updater-test.exe verifies the PowerShell manifest (exit 0)'

  # --- tampering is detected by the C++ verifier
  $bad = Join-Path $work 'out-tampered'
  Copy-Item -Recurse $out $bad
  $tamperedBytes = [System.IO.File]::ReadAllBytes((Join-Path $bad 'farsightvnc-update.manifest'))
  $index = [System.Text.Encoding]::ASCII.GetString($tamperedBytes).IndexOf('fsvnc-viewer.exe 150000') + 18
  $tamperedBytes[$index] = [byte][char]'9'
  [System.IO.File]::WriteAllBytes((Join-Path $bad 'farsightvnc-update.manifest'), $tamperedBytes)
  $env:FSV_PS_MANIFEST_DIR = $bad
  & $TestExe *> $null
  Assert-That ($LASTEXITCODE -ne 0) 'updater-test.exe rejects a tampered manifest (non-zero exit)'

  Remove-Item Env:\FSV_PS_MANIFEST_DIR, Env:\FSV_PS_EXPECT_VERSION -ErrorAction SilentlyContinue

  # --- the manifest script fails loudly on bad input
  Assert-That ((Invoke-Script $manifestScript ($manifestArgs[0..4] + @('v1.0.0a') + $manifestArgs[6..9])) -ne 0) 'rejects a version with a leading v'
  $badOut = Join-Path $work 'out-bad'
  $argsBadVersion = @('-ShippingDirectory', $ship, '-ZipPath', $zip, '-Version', '1.0', '-PrivateKeyPath', $privateKey, '-OutDirectory', $badOut)
  Assert-That ((Invoke-Script $manifestScript $argsBadVersion) -ne 0) 'rejects a malformed version'
  [System.IO.File]::WriteAllText((Join-Path $ship 'README.txt'), "changed after zipping`n")
  $argsMismatch = @('-ShippingDirectory', $ship, '-ZipPath', $zip, '-Version', $version, '-PrivateKeyPath', $privateKey, '-OutDirectory', $badOut)
  Assert-That ((Invoke-Script $manifestScript $argsMismatch) -ne 0) 'rejects a zip that differs from the shipping folder'
  Assert-That (-not (Test-Path (Join-Path $badOut 'farsightvnc-update.manifest'))) 'no manifest is written on mismatch'
} finally {
  Remove-Item Env:\FSV_PS_MANIFEST_DIR, Env:\FSV_PS_EXPECT_VERSION -ErrorAction SilentlyContinue
  if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
}

if ($failures -gt 0) {
  Write-Host "Test-ReleaseScripts: $failures failure(s)"
  exit 1
}
Write-Host 'Test-ReleaseScripts: all checks passed'
exit 0
