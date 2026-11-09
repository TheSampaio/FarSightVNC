<#
.SYNOPSIS
  End-to-end install test of a REAL signed FarSightVNC release (no network, no UAC).

.DESCRIPTION
  Uses the three release assets (zip, farsightvnc-update.manifest, .sig) found in
  -ReleaseDir and a copy of the previous shipping folder (-OldShipping) as the
  installed version. Everything happens in a fresh temp directory:

    1. positive: stage the release with the production keys (updater-test.exe
       --stage-local), start the OLD viewer, start FarSightUpdater.exe, check that it
       waits while the viewer runs, stop ONLY that viewer, then check that every
       manifest file was installed, user files are untouched, leftovers are gone;
    2. negative: flip one byte of a staged file; the updater must exit 3 and leave
       the installation byte-identical;
    3. downgrade: install root already at the new version; the updater must refuse
       ("not newer", exit 0), change nothing and remove the pending update.

  It runs no network code and never elevates (the temp folder is user-writable).
  Only the processes it starts are stopped; only the temp and state directories it
  created are removed.

.PARAMETER ReleaseDir
  Folder holding FarSightVNC-v<version>.zip, farsightvnc-update.manifest and
  farsightvnc-update.manifest.sig (for example the Desktop after New-UpdateManifest).

.PARAMETER OldShipping
  The previous shipping folder (x64\tvnviewer.exe, Win32\tvnviewer.exe, LICENSE.txt, ...).

.PARAMETER Arch
  x64 (default) or Win32: the architecture of the viewer that is started.

.PARAMETER Tag
  Release tag (default: v + the manifest version).

.PARAMETER OldVersion
  Version of -OldShipping (default: read from its tvnviewer.exe).

.PARAMETER TestExe
  updater-test.exe with --stage-local and --state-dir
  (default build\bin\x64\Release\updater-test.exe).
#>
param(
  [Parameter(Mandatory = $true)][string]$ReleaseDir,
  [Parameter(Mandatory = $true)][string]$OldShipping,
  [ValidateSet('x64', 'Win32')][string]$Arch = 'x64',
  [string]$Tag,
  [string]$OldVersion,
  [string]$TestExe
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $TestExe) { $TestExe = Join-Path $repoRoot 'build\bin\x64\Release\updater-test.exe' }
$ReleaseDir = (Resolve-Path -LiteralPath $ReleaseDir).Path
$OldShipping = (Resolve-Path -LiteralPath $OldShipping).Path
$TestExe = (Resolve-Path -LiteralPath $TestExe).Path

$ManifestPath = Join-Path $ReleaseDir 'farsightvnc-update.manifest'
foreach ($required in @($ManifestPath, "$ManifestPath.sig")) {
  if (-not (Test-Path -LiteralPath $required)) { throw "Release asset not found: $required" }
}

# ---------------------------------------------------------------- reporting
$script:failures = 0
$script:checks = 0
function Assert-That([bool]$Condition, [string]$Message) {
  $script:checks++
  if ($Condition) { Write-Host "  PASS  $Message" }
  else { $script:failures++; Write-Host "  FAIL  $Message" -ForegroundColor Red }
}

# ---------------------------------------------------------------- helpers
function Get-Sha256Hex([string]$Path) {
  $stream = [System.IO.File]::OpenRead($Path)
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($stream)) -replace '-', '').ToLowerInvariant() }
  finally { $sha.Dispose(); $stream.Dispose() }
}

# Relative path -> SHA-256 for every file under a folder (used to prove "unchanged").
function Get-TreeSnapshot([string]$Root) {
  $snapshot = [ordered]@{}
  foreach ($item in (Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Sort-Object FullName)) {
    $snapshot[$item.FullName.Substring($Root.Length + 1)] = Get-Sha256Hex $item.FullName
  }
  return $snapshot
}

function Test-SnapshotEqual($A, $B) {
  if ($A.Count -ne $B.Count) { return $false }
  foreach ($key in $A.Keys) {
    if (-not $B.Contains($key) -or $A[$key] -ne $B[$key]) { return $false }
  }
  return $true
}

