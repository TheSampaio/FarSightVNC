# Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.

<#
.SYNOPSIS
  Fuzzes the viewer decoders against tools/fuzz-rfb-server.py.

.DESCRIPTION
  For every case (encoding x seed) the script starts the fuzzing RFB server on
  a loopback port (5933-5945 only), launches the viewer with
  -host=127.0.0.1 -port=<p> -viewonly=yes -encoding=<enc> -useclipboard=no and
  classifies the outcome:

    CRASH  the viewer exited with an exception code (0xC0000005, 0xC0000409,
           0xC0000374, ...) or abort() (3), or a CRT dialog appeared
           ("Debug Assertion", "Debug Error", "Run-Time Check Failure",
           "HEAP CORRUPTION", abort, WER).
    HANG   a viewer window stopped answering (IsHungAppWindow or
           SendMessageTimeout(WM_NULL) failed), or the viewer did not exit
           after its error/disconnect message was dismissed.
    OK     the viewer showed its handled error/disconnect message and exited
           cleanly once it was dismissed (OK), or it stayed responsive until
           the timeout (OK, "nodialog").

  The message box is dismissed with a posted WM_CLOSE (no synthetic
  input), so the viewer runs its normal teardown and the debug CRT heap checks
  every freed block. The script sets SEM_NOGPFAULTERRORBOX for itself, which the
  viewer inherits, so an access violation ends the process with its exception
  code instead of opening a WER dialog. Only processes started here are
  stopped (by PID); history subkeys 127.0.0.1::<port> created by the runs are
  removed.

  Build the viewer first (Debug x64 adds /RTC1 and the debug CRT heap):
    & .\tools\build.ps1 -Configurations Debug -Platforms x64 -Project tvnviewer

.EXAMPLE
  .\tools\fuzz-test.ps1 -Seeds (1..50)
  .\tools\fuzz-test.ps1 -Encodings zrle -Seeds 17 -Parallel 1
  .\tools\fuzz-test.ps1 -Regression
#>
param(
  [ValidateSet('tight', 'zrle', 'hextile', 'rre', 'copyrect', 'richcursor', 'desktopsize')]
  [string[]]$Encodings = @('tight', 'zrle', 'hextile', 'rre', 'copyrect', 'richcursor', 'desktopsize'),
  [int[]]$Seeds = @(1..50),
  [string]$Viewer = (Join-Path $PSScriptRoot '..\x64\Debug\tvnviewer.exe'),
  [string]$Python = '',
  [ValidateRange(5933, 5945)]
  [int]$BasePort = 5933,
  [ValidateRange(1, 13)]
  [int]$Parallel = 3,
  [int]$TimeoutSeconds = 20,
  [int]$Messages = 25,
  [switch]$Regression,
  [string]$CsvPath = ''
)

$ErrorActionPreference = 'Stop'
if ($BasePort + $Parallel - 1 -gt 5945) { throw 'Ports must stay within 5933-5945.' }
$Viewer = (Resolve-Path $Viewer).Path
$serverScript = Join-Path $PSScriptRoot 'fuzz-rfb-server.py'
if (-not $Python) { $Python = (Get-Command python -ErrorAction Stop).Source }

