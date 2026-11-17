<#
.SYNOPSIS
  Scans built FarSightVNC server binaries (and, later, the installer) for
  TightVNC identity strings that would make the two products fight over the same
  machine-wide resources.

.DESCRIPTION
  The owner runs a live TightVNC server. The FarSightVNC server is only allowed
  to exist on the same machine because it has an identity of its own: its own
  service name, registry key, Event Log source, named pipes, global mutexes and
  RegisterWindowMessage strings. server-identity-test asserts that constant by
  constant in the source; this script asserts it on the bytes that actually ship,
  which also catches a stale object file, a resource script or a third-party
  blob that the source-level test cannot see.

  Two lists are used, because not every occurrence of the word "TightVNC" is a
  collision:

  COLLISION (fails the scan)
    Strings that name a shared operating-system resource. If one of these is in
    the binary, installing or starting FarSightVNC can take over, overwrite or
    disturb the TightVNC installation.

  COSMETIC (reported, does not fail)
    User-visible text and version metadata, which today still come from
    tvnserver/tvnserver.rc and from the "Based on TightVNC" attribution. These
    are a later task (the .rc files are UTF-16 LE and are deliberately left
    alone); they cost the owner nothing but a wrong label.

  The scan reads files and nothing else: no registry access, no service control,
  no process is started. It is safe to run on a machine with TightVNC installed
  and running.

.PARAMETER Path
  Files or directories to scan. Default: the usual build output locations of the
  server and its helpers. Directories are searched for *.exe, *.dll and *.msi.

.PARAMETER FailOnCosmetic
  Also fail on the cosmetic occurrences. Use this once tvnserver.rc has been
  rebranded, to keep it that way.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\scan-server-identity.ps1

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\scan-server-identity.ps1 -Path .\build\bin\x64\Release
#>
[CmdletBinding()]
param(
  [string[]]$Path,
  [switch]$FailOnCosmetic
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

# Strings that name a shared machine-wide resource. Each one is a real collision
# with the installed TightVNC server, not a cosmetic leftover.
#
# Matched without case, because the resources they name ignore case: registry
# keys, named pipes and the global atom table behind RegisterWindowMessage.
$collisionStringsNoCase = @(
  'Software\TightVNC\Server',             # server configuration key
  'Software\TightVNC\Control',            # reverse connection history key
  'TightVNC_Service_Control',             # named pipe, service side
  'TightVNC_Application_Control',         # named pipe, application side
  'TVN_log_pipe_public_name',             # log pipe
  'TVN.HOOK.LOADER.CLOSE.CODE',           # RegisterWindowMessage, system-wide
  'TVN.HOOK.MESSAGE.CODE'                 # RegisterWindowMessage, system-wide
)

# Matched with case. These two are mutex names that differ from a C++ class name
# of this very code base only by case ("tvnserverApplication" the mutex versus
# "TvnServerApplication" the class, whose name reaches the binary through RTTI
# and debug records). Matching them without case reports every build as broken.
$collisionStringsExact = @(
  'tvnserverApplication',                 # global mutex, server instance
  'tvnserverServiceControlSlave'          # global mutex, control application
)

# Cosmetic: UI text, product metadata and the attribution. Reported only.
$cosmeticStrings = @(
  'TightVNC Server',
  'TightVNC Control',
  'tvnserver.exe'
)

if (-not $Path -or $Path.Count -eq 0) {
  $Path = @(
    (Join-Path $repoRoot 'build\bin\x64\Release'),
    (Join-Path $repoRoot 'build\bin\x64\Debug'),
    (Join-Path $repoRoot 'build\bin\Win32\Release'),
    (Join-Path $repoRoot 'build\bin\Win32\Debug')
  )
}

function Get-ScanTarget {
  param([string[]]$Roots)

  $files = New-Object System.Collections.Generic.List[string]
  foreach ($root in $Roots) {
    if (-not (Test-Path -LiteralPath $root)) { continue }
    $item = Get-Item -LiteralPath $root
    if ($item.PSIsContainer) {
      Get-ChildItem -LiteralPath $root -File |
        Where-Object { $_.Extension -in '.exe', '.dll', '.msi' } |
        ForEach-Object { $files.Add($_.FullName) }
    } else {
      $files.Add($item.FullName)
    }
  }
  return $files
}

# A PE file carries its literals both as ANSI and as UTF-16 LE. The byte array is
# decoded as ASCII once and as Unicode twice (both alignments), so a wide string
# that does not start on an even offset is still found.
function Get-DecodedText {
  param([string]$File)

  $bytes = [System.IO.File]::ReadAllBytes($File)
  $texts = @(
    [System.Text.Encoding]::ASCII.GetString($bytes),
    [System.Text.Encoding]::Unicode.GetString($bytes)
  )
  if ($bytes.Length -gt 1) {
    $texts += [System.Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1)
  }
  return $texts
}

function Test-Contains {
  param([string[]]$Texts, [string]$Needle, [switch]$CaseSensitive)

  if ($CaseSensitive) {
    $comparison = [System.StringComparison]::Ordinal
  } else {
    $comparison = [System.StringComparison]::OrdinalIgnoreCase
  }
  foreach ($text in $Texts) {
    if ($text.IndexOf($Needle, $comparison) -ge 0) {
      return $true
    }
  }
  return $false
}

$targets = Get-ScanTarget -Roots $Path
if ($targets.Count -eq 0) {
  Write-Host 'scan-server-identity: no binary found to scan (nothing built yet) - SKIPPED'
  exit 0
}

$collisionHits = New-Object System.Collections.Generic.List[string]
$cosmeticHits = New-Object System.Collections.Generic.List[string]

foreach ($file in $targets) {
  $texts = Get-DecodedText -File $file
  # The full (repository-relative where possible) path, never the bare file name:
  # several configurations are scanned at once and a stale binary from one of
  # them must be identifiable.
  $name = $file
  if ($file.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    $name = $file.Substring($repoRoot.Length).TrimStart('\')
  }

  foreach ($needle in $collisionStringsNoCase) {
    if (Test-Contains -Texts $texts -Needle $needle) {
      $collisionHits.Add("$name : $needle")
    }
  }
  foreach ($needle in $collisionStringsExact) {
    if (Test-Contains -Texts $texts -Needle $needle -CaseSensitive) {
      $collisionHits.Add("$name : $needle (exact case)")
    }
  }
  foreach ($needle in $cosmeticStrings) {
    if (Test-Contains -Texts $texts -Needle $needle) {
      $cosmeticHits.Add("$name : $needle")
    }
  }
}

Write-Host "scan-server-identity: scanned $($targets.Count) file(s)"
foreach ($file in $targets) {
  Write-Host "  $file"
}

if ($cosmeticHits.Count -gt 0) {
  Write-Host ''
  Write-Host "COSMETIC (UI text / version metadata, still to be rebranded): $($cosmeticHits.Count)"
  foreach ($hit in $cosmeticHits) { Write-Host "  $hit" }
}

if ($collisionHits.Count -gt 0) {
  Write-Host ''
  Write-Host "COLLISION with the installed TightVNC server: $($collisionHits.Count)"
  foreach ($hit in $collisionHits) { Write-Host "  $hit" }
  Write-Host ''
  Write-Host 'scan-server-identity: FAILED'
  exit 1
}

if ($FailOnCosmetic -and $cosmeticHits.Count -gt 0) {
  Write-Host ''
  Write-Host 'scan-server-identity: FAILED (-FailOnCosmetic)'
  exit 1
}

Write-Host ''
Write-Host 'scan-server-identity: OK, no shared machine-wide name found'
exit 0