# Same rule as FsUtil::updateStateDirectory: <LocalAppData>\FarSightVNC\Updates\<16 hex of
# SHA-256 of the lower-cased full root path in UTF-8>.
function Get-StateDirectory([string]$Root) {
  $normalized = [System.IO.Path]::GetFullPath($Root).TrimEnd('\').ToLowerInvariant()
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $hex = ([BitConverter]::ToString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($normalized))) -replace '-', '').ToLowerInvariant()
  } finally { $sha.Dispose() }
  return Join-Path (Join-Path (Join-Path $env:LOCALAPPDATA 'FarSightVNC') 'Updates') $hex.Substring(0, 16)
}

# Output of "updater-test.exe --state-dir <root>" (the C++ implementation).
function Get-NativeStateDirectory([string]$Root) {
  $output = & $TestExe --state-dir $Root
  return ($output | Select-Object -First 1).Trim()
}

$ManifestFiles = @()
$ManifestVersion = $null
foreach ($line in [System.IO.File]::ReadAllLines($ManifestPath)) {
  if ($line -match '^version (\S+)$') { $ManifestVersion = $Matches[1] }
  elseif ($line -match '^file (\S+) (\d+) ([0-9a-f]{64})$') {
    $ManifestFiles += [pscustomobject]@{ Path = $Matches[1]; Size = [int64]$Matches[2]; Sha = $Matches[3] }
  }
}
if (-not $ManifestVersion -or $ManifestFiles.Count -eq 0) { throw 'Could not read the manifest.' }
if (-not $Tag) { $Tag = "v$ManifestVersion" }
if (-not $OldVersion) {
  $OldVersion = (Get-Item (Join-Path $OldShipping "$Arch\tvnviewer.exe")).VersionInfo.ProductVersion
}

function New-InstallRoot([string]$Parent, [string]$Source) {
  $root = Join-Path $Parent 'FarSightVNC'
  New-Item -ItemType Directory -Force $Parent | Out-Null
  Copy-Item -LiteralPath $Source -Destination $root -Recurse
  Set-Content -LiteralPath (Join-Path $root 'my.vnc') -Value 'host=user-file' -Encoding ASCII
  Set-Content -LiteralPath (Join-Path $root "$Arch\notes.txt") -Value 'user notes' -Encoding ASCII
  return $root
}

# Stages the release for $Root; returns the state directory.
function Invoke-Stage([string]$Root, [string]$CurrentVersion) {
  $state = Get-StateDirectory $Root
  if (Test-Path -LiteralPath $state) { Remove-Item -LiteralPath $state -Recurse -Force }
  $script:stateDirs += $state
  $output = & $TestExe --stage-local $Tag $ReleaseDir $state $CurrentVersion
  $code = $LASTEXITCODE
  Write-Host "  helper: $(($output -join ' | '))"
  return [pscustomobject]@{ State = $state; Code = $code }
}

function Start-Updater([string]$State, [string]$Root) {
  $bin = Join-Path $State 'bin'
  New-Item -ItemType Directory -Force $bin | Out-Null
  $updaterExe = Join-Path $bin 'FarSightUpdater.exe'
  Copy-Item -LiteralPath (Join-Path $State "pending\files\$Arch\FarSightUpdater.exe") -Destination $updaterExe -Force
  $arguments = @('--protocol', '1', '--install', '--state', "`"$State`"", '--viewer', "`"$Root\$Arch\tvnviewer.exe`"", '--arch', $Arch) -join ' '
  $process = Start-Process -FilePath $updaterExe -ArgumentList $arguments -PassThru -WindowStyle Hidden
  $null = $process.Handle
  return $process
}

function Wait-Updater($Process, [int]$TimeoutSeconds = 60) {
  if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
    $Process.Kill()   # only the updater started by this script
    return -1
  }
  return $Process.ExitCode
}