# Fixed cases that reproduced confirmed defects; every one must end OK.
# Scenario cases are hand-written sequences (fuzz-rfb-server.py --scenario);
# seed cases replay the original fuzzer finding and are tied to the generator
# version of fuzz-rfb-server.py that found them.
$regressionCases = @(
  # FrameBuffer::fillRect filled one row past the buffer for a zero-height
  # rectangle on the bottom edge (heap overflow, 2026-11-03).
  @{ Encoding = 'rre'; Scenario = 'fillrect-rre-bottom-edge' }
  @{ Encoding = 'tight'; Scenario = 'fillrect-tight-bottom-edge' }
  @{ Encoding = 'rre'; Seed = 17 }, @{ Encoding = 'rre'; Seed = 21 }, @{ Encoding = 'rre'; Seed = 30 }
  @{ Encoding = 'rre'; Seed = 31 }, @{ Encoding = 'rre'; Seed = 41 }
  @{ Encoding = 'hextile'; Seed = 17 }, @{ Encoding = 'tight'; Seed = 34 }
)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class FuzzWin {
  delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lParam);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] static extern bool IsHungAppWindow(IntPtr hwnd);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder text, int max);
  [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
  [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);

  static List<IntPtr> TopWindows(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => {
      uint owner;
      GetWindowThreadProcessId(h, out owner);
      if (owner == pid && IsWindowVisible(h)) list.Add(h);
      return true;
    }, IntPtr.Zero);
    return list;
  }

  static string Text(IntPtr h) {
    var sb = new StringBuilder(1024);
    GetWindowText(h, sb, sb.Capacity);
    return sb.ToString();
  }

  static string Class(IntPtr h) {
    var sb = new StringBuilder(256);
    GetClassName(h, sb, sb.Capacity);
    return sb.ToString();
  }

  // One string per visible dialog of the process: "title | child texts".
  public static string[] Dialogs(int pid) {
    var result = new List<string>();
    foreach (var h in TopWindows((uint)pid)) {
      if (Class(h) != "#32770") continue;
      var sb = new StringBuilder(Text(h));
      EnumChildWindows(h, (c, l) => { var t = Text(c); if (t.Length > 0) sb.Append(" | ").Append(t); return true; }, IntPtr.Zero);
      result.Add(sb.ToString().Replace("\r", " ").Replace("\n", " "));
    }
    return result.ToArray();
  }

  // Posts WM_CLOSE to every visible dialog of the process (dismisses an MB_OK message box).
  public static int DismissDialogs(int pid) {
    int n = 0;
    foreach (var h in TopWindows((uint)pid)) {
      if (Class(h) == "#32770" && PostMessage(h, 0x0010 /* WM_CLOSE */, IntPtr.Zero, IntPtr.Zero)) n++;
    }
    return n;
  }

  // Number of visible top-level windows that do not answer within the timeout.
  public static int HungWindows(int pid, uint timeoutMs) {
    int hung = 0;
    foreach (var h in TopWindows((uint)pid)) {
      IntPtr r;
      if (IsHungAppWindow(h) || SendMessageTimeout(h, 0 /* WM_NULL */, IntPtr.Zero, IntPtr.Zero,
                                                   0x0002 /* SMTO_ABORTIFHUNG */, timeoutMs, out r) == IntPtr.Zero) hung++;
    }
    return hung;
  }

  public static int WindowCount(int pid) { return TopWindows((uint)pid).Count; }
}
'@

# SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX, inherited by the viewer.
[void][FuzzWin]::SetErrorMode(0x0003)

$crashPattern = 'Debug Assertion|Debug Error|Run-?Time Check Failure|HEAP CORRUPTION|abort\(\)|Visual C\+\+ Runtime|has stopped working|Unhandled exception|stack buffer|Assertion failed'
$crashExitCodes = @{ 3 = 'abort' }
$historyRoot = 'HKCU:\Software\TightVNC\Viewer\History'
$workDir = Join-Path ([IO.Path]::GetTempPath()) ("fsvnc-fuzz-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $workDir | Out-Null

$ports = @($BasePort..($BasePort + $Parallel - 1))
$preexisting = @{}
foreach ($port in $ports) {
  $preexisting[$port] = Test-Path -LiteralPath "$historyRoot\127.0.0.1::$port"
  if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) { throw "Port $port is already in use." }
}

if ($Regression) {
  $queue = New-Object System.Collections.Queue
  foreach ($case in $regressionCases) { $queue.Enqueue($case) }
  if ($queue.Count -eq 0) { Write-Host 'No regression cases defined.'; exit 0 }
} else {
  $queue = New-Object System.Collections.Queue
  foreach ($seed in $Seeds) { foreach ($enc in $Encodings) { $queue.Enqueue(@{ Encoding = $enc; Seed = $seed }) } }
}

function Format-ExitCode([int]$code) { '0x{0:X8}' -f $code }

