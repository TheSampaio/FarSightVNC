<#
.SYNOPSIS
  End-to-end keyboard test: tvnviewer -> fake RFB server (loopback, no input).

.DESCRIPTION
  Starts tools/fake-rfb-keyrecorder.py on 127.0.0.1 (default port 5921, never
  5900), starts the built tvnviewer.exe against it and delivers keystrokes
  ONLY by PostMessage(WM_KEYDOWN/WM_KEYUP) with a complete lParam to the
  viewer's own desktop child window (same path as the fullscreen keyboard
  hook). Nothing is injected into the real desktop (no SendInput/keybd_event)
  and no other window receives a message: the target handle is checked to
  belong to the viewer process before every post.

  The ABNT2 layout is requested for the viewer thread only
  (WM_INPUTLANGCHANGEREQUEST to the viewer window). The recorded KeyEvent
  keysyms are compared with the expected sequences and every press must have a
  release of the same keysym. The viewer history entry of the test host is
  removed only when this run created it. All started processes are stopped.

.EXAMPLE
  .\tools\keyboard-e2e-test.ps1 -Viewer .\x64\Release\tvnviewer.exe
#>
param(
  [string]$Viewer = (Join-Path $PSScriptRoot '..\x64\Release\tvnviewer.exe'),
  [int]$Port = 5921,
  [string]$Python = 'python',
  [int]$StepDelayMs = 60
)