# ---------------------------------------------------------------- run
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("fsv-e2e-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$script:stateDirs = @()
$viewerProcess = $null
$updaterProcess = $null

try {
  New-Item -ItemType Directory -Force $work | Out-Null
  Write-Host "Release $Tag (manifest version $ManifestVersion), old shipping $OldVersion, arch $Arch"
  Write-Host "Temp: $work"

  # ===== a/b: positive run =====
  Write-Host "`n[1] Install over $OldVersion while the old viewer is running"
  $rootA = New-InstallRoot (Join-Path $work 'a') $OldShipping
  $userSnapshotA = @{
    'my.vnc'            = Get-Sha256Hex (Join-Path $rootA 'my.vnc')
    "$Arch\notes.txt"   = Get-Sha256Hex (Join-Path $rootA "$Arch\notes.txt")
  }
  $nativeState = Get-NativeStateDirectory $rootA
  Assert-That ($nativeState -ieq (Get-StateDirectory $rootA)) "state directory matches FsUtil::updateStateDirectory ($nativeState)"

  $stage = Invoke-Stage $rootA $OldVersion
  Assert-That ($stage.Code -eq 0) 'stageUpdate (production keys, real assets) succeeded: exit 0'
  Assert-That (Test-Path -LiteralPath (Join-Path $stage.State 'pending\ready')) 'pending\ready exists'
  Assert-That (Test-Path -LiteralPath $stage.State) 'helper created the state directory computed by the script'

  $beforeA = Get-TreeSnapshot $rootA
  $viewerProcess = Start-Process -FilePath (Join-Path $rootA "$Arch\tvnviewer.exe") -PassThru
  Start-Sleep -Seconds 2
  Assert-That (-not $viewerProcess.HasExited) 'old viewer is running'
  $updaterProcess = Start-Updater $stage.State $rootA
  Start-Sleep -Seconds 6
  Assert-That (-not $updaterProcess.HasExited) 'updater is still waiting after 6 s (viewer open)'
  Assert-That (Test-SnapshotEqual $beforeA (Get-TreeSnapshot $rootA)) 'installation unchanged while the viewer is open'

  Stop-Process -Id $viewerProcess.Id -Force
  $viewerProcess = $null
  $codeA = Wait-Updater $updaterProcess
  $updaterProcess = $null
  Assert-That ($codeA -eq 0) "updater exit code 0 (got $codeA)"

  $allPresent = $true
  foreach ($file in $ManifestFiles) {
    $target = Join-Path $rootA ($file.Path -replace '/', '\')
    if (-not (Test-Path -LiteralPath $target) -or (Get-Sha256Hex $target) -ne $file.Sha) {
      $allPresent = $false
      Write-Host "    missing or different: $($file.Path)" -ForegroundColor Yellow
    }
  }
  Assert-That $allPresent "all $($ManifestFiles.Count) manifest files exist with matching SHA-256"
  foreach ($arch2 in 'x64', 'Win32') {
    $version = (Get-Item (Join-Path $rootA "$arch2\tvnviewer.exe")).VersionInfo.ProductVersion
    Assert-That ($version -eq $ManifestVersion) "$arch2\tvnviewer.exe is now $version"
    Assert-That (Test-Path -LiteralPath (Join-Path $rootA "$arch2\FarSightUpdater.exe")) "$arch2\FarSightUpdater.exe was added"
  }
  Assert-That ((Get-Sha256Hex (Join-Path $rootA 'my.vnc')) -eq $userSnapshotA['my.vnc']) 'my.vnc unchanged'
  Assert-That ((Get-Sha256Hex (Join-Path $rootA "$Arch\notes.txt")) -eq $userSnapshotA["$Arch\notes.txt"]) "$Arch\notes.txt unchanged"
  Assert-That (-not (Test-Path -LiteralPath (Join-Path $rootA '.fsv-update'))) 'no .fsv-update work folder left'
  Assert-That (-not (Test-Path -LiteralPath (Join-Path $stage.State 'pending'))) 'pending directory removed'
  $lastResult = Join-Path $stage.State 'last-result.txt'
  $lastText = if (Test-Path -LiteralPath $lastResult) { (Get-Content -LiteralPath $lastResult -Raw) } else { '' }
  Assert-That ($lastText.StartsWith("ok $ManifestVersion")) "last-result.txt starts with 'ok $ManifestVersion' ($($lastText.Trim()))"
  Assert-That (Test-Path -LiteralPath (Join-Path $stage.State 'updater.log')) 'updater.log present'

  # ===== e: negative run (tampered staging) =====
  Write-Host "`n[2] Tampered staged file must be rejected and leave the installation untouched"
  $rootB = New-InstallRoot (Join-Path $work 'b') $OldShipping
  $stageB = Invoke-Stage $rootB $OldVersion
  Assert-That ($stageB.Code -eq 0) 'stageUpdate succeeded: exit 0'
  $victim = Join-Path $stageB.State "pending\files\$Arch\tvnviewer.exe"
  $bytes = [System.IO.File]::ReadAllBytes($victim)
  $mid = [int][Math]::Floor($bytes.Length / 2)
  $bytes[$mid] = $bytes[$mid] -bxor 0xFF
  [System.IO.File]::WriteAllBytes($victim, $bytes)
  $beforeB = Get-TreeSnapshot $rootB
  $updaterProcess = Start-Updater $stageB.State $rootB
  $codeB = Wait-Updater $updaterProcess
  $updaterProcess = $null
  Assert-That ($codeB -eq 3) "updater exit code 3 (got $codeB)"
  Assert-That (Test-SnapshotEqual $beforeB (Get-TreeSnapshot $rootB)) 'installation byte-identical'
  Assert-That (-not (Test-Path -LiteralPath (Join-Path $rootB '.fsv-update'))) 'no .fsv-update work folder'

  # ===== f: downgrade run =====
  Write-Host "`n[3] Staged version not newer than the installed one must be refused"
  $newShipping = Join-Path $work 'newship'
  New-Item -ItemType Directory -Force $newShipping | Out-Null
  foreach ($file in $ManifestFiles) {
    # Rebuild the NEW shipping folder from the files installed (and verified) in run [1].
    $dest = Join-Path $newShipping ($file.Path -replace '/', '\')
    New-Item -ItemType Directory -Force (Split-Path $dest -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $rootA ($file.Path -replace '/', '\')) -Destination $dest
  }
  $rootC = New-InstallRoot (Join-Path $work 'c') $newShipping
  # The helper is told the running version is the OLD one, so staging succeeds.
  $stageC = Invoke-Stage $rootC $OldVersion
  Assert-That ($stageC.Code -eq 0) "stageUpdate succeeded with current=${OldVersion}: exit 0"
  $beforeC = Get-TreeSnapshot $rootC
  $updaterProcess = Start-Updater $stageC.State $rootC
  $codeC = Wait-Updater $updaterProcess
  $updaterProcess = $null
  Assert-That ($codeC -eq 0) "updater exit code 0 (got $codeC)"
  $logC = Join-Path $stageC.State 'updater.log'
  $logText = if (Test-Path -LiteralPath $logC) { Get-Content -LiteralPath $logC -Raw } else { '' }
  Assert-That ($logText -match 'not newer') "updater.log says 'not newer'"
  Assert-That (Test-SnapshotEqual $beforeC (Get-TreeSnapshot $rootC)) 'installation unchanged'
  Assert-That (-not (Test-Path -LiteralPath (Join-Path $stageC.State 'pending'))) 'pending directory removed'
}
catch {
  $script:failures++
  Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
}
finally {
  # ===== g: cleanup (only what this script started or created) =====
  if ($viewerProcess -and -not $viewerProcess.HasExited) { Stop-Process -Id $viewerProcess.Id -Force }
  if ($updaterProcess -and -not $updaterProcess.HasExited) { $updaterProcess.Kill() }
  Start-Sleep -Milliseconds 300
  foreach ($state in $script:stateDirs) {
    if ($state -and (Test-Path -LiteralPath $state)) { Remove-Item -LiteralPath $state -Recurse -Force -ErrorAction SilentlyContinue }
  }
  if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
  $leftover = (Test-Path -LiteralPath $work) -or (@($script:stateDirs | Where-Object { Test-Path -LiteralPath $_ }).Count -gt 0)
  Assert-That (-not $leftover) 'temp and state directories removed'
}

Write-Host "`nEnd-to-end install test: $($script:checks) checks, $($script:failures) failures"
if ($script:failures -ne 0) { exit 1 }
exit 0