function Start-Case($case, [int]$port) {
  if ($case.Scenario) {
    $label = $case.Scenario
    $caseArgs = @('--scenario', $case.Scenario)
  } else {
    $label = $case.Seed
    $caseArgs = @('--encoding', $case.Encoding, '--seed', $case.Seed)
  }
  $tag = "$($case.Encoding)-$label-$port"
  $out = Join-Path $workDir "$tag.out"
  $err = Join-Path $workDir "$tag.err"
  $server = Start-Process -FilePath $Python -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err `
    -ArgumentList (@('-I', "`"$serverScript`"", '--port', $port) + $caseArgs + @('--messages', $Messages))
  [pscustomobject]@{ Encoding = $case.Encoding; Seed = $label; Port = $port; Server = $server; Viewer = $null
    Out = $out; Err = $err; Phase = 'listen'; Started = [DateTime]::UtcNow; Mark = [DateTime]::UtcNow
    Result = ''; Detail = '' }
}

function Read-ServerLine($state, [string]$prefix) {
  if (-not (Test-Path -LiteralPath $state.Out)) { return $null }
  try {
    # The server still holds the file open for writing: share it.
    $fs = New-Object System.IO.FileStream($state.Out, 'Open', 'Read', 'ReadWrite')
    try { $text = (New-Object System.IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
  } catch { return $null }
  $text -split "`r?`n" | Where-Object { $_.StartsWith($prefix) } | Select-Object -Last 1
}

function Start-Viewer($state) {
  $enc = if ($state.Encoding -in @('tight', 'zrle', 'hextile', 'rre')) { $state.Encoding } else { 'raw' }
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $Viewer
  $psi.Arguments = "-host=127.0.0.1 -port=$($state.Port) -viewonly=yes -encoding=$enc -useclipboard=no"
  $psi.UseShellExecute = $false        # CreateProcess: the error mode is inherited
  $psi.WorkingDirectory = Split-Path $Viewer
  $state.Viewer = [System.Diagnostics.Process]::Start($psi)
  $state.Phase = 'run'
  $state.Mark = [DateTime]::UtcNow
}

function Complete-Case($state, [string]$result, [string]$detail) {
  $state.Result = $result
  $state.Detail = $detail
  $state.Phase = 'done'
}

function Test-Running($state) {
  $v = $state.Viewer
  $elapsed = ([DateTime]::UtcNow - $state.Mark).TotalSeconds
  if ($v.HasExited) {
    $code = $v.ExitCode
    if ($code -lt 0 -or $crashExitCodes.ContainsKey($code)) {
      Complete-Case $state 'CRASH' ("exit {0}" -f (Format-ExitCode $code)); return
    }
    if ($state.Phase -eq 'dismissed') { Complete-Case $state 'OK' 'message'; return }
    Complete-Case $state 'OK' ("exit {0}" -f $code); return
  }
  $dialogs = @([FuzzWin]::Dialogs($v.Id))
  $crash = $dialogs | Where-Object { $_ -match $crashPattern } | Select-Object -First 1
  if ($crash) { Complete-Case $state 'CRASH' $crash; return }
  if ($state.Phase -eq 'run') {
    if ($dialogs.Count -gt 0) {
      # Handled error or disconnect: let pending frees run, then dismiss it.
      if (-not $state.PSObject.Properties['DialogSeen']) {
        $state | Add-Member -NotePropertyName DialogSeen -NotePropertyValue ([DateTime]::UtcNow)
        $state | Add-Member -NotePropertyName Message -NotePropertyValue $dialogs[0]
      } elseif (([DateTime]::UtcNow - $state.DialogSeen).TotalSeconds -ge 0.7) {
        [void][FuzzWin]::DismissDialogs($v.Id)
        $state.Phase = 'dismissed'
        $state.Mark = [DateTime]::UtcNow
      }
      return
    }
    if ($elapsed -ge $TimeoutSeconds) {
      if ([FuzzWin]::HungWindows($v.Id, 3000) -gt 0) { Complete-Case $state 'HANG' 'window not responding'; return }
      Complete-Case $state 'OK' 'nodialog'
    }
    return
  }
  # dismissed: the viewer must close on its own.
  if ($dialogs.Count -gt 0 -and $elapsed -ge 1.0) {
    # A second message box (e.g. an error after the disconnect); dismiss it too.
    [void][FuzzWin]::DismissDialogs($v.Id)
  }
  if ($elapsed -ge 10) {
    if ([FuzzWin]::HungWindows($v.Id, 3000) -gt 0) { Complete-Case $state 'HANG' 'window not responding after message'; return }
    Complete-Case $state 'HANG' 'viewer did not exit after its message was dismissed'
  }
}

function Stop-Case($state) {
  if ($state.Viewer -and -not $state.Viewer.HasExited) {
    Stop-Process -Id $state.Viewer.Id -Force -ErrorAction SilentlyContinue
    [void]$state.Viewer.WaitForExit(5000)
  }
  if ($state.Server -and -not $state.Server.HasExited) {
    # The server notices the closed socket and prints its DONE line; give it a moment.
    [void]$state.Server.WaitForExit(3000)
  }
  if ($state.Server -and -not $state.Server.HasExited) {
    Stop-Process -Id $state.Server.Id -Force -ErrorAction SilentlyContinue
    [void]$state.Server.WaitForExit(5000)
  }
  $key = "$historyRoot\127.0.0.1::$($state.Port)"
  if (-not $preexisting[$state.Port] -and (Test-Path -LiteralPath $key)) { Remove-Item -LiteralPath $key -Recurse }
  $done = Read-ServerLine $state 'DONE'
  $state | Add-Member -NotePropertyName ServerDone -NotePropertyValue ([string]$done) -Force
}

$results = New-Object System.Collections.ArrayList
$active = @{}
$total = $queue.Count
$clock = [Diagnostics.Stopwatch]::StartNew()
try {
  while ($queue.Count -gt 0 -or $active.Count -gt 0) {
    foreach ($port in $ports) {
      if (-not $active.ContainsKey($port) -and $queue.Count -gt 0) { $active[$port] = Start-Case $queue.Dequeue() $port }
    }
    Start-Sleep -Milliseconds 150
    foreach ($port in @($active.Keys)) {
      $state = $active[$port]
      if ($state.Phase -eq 'listen') {
        if (Read-ServerLine $state 'LISTENING') { Start-Viewer $state }
        elseif ($state.Server.HasExited -or ([DateTime]::UtcNow - $state.Started).TotalSeconds -gt 15) {
          Complete-Case $state 'ERROR' ('server did not start: ' + ((Get-Content -LiteralPath $state.Err -ErrorAction SilentlyContinue) -join ' '))
        }
      } else {
        Test-Running $state
      }
      if ($state.Phase -eq 'done') {
        Stop-Case $state
        $active.Remove($port)
        [void]$results.Add($state)
        if ($state.Result -ne 'OK') {
          $last = if ($state.ServerDone) { ($state.ServerDone -replace '^DONE ', '') } else { '' }
          if ($last.Length -gt 220) { $last = $last.Substring(0, 220) + '...' }
          $detail = if ($state.Detail.Length -gt 160) { $state.Detail.Substring(0, 160) + '...' } else { $state.Detail }
          Write-Host ("{0,-5} {1}#{2}: {3} [{4}]" -f $state.Result, $state.Encoding, $state.Seed, $detail, $last)
        }
      }
    }
  }
} finally {
  foreach ($state in $active.Values) { Stop-Case $state }
  Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($CsvPath) {
  $results | Select-Object Encoding, Seed, Result, Detail, @{ n = 'Message'; e = { if ($_.PSObject.Properties['Message']) { $_.Message } } }, ServerDone |
    Export-Csv -NoTypeInformation -Path $CsvPath
}
$summary = foreach ($group in ($results | Group-Object Encoding)) {
  $ok = @($group.Group | Where-Object Result -eq 'OK')
  $noDialog = @($ok | Where-Object Detail -eq 'nodialog').Count
  '{0}: ok={1} (nodialog={2}) hang={3} crash={4} error={5}' -f $group.Name, $ok.Count, $noDialog,
    @($group.Group | Where-Object Result -eq 'HANG').Count, @($group.Group | Where-Object Result -eq 'CRASH').Count,
    @($group.Group | Where-Object Result -eq 'ERROR').Count
}
Write-Host ("{0} case(s) in {1:N0} s | {2}" -f $results.Count, $clock.Elapsed.TotalSeconds, ($summary -join ' | '))
$bad = @($results | Where-Object Result -ne 'OK')
if ($bad.Count -gt 0) { exit 1 }