$ErrorActionPreference = 'Stop'
if ($Port -eq 5900) { throw 'Port 5900 is reserved for the installed service.' }
$Viewer = (Resolve-Path $Viewer).Path
$serverScript = Join-Path $PSScriptRoot 'fake-rfb-keyrecorder.py'
$workDir = Join-Path $env:TEMP ("farsightvnc-e2e-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workDir | Out-Null
$logFile = Join-Path $workDir 'keys.jsonl'
$stopFile = Join-Path $workDir 'stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class E2eWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint thread);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr LoadKeyboardLayout(string klid, uint flags);
  [DllImport("user32.dll")] public static extern bool UnloadKeyboardLayout(IntPtr hkl);
  [DllImport("user32.dll")] public static extern int GetKeyboardLayoutList(int n, IntPtr[] list);

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

$WM_KEYDOWN = 0x100; $WM_KEYUP = 0x101; $WM_INPUTLANGCHANGEREQUEST = 0x50
$ABNT2_LANGID = 0x0416
$VK = @{ LShift = 0xA0; RShift = 0xA1; Shift = 0x10; A = 0x41; AbntC1 = 0xC1; DeadAcute = 0xDB }
$SC = @{ LShift = 0x2A; RShift = 0x36; A = 0x1E; AbntC1 = 0x73; DeadAcute = 0x1A }

$state = @{ Server = $null; Viewer = $null; LoadedLayout = [IntPtr]::Zero }
$historyKey = "HKCU:\Software\TightVNC\Viewer\History\127.0.0.1::$Port"
$historyExisted = Test-Path -LiteralPath $historyKey
$results = New-Object System.Collections.ArrayList

function Read-Log {
  if (-not (Test-Path -LiteralPath $logFile)) { return @() }
  # The server keeps the file open for writing: share it.
  $fs = New-Object System.IO.FileStream($logFile, 'Open', 'Read', 'ReadWrite')
  try {
    $reader = New-Object System.IO.StreamReader($fs)
    $text = $reader.ReadToEnd()
  } finally { $fs.Dispose() }
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

function Send-Key([IntPtr]$target, [int]$vk, [int]$scan, [bool]$up, [bool]$extended, [bool]$repeat) {
  # Guard: only the viewer's own window may ever receive a message.
  if ([E2eWin]::OwnerPid($target) -ne $state.Viewer.Id) { throw 'refusing to post to a window of another process' }
  [long]$lp = 1 -bor ($scan -shl 16)
  if ($extended) { $lp = $lp -bor 0x1000000 }
  if ($up) { $lp = $lp -bor 0xC0000000 } elseif ($repeat) { $lp = $lp -bor 0x40000000 }
  $msg = if ($up) { $WM_KEYUP } else { $WM_KEYDOWN }
  if (-not [E2eWin]::PostMessage($target, [uint32]$msg, [IntPtr]$vk, [IntPtr]$lp)) { throw 'PostMessage failed' }
  Start-Sleep -Milliseconds $StepDelayMs
}

# step = @(kind, vk, scan): kind d (down), u (up), r (autorepeat down)
function Invoke-Case([string]$name, [IntPtr]$target, [object[]]$steps, [object[]]$expected) {
  $before = (Get-KeyEvents).Count
  foreach ($s in $steps) {
    Send-Key $target $s[1] $s[2] ($s[0] -eq 'u') $false ($s[0] -eq 'r')
  }
  Start-Sleep -Milliseconds 300
  $events = @(Get-KeyEvents | Select-Object -Skip $before)
  $actual = @($events | ForEach-Object { '{0}{1:x}' -f $(if ($_.down) { '+' } else { '-' }), $_.keysym })
  $want = @($expected)
  $held = @{}
  $balanced = $true
  foreach ($e in $events) {
    if ($e.down) { $held[$e.keysym] = $true }
    elseif ($held.ContainsKey($e.keysym)) { $held.Remove($e.keysym) }
    else { $balanced = $false }
  }
  if ($held.Count -gt 0) { $balanced = $false }
  $same = ($actual -join ' ') -eq ($want -join ' ')
  [void]$results.Add([pscustomobject]@{ Case = $name; Passed = ($same -and $balanced); Balanced = $balanced
      Expected = ($want -join ' '); Actual = ($actual -join ' ') })
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

  $desk = [IntPtr]::Zero
  for ($i = 0; $i -lt 100 -and $desk -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 100
    $top = [E2eWin]::FindTopWindow([uint32]$state.Viewer.Id)
    if ($top -ne [IntPtr]::Zero) { $desk = [E2eWin]::FindDesktopChild($top) }
  }
  if ($desk -eq [IntPtr]::Zero) { throw 'viewer desktop window not found' }
  $thread = [E2eWin]::OwnerThread($desk)

  # Request ABNT2 for the viewer thread only.
  $current = [E2eWin]::GetKeyboardLayout($thread).ToInt64()
  if (($current -band 0xFFFF) -ne $ABNT2_LANGID) {
    $loaded = [E2eWin]::LayoutList()
    $hkl = [E2eWin]::LoadKeyboardLayout('00010416', 0x80)   # KLF_NOTELLSHELL, not activated
    if ($hkl -eq [IntPtr]::Zero) { throw 'cannot load the ABNT2 layout' }
    if ($loaded -notcontains $hkl) { $state.LoadedLayout = $hkl }
    [void][E2eWin]::PostMessage($desk, [uint32]$WM_INPUTLANGCHANGEREQUEST, [IntPtr]::Zero, $hkl)
    for ($i = 0; $i -lt 30; $i++) {
      Start-Sleep -Milliseconds 100
      if (([E2eWin]::GetKeyboardLayout($thread).ToInt64() -band 0xFFFF) -eq $ABNT2_LANGID) { break }
    }
  }
  $now = [E2eWin]::GetKeyboardLayout($thread).ToInt64() -band 0xFFFF
  [void]$results.Add([pscustomobject]@{ Case = 'viewer thread uses the ABNT2 layout'; Passed = ($now -eq $ABNT2_LANGID)
      Balanced = $true; Expected = '0x0416'; Actual = ('0x{0:X4}' -f $now) })
  if ($now -ne $ABNT2_LANGID) { throw 'ABNT2 could not be activated for the viewer thread' }

  $ls = @('d', $VK.LShift, $SC.LShift);  $lsU = @('u', $VK.LShift, $SC.LShift)
  $rs = @('d', $VK.RShift, $SC.RShift);  $rsU = @('u', $VK.RShift, $SC.RShift)
  $kd = @('d', $VK.AbntC1, $SC.AbntC1);  $ku = @('u', $VK.AbntC1, $SC.AbntC1)
  $aD = @('d', $VK.A, $SC.A);            $aU = @('u', $VK.A, $SC.A)
  $aR = @('r', $VK.A, $SC.A)
  Invoke-Case "ABNT2 '?' LShift, key released first" $desk @($ls, $kd, $ku, $lsU) @('+ffe1', '+3f', '-3f', '-ffe1')
  Invoke-Case "ABNT2 '?' LShift, Shift released first" $desk @($ls, $kd, $lsU, $ku) @('+ffe1', '+3f', '-ffe1', '-3f')
  Invoke-Case "ABNT2 '?' RShift, key released first" $desk @($rs, $kd, $ku, $rsU) @('+ffe2', '+3f', '-3f', '-ffe2')
  Invoke-Case "ABNT2 '?' RShift, Shift released first" $desk @($rs, $kd, $rsU, $ku) @('+ffe2', '+3f', '-ffe2', '-3f')
  Invoke-Case "ABNT2 '?' generic VK_SHIFT with scan 0x36 (windowed Right Shift)" $desk `
    @(@('d', $VK.Shift, $SC.RShift), $kd, @('u', $VK.Shift, $SC.RShift), $ku) @('+ffe2', '+3f', '-ffe2', '-3f')
  Invoke-Case 'ABNT2 plain a' $desk @($aD, $aU) @('+61', '-61')
  Invoke-Case 'ABNT2 Shift+a, Shift released first' $desk @($ls, $aD, $lsU, $aU) @('+ffe1', '+41', '-ffe1', '-41')
  Invoke-Case 'ABNT2 autorepeat a: same keysym, one release' $desk @($aD, $aR, $aR, $aU) @('+61', '+61', '+61', '-61')
  Invoke-Case 'ABNT2 dead acute then a gives a-acute' $desk `
    @(@('d', $VK.DeadAcute, $SC.DeadAcute), @('u', $VK.DeadAcute, $SC.DeadAcute), $aD, $aU) @('+e1', '-e1')
} catch {
  [void]$results.Add([pscustomobject]@{ Case = 'harness'; Passed = $false; Balanced = $false; Expected = 'no error'; Actual = $_.Exception.Message })
} finally {
  if ($state.Viewer -and -not $state.Viewer.HasExited) { Stop-Process -Id $state.Viewer.Id -Force -ErrorAction SilentlyContinue }
  New-Item -ItemType File -Path $stopFile -Force -ErrorAction SilentlyContinue | Out-Null
  if ($state.Server) {
    [void]$state.Server.WaitForExit(5000)
    if (-not $state.Server.HasExited) { Stop-Process -Id $state.Server.Id -Force -ErrorAction SilentlyContinue }
  }
  if ($state.LoadedLayout -ne [IntPtr]::Zero) { [void][E2eWin]::UnloadKeyboardLayout($state.LoadedLayout) }
  if (-not $historyExisted -and (Test-Path -LiteralPath $historyKey)) { Remove-Item -LiteralPath $historyKey -Recurse }
  Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

$results | Format-Table Case, Passed, Balanced, Expected, Actual -AutoSize -Wrap | Out-String -Width 220 | Write-Host
$failed = @($results | Where-Object { -not $_.Passed })
Write-Host ("{0} case(s), {1} failed" -f $results.Count, $failed.Count)
if ($failed.Count -gt 0) { exit 1 }
Write-Host 'Keyboard end-to-end test passed.'
