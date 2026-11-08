<#
.SYNOPSIS
  Real-input keyboard test: SendInput scancodes -> tvnviewer -> fake RFB server.

.DESCRIPTION
  Unlike keyboard-e2e-test.ps1, which posts WM_KEYDOWN/WM_KEYUP to the viewer
  window, this test goes through the real Windows input path (raw input
  thread, thread key state, TranslateMessage and the layout's dead-key
  buffer), the same path as a physical keyboard in windowed mode.

  SendInput reaches the foreground window, so the test refuses to send any
  event unless the foreground window belongs to the started viewer process,
  and releases every key it pressed in the finally block. Use it on an idle
  desktop: do not type while it runs. The fake server listens on 127.0.0.1
  (default port 5922, never 5900). ABNT2 is requested for the viewer thread
  only; the history entry of the test host is removed when this run created it.

.EXAMPLE
  .\tools\keyboard-sendinput-test.ps1 -Viewer .\build\bin\x64\Release\tvnviewer.exe
#>
param(
  [string]$Viewer = (Join-Path $PSScriptRoot '..\build\bin\x64\Release\tvnviewer.exe'),
  [int]$Port = 5922,
  [string]$Python = 'python',
  [int]$StepDelayMs = 80
)

$ErrorActionPreference = 'Stop'
if ($Port -eq 5900) { throw 'Port 5900 is reserved for the installed service.' }
$Viewer = (Resolve-Path $Viewer).Path
$serverScript = Join-Path $PSScriptRoot 'fake-rfb-keyrecorder.py'
$workDir = Join-Path $env:TEMP ("farsightvnc-si-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workDir | Out-Null
$logFile = Join-Path $workDir 'keys.jsonl'
$stopFile = Join-Path $workDir 'stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class SiWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint thread);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr LoadKeyboardLayout(string klid, uint flags);
  [DllImport("user32.dll")] public static extern bool UnloadKeyboardLayout(IntPtr hkl);
  [DllImport("user32.dll")] public static extern int GetKeyboardLayoutList(int n, IntPtr[] list);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);

  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public INPUTUNION u; }
  [DllImport("user32.dll", SetLastError = true)] public static extern uint SendInput(uint n, INPUT[] inputs, int size);

  public static bool SendScan(ushort scan, bool up, bool extended) {
    INPUT[] i = new INPUT[1];
    i[0].type = 1;
    i[0].u.ki.wScan = scan;
    i[0].u.ki.dwFlags = 0x0008 | (up ? 0x0002u : 0u) | (extended ? 0x0001u : 0u); // SCANCODE
    return SendInput(1, i, Marshal.SizeOf(typeof(INPUT))) == 1;
  }

  public static uint OwnerPid(IntPtr h) { uint p; GetWindowThreadProcessId(h, out p); return p; }
  public static uint OwnerThread(IntPtr h) { uint p; return GetWindowThreadProcessId(h, out p); }

  public static IntPtr FindTopWindow(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate (IntPtr h, IntPtr l) {
      if (OwnerPid(h) == pid && IsWindowVisible(h)) {
        StringBuilder sb = new StringBuilder(256); GetClassName(h, sb, 256);
        if (sb.ToString() == "TvnWindowClass") { found = h; return false; }
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }

  public static IntPtr FindDesktopChild(IntPtr top) {
    IntPtr found = IntPtr.Zero;
    EnumChildWindows(top, delegate (IntPtr h, IntPtr l) {
      StringBuilder sb = new StringBuilder(256); GetClassName(h, sb, 256);
      if (sb.ToString() == "TvnWindowClass") { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }

  public static IntPtr[] LayoutList() {
    int n = GetKeyboardLayoutList(0, null);
    IntPtr[] l = new IntPtr[Math.Max(n, 1)];
    n = GetKeyboardLayoutList(n, l);
    Array.Resize(ref l, n);
    return l;
  }
}
'@

$WM_INPUTLANGCHANGEREQUEST = 0x50
$ABNT2_LANGID = 0x0416
$SC = @{ LShift = 0x2A; RShift = 0x36; A = 0x1E; AbntC1 = 0x73; Six = 0x07; DeadAcute = 0x1A; Space = 0x39 }

$state = @{ Server = $null; Viewer = $null; LoadedLayout = [IntPtr]::Zero }
$pressed = New-Object System.Collections.ArrayList
$historyKey = "HKCU:\Software\TightVNC\Viewer\History\127.0.0.1::$Port"
$historyExisted = Test-Path -LiteralPath $historyKey
$results = New-Object System.Collections.ArrayList

function Read-Log {
  if (-not (Test-Path -LiteralPath $logFile)) { return @() }
  $fs = New-Object System.IO.FileStream($logFile, 'Open', 'Read', 'ReadWrite')
  try { $text = (New-Object System.IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
  @($text -split "`r?`n" | Where-Object { $_ })
}

function Get-KeyEvents {
  @(Read-Log | Where-Object { $_ -match '"msg": "KeyEvent"' } | ForEach-Object { $_ | ConvertFrom-Json })
}

function Wait-LogEvent([string]$name, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    if (@(Read-Log | Where-Object { $_ -match "`"event`": `"$name`"" }).Count -gt 0) { return $true }
    Start-Sleep -Milliseconds 100
  }
  return $false
}

function Assert-ViewerForeground {
  $fg = [SiWin]::GetForegroundWindow()
  if ([SiWin]::OwnerPid($fg) -ne $state.Viewer.Id) { throw 'the viewer is not the foreground window; refusing to send input' }
}

function Send-Scan([int]$scan, [bool]$up) {
  Assert-ViewerForeground
  if (-not [SiWin]::SendScan([uint16]$scan, $up, $false)) { throw 'SendInput failed' }
  if ($up) { $pressed.Remove($scan) } elseif (-not $pressed.Contains($scan)) { [void]$pressed.Add($scan) }
  Start-Sleep -Milliseconds $StepDelayMs
}

# step = @(kind, scan): kind d (down) or u (up)
function Invoke-Case([string]$name, [object[]]$steps, [object[]]$expected) {
  $before = (Get-KeyEvents).Count
  foreach ($s in $steps) { Send-Scan $s[1] ($s[0] -eq 'u') }
  Start-Sleep -Milliseconds 400
  $events = @(Get-KeyEvents | Select-Object -Skip $before)
  $actual = @($events | ForEach-Object { '{0}{1:x}' -f $(if ($_.down) { '+' } else { '-' }), $_.keysym })
  $held = @{}
  $balanced = $true
  foreach ($e in $events) {
    if ($e.down) { $held[$e.keysym] = $true }
    elseif ($held.ContainsKey($e.keysym)) { $held.Remove($e.keysym) }
    else { $balanced = $false }
  }
  if ($held.Count -gt 0) { $balanced = $false }
  $same = ($actual -join ' ') -eq (@($expected) -join ' ')
  [void]$results.Add([pscustomobject]@{ Case = $name; Passed = ($same -and $balanced); Balanced = $balanced
      Expected = (@($expected) -join ' '); Actual = ($actual -join ' ') })
}

try {
  if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
    throw "Port $Port is already in use."
  }
  $state.Server = Start-Process -FilePath $Python -PassThru -WindowStyle Hidden `
    -ArgumentList @('-I', "`"$serverScript`"", '--port', $Port, '--log', "`"$logFile`"", '--stop-file', "`"$stopFile`"", '--duration', 300)
  if (-not (Wait-LogEvent 'listening' 15)) { throw 'fake server did not start' }

  $state.Viewer = Start-Process -FilePath $Viewer -PassThru -ArgumentList @('-host=127.0.0.1', "-port=$Port")
  if (-not (Wait-LogEvent 'connected' 20)) { throw 'viewer did not connect to the fake server' }

  $top = [IntPtr]::Zero; $desk = [IntPtr]::Zero
  for ($i = 0; $i -lt 100 -and $desk -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 100
    $top = [SiWin]::FindTopWindow([uint32]$state.Viewer.Id)
    if ($top -ne [IntPtr]::Zero) { $desk = [SiWin]::FindDesktopChild($top) }
  }
  if ($desk -eq [IntPtr]::Zero) { throw 'viewer desktop window not found' }
  $thread = [SiWin]::OwnerThread($desk)
  [void][SiWin]::SetForegroundWindow($top)
  Start-Sleep -Milliseconds 500

  $current = [SiWin]::GetKeyboardLayout($thread).ToInt64()
  if (($current -band 0xFFFF) -ne $ABNT2_LANGID) {
    $loaded = [SiWin]::LayoutList()
    $hkl = [SiWin]::LoadKeyboardLayout('00000416', 0x80)
    if ($hkl -eq [IntPtr]::Zero) { throw 'cannot load the ABNT2 layout' }
    if ($loaded -notcontains $hkl) { $state.LoadedLayout = $hkl }
    [void][SiWin]::PostMessage($desk, [uint32]$WM_INPUTLANGCHANGEREQUEST, [IntPtr]::Zero, $hkl)
    for ($i = 0; $i -lt 30; $i++) {
      Start-Sleep -Milliseconds 100
      if (([SiWin]::GetKeyboardLayout($thread).ToInt64() -band 0xFFFF) -eq $ABNT2_LANGID) { break }
    }
  }
  $now = [SiWin]::GetKeyboardLayout($thread).ToInt64() -band 0xFFFF
  if ($now -ne $ABNT2_LANGID) { throw 'ABNT2 could not be activated for the viewer thread' }
  Assert-ViewerForeground

  $ls = @('d', $SC.LShift); $lsU = @('u', $SC.LShift)
  $rs = @('d', $SC.RShift); $rsU = @('u', $SC.RShift)
  $qd = @('d', $SC.AbntC1); $qu = @('u', $SC.AbntC1)
  $sd = @('d', $SC.Six);    $su = @('u', $SC.Six)
  $ad = @('d', $SC.A);      $au = @('u', $SC.A)
  Invoke-Case 'plain 6 is sent as KP_6' @($sd, $su) @('+ffb6', '-ffb6')
  Invoke-Case "'?' LShift, key released first" @($ls, $qd, $qu, $lsU) @('+ffe1', '-ffe1', '+3f', '+ffe1', '-3f', '-ffe1')
  Invoke-Case "'?' LShift, Shift released first" @($ls, $qd, $lsU, $qu) @('+ffe1', '-ffe1', '+3f', '+ffe1', '-ffe1', '-3f')
  Invoke-Case "'?' RShift, Shift released first" @($rs, $qd, $rsU, $qu) @('+ffe1', '-ffe1', '+3f', '+ffe1', '-ffe1', '-3f')
  Invoke-Case "'?' RShift, key released first" @($rs, $qd, $qu, $rsU) @('+ffe1', '-ffe1', '+3f', '+ffe1', '-3f', '-ffe1')
  Invoke-Case 'both Shifts, LShift released first (lost key-up), then a' @($ls, $rs, $lsU, $rsU, $ad, $au) @('+ffe1', '-ffe1', '+61', '-61')
  Invoke-Case "plain a after '?'" @($ad, $au) @('+61', '-61')
  Invoke-Case 'plain 6 again (KP_6)' @($sd, $su) @('+ffb6', '-ffb6')
  Invoke-Case 'Shift+6 (dead diaeresis) then space gives diaeresis' @($ls, $sd, $su, $lsU, @('d', $SC.Space), @('u', $SC.Space)) @('+ffe1', '-ffe1', '+a8', '-a8')
  Invoke-Case 'plain 6 after the dead key (KP_6)' @($sd, $su) @('+ffb6', '-ffb6')
  Invoke-Case 'dead acute then a gives a-acute' @(@('d', $SC.DeadAcute), @('u', $SC.DeadAcute), $ad, $au) @('+e1', '-e1')
  Invoke-Case 'Shift+a, Shift released first' @($ls, $ad, $lsU, $au) @('+ffe1', '+41', '-ffe1', '-41')
} catch {
  [void]$results.Add([pscustomobject]@{ Case = 'harness'; Passed = $false; Balanced = $false; Expected = 'no error'; Actual = $_.Exception.Message })
} finally {
  foreach ($scan in @($pressed)) { [void][SiWin]::SendScan([uint16]$scan, $true, $false) }
  if ($state.Viewer -and -not $state.Viewer.HasExited) { Stop-Process -Id $state.Viewer.Id -Force -ErrorAction SilentlyContinue }
  New-Item -ItemType File -Path $stopFile -Force -ErrorAction SilentlyContinue | Out-Null
  if ($state.Server) {
    [void]$state.Server.WaitForExit(5000)
    if (-not $state.Server.HasExited) { Stop-Process -Id $state.Server.Id -Force -ErrorAction SilentlyContinue }
  }
  if ($state.LoadedLayout -ne [IntPtr]::Zero) { [void][SiWin]::UnloadKeyboardLayout($state.LoadedLayout) }
  if (-not $historyExisted -and (Test-Path -LiteralPath $historyKey)) { Remove-Item -LiteralPath $historyKey -Recurse }
  Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

$results | Format-Table Case, Passed, Balanced, Expected, Actual -AutoSize -Wrap | Out-String -Width 220 | Write-Host
$failed = @($results | Where-Object { -not $_.Passed })
Write-Host ("{0} case(s), {1} failed" -f $results.Count, $failed.Count)
$stuck = @(0x10, 0xA0, 0xA1 | Where-Object { [SiWin]::GetAsyncKeyState($_) -band 0x8000 })
if ($stuck.Count -gt 0) { Write-Host 'WARNING: a Shift key is still down on this machine.' }
if ($failed.Count -gt 0) { exit 1 }
Write-Host 'Real-input keyboard test passed.'
