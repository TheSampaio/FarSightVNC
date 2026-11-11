<#
.SYNOPSIS
  Phase 2 harness: pointer mapping, wheel/X buttons and DPI report for tvnviewer.

.DESCRIPTION
  Starts tools\fake-rfb-server.ps1 (loopback only), launches the given viewer
  against it and drives the viewer with PostMessage to its remote desktop
  child window. Nothing is injected into the real desktop (no SendInput,
  SetCursorPos or mouse_event). The server's JSON log is then compared with
  what the viewer should have sent.

  Checks per (framebuffer size, scale) case:
    - Pointer mapping. Assumed model (taken from ScaleManager.cpp):
        disp  = ceil(fb * num / den)         scaled image size per axis
                (num/den = scale/100, or for "auto" the fit ratio
                 min(clientW/W, clientH/H))
        cent  = disp < client ? floor((client - disp) / 2) : 0
        srv   = floor((clientCoord - cent) * den / num)
      Expected: the first image pixel (cent) maps to 0 and the last image
      pixel maps to fb-1 (tolerance max(1, ceil(den/num)) when scaled, exact
      at 1:1). A mid point must be within the same tolerance of the formula.
      When the image is larger than the window (disp > client, scrollbars)
      the scroll offset is assumed to be 0 and the last visible client pixel
      is checked against the formula instead of fb-1.
    - Left/middle/right button down+up masks (1, 2, 4).
    - Vertical wheel -> buttons 4/5 (masks 8/16) press+release.
    - Horizontal wheel -> buttons 6/7 (masks 32/64) press+release.
    - X1/X2 buttons: no mask bits are sent unless the client advertised an
      extended-mouse-buttons pseudo-encoding (-316) in SetEncodings.
  It also reports the viewer's DPI awareness and window DPI (INFO rows).

  With -DisplayModes (FarSightVNC viewers only) the first scale of each size
  also drives the viewer's own "Display" system-menu commands with
  WM_SYSCOMMAND/WM_COMMAND posted to its windows and checks:
    - the initial window lies inside the monitor work area (tall desktops),
    - Fit to window (0xA830) and Original (0xA820) change the pointer model,
    - "Fullscreen across all monitors" (0xA800) covers the virtual screen,
      "Fullscreen on current monitor" (0xA810) covers the monitor while fullscreen,
      and leaving fullscreen restores a captioned window.
  The fullscreen steps briefly show a topmost window of the viewer and install
  its keyboard hook for about a second.

  Coordinates are read and sent in the viewer window's own DPI context
  (SetThreadDpiAwarenessContext), so DPI-unaware viewers are handled.

  Cleanup (always): kills only the viewer PID and server PID it started,
  removes HKCU\Software\TightVNC\Viewer\History\127.0.0.1::<port> if it did
  not exist before and restores any other changed value of
  HKCU\Software\TightVNC\Viewer.

  Exit code 1 if any check FAILs.

  Limitations: PostMessage cannot test real touch/pen input, real wheel
  accumulation from hardware, monitor changes or system DPI scales other than
  the current one.

.EXAMPLE
  .\tools\phase2-test.ps1 -Viewer .\build\bin\x64\Release\fsvncviewer.exe
  .\tools\phase2-test.ps1 -Viewer C:\path\fsvncviewer.exe -Sizes 1920x1080 -Scales 100,auto
#>
param(
  [Parameter(Mandatory = $true)][string]$Viewer,
  [int]$Port = 5932,
  [string[]]$Sizes = @('1920x1080', '1080x1920'),
  [string[]]$Scales = @('100', '50', 'auto'),
  [switch]$DisplayModes,
  [int]$ConnectTimeoutSeconds = 25,
  [string]$WorkDir = (Join-Path $env:TEMP 'farsightvnc-phase2')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Viewer)) { throw "Viewer not found: $Viewer" }
$Viewer = (Resolve-Path -LiteralPath $Viewer).Path
$serverScript = Join-Path $PSScriptRoot 'fake-rfb-server.ps1'
if (-not (Test-Path -LiteralPath $serverScript)) { throw "Missing $serverScript" }
if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
  throw "Port $Port is already in use."
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class FsvNative
{
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr h, int index);
  [DllImport("user32.dll")] static extern bool SystemParametersInfo(uint action, uint param, out RECT r, uint flags);
  [DllImport("user32.dll")] static extern int GetSystemMetrics(int index);
  [DllImport("user32.dll")] static extern IntPtr GetWindowDpiAwarenessContext(IntPtr h);
  [DllImport("user32.dll")] static extern int GetAwarenessFromDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] static extern bool AreDpiAwarenessContextsEqual(IntPtr a, IntPtr b);
  [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] static extern uint GetDpiForSystem();
  [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("shcore.dll")] static extern int GetProcessDpiAwareness(IntPtr hProcess, out int value);
  [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }

  public static IntPtr[] TopWindows(uint pid)
  {
    List<IntPtr> r = new List<IntPtr>();
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint wp; GetWindowThreadProcessId(h, out wp);
      if (wp == pid && IsWindowVisible(h)) r.Add(h);
      return true;
    }, IntPtr.Zero);
    return r.ToArray();
  }

  public static IntPtr[] Descendants(IntPtr parent)
  {
    List<IntPtr> r = new List<IntPtr>();
    EnumChildWindows(parent, delegate(IntPtr h, IntPtr p) { r.Add(h); return true; }, IntPtr.Zero);
    return r.ToArray();
  }

  public static bool Visible(IntPtr h) { return IsWindowVisible(h); }

  public static string ClassName(IntPtr h)
  {
    StringBuilder sb = new StringBuilder(256);
    GetClassName(h, sb, 256);
    return sb.ToString();
  }

  // Client size in the window's own DPI context.
  public static int[] ClientSize(IntPtr h)
  {
    IntPtr old = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
    try { RECT r; GetClientRect(h, out r); return new int[] { r.R - r.L, r.B - r.T }; }
    finally { if (old != IntPtr.Zero) SetThreadDpiAwarenessContext(old); }
  }

  public static int[] ToScreen(IntPtr h, int x, int y)
  {
    IntPtr old = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
    try { POINT p; p.X = x; p.Y = y; ClientToScreen(h, ref p); return new int[] { p.X, p.Y }; }
    finally { if (old != IntPtr.Zero) SetThreadDpiAwarenessContext(old); }
  }

  // Window rectangle L,T,R,B and style in the window's own DPI context.
  public static int[] WindowRect(IntPtr h)
  {
    IntPtr old = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
    try { RECT r; GetWindowRect(h, out r); return new int[] { r.L, r.T, r.R, r.B }; }
    finally { if (old != IntPtr.Zero) SetThreadDpiAwarenessContext(old); }
  }

  public static int[] WorkArea(IntPtr h)
  {
    IntPtr old = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
    try { RECT r; SystemParametersInfo(0x30, 0, out r, 0); return new int[] { r.L, r.T, r.R, r.B }; }
    finally { if (old != IntPtr.Zero) SetThreadDpiAwarenessContext(old); }
  }

  // Virtual screen L,T,R,B (SM_*VIRTUALSCREEN) in the window's own DPI context.
  public static int[] VirtualScreen(IntPtr h)
  {
    IntPtr old = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
    try { int l = GetSystemMetrics(76), t = GetSystemMetrics(77); return new int[] { l, t, l + GetSystemMetrics(78), t + GetSystemMetrics(79) }; }
    finally { if (old != IntPtr.Zero) SetThreadDpiAwarenessContext(old); }
  }

  public static long Style(IntPtr h, int index) { return (uint)GetWindowLong(h, index); }

  public static bool Post(IntPtr h, uint msg, long w, long l)
  {
    return PostMessage(h, msg, new IntPtr(w), new IntPtr(l));
  }

  public static long Pos(int x, int y) { return (long)(((uint)(y & 0xFFFF) << 16) | (uint)(x & 0xFFFF)); }

  public static string Dpi(IntPtr h, uint pid)
  {
    IntPtr ctx = GetWindowDpiAwarenessContext(h);
    string kind = "unknown";
    if (AreDpiAwarenessContextsEqual(ctx, new IntPtr(-4))) kind = "PerMonitorV2";
    else if (AreDpiAwarenessContextsEqual(ctx, new IntPtr(-3))) kind = "PerMonitor";
    else if (AreDpiAwarenessContextsEqual(ctx, new IntPtr(-2))) kind = "System";
    else if (AreDpiAwarenessContextsEqual(ctx, new IntPtr(-5))) kind = "Unaware(GDI-scaled)";
    else if (AreDpiAwarenessContextsEqual(ctx, new IntPtr(-1))) kind = "Unaware";
    int procAw = -1;
    IntPtr hp = OpenProcess(0x1000, false, pid);
    if (hp != IntPtr.Zero) { GetProcessDpiAwareness(hp, out procAw); CloseHandle(hp); }
    string[] names = { "Unaware", "System", "PerMonitor" };
    string pn = (procAw >= 0 && procAw < 3) ? names[procAw] : "n/a";
    return "window context=" + kind + " (GetAwarenessFromDpiAwarenessContext=" + GetAwarenessFromDpiAwarenessContext(ctx) +
           "), process=" + pn + ", GetDpiForWindow=" + GetDpiForWindow(h) + ", GetDpiForSystem=" + GetDpiForSystem();
  }
}
'@

$results = New-Object System.Collections.ArrayList
function Add-Result([string]$size, [string]$scale, [string]$check, [string]$expected, [string]$actual, [string]$status) {
  [void]$results.Add([pscustomobject]@{ Size = $size; Scale = $scale; Check = $check; Expected = $expected; Actual = $actual; Result = $status })
}

# --- JSON log access -------------------------------------------------------
$script:logFile = $null
function Get-Log {
  $fs = New-Object IO.FileStream($script:logFile, 'Open', 'Read', 'ReadWrite')
  try {
    $text = (New-Object IO.StreamReader($fs)).ReadToEnd()
  } finally { $fs.Dispose() }
  $entries = @()
  foreach ($line in ($text -split "`n")) {
    if ($line.StartsWith('{') -and $line.TrimEnd().EndsWith('}')) { $entries += ($line | ConvertFrom-Json) }
  }
  $entries   # callers wrap in @() to get an array
}
function Wait-Log([scriptblock]$predicate, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  do {
    $hit = @(Get-Log | Where-Object $predicate)
    if ($hit.Count -gt 0) { return $true }
    Start-Sleep -Milliseconds 200
  } while ((Get-Date) -lt $deadline)
  $false
}
# Runs an action and returns the PointerEvent log entries it caused.
function Invoke-Probe([scriptblock]$action) {
  $before = @(Get-Log).Count
  & $action
  Start-Sleep -Milliseconds 400
  $new = @(Get-Log)
  @($new | Select-Object -Skip $before | Where-Object { $_.msg -eq 'PointerEvent' })
}

# --- registry backup -------------------------------------------------------
$viewerKey = 'HKCU:\Software\TightVNC\Viewer'
$historyKey = "$viewerKey\History\127.0.0.1::$Port"
$historyExisted = Test-Path -LiteralPath $historyKey
$viewerKeyExisted = Test-Path -LiteralPath $viewerKey
$savedValues = @{}
if ($viewerKeyExisted) {
  $item = Get-Item -LiteralPath $viewerKey
  foreach ($n in $item.GetValueNames()) {
    $savedValues[$n] = @{ Value = $item.GetValue($n, $null, 'DoNotExpandEnvironmentNames'); Kind = $item.GetValueKind($n) }
  }
}
function Restore-Registry {
  if (-not $historyExisted -and (Test-Path -LiteralPath $historyKey)) { Remove-Item -LiteralPath $historyKey -Recurse }
  if ($viewerKeyExisted) {
    $item = Get-Item -LiteralPath $viewerKey
    foreach ($n in $item.GetValueNames()) {
      if (-not $savedValues.ContainsKey($n)) { Remove-ItemProperty -LiteralPath $viewerKey -Name $n }
    }
    foreach ($n in $savedValues.Keys) {
      $cur = $item.GetValue($n, $null, 'DoNotExpandEnvironmentNames')
      if (($cur -join ',') -ne ($savedValues[$n].Value -join ',')) {
        Set-ItemProperty -LiteralPath $viewerKey -Name $n -Value $savedValues[$n].Value -Type $savedValues[$n].Kind
      }
    }
  } elseif (Test-Path -LiteralPath $viewerKey) {
    Remove-Item -LiteralPath $viewerKey -Recurse
  }
}

# --- geometry model --------------------------------------------------------
function Get-Axis([int]$client, [int]$src, [long]$num, [long]$den) {
  $disp = [int][math]::Ceiling($src * $num / [double]$den)
  $cent = 0
  if ($disp -lt $client) { $cent = [int][math]::Floor(($client - $disp) / 2) }
  $visible = [math]::Min($disp, $client)
  [pscustomobject]@{ Disp = $disp; Cent = $cent; First = $cent; Last = $cent + $visible - 1; Fits = ($disp -le $client); Src = $src; Num = $num; Den = $den }
}
function Get-Expected($axis, [int]$c) { [int][math]::Floor(($c - $axis.Cent) * $axis.Den / [double]$axis.Num) }

function Test-Point([string]$size, [string]$scale, [string]$name, $child, $ax, $ay, [int]$cx, [int]$cy, [int]$ex, [int]$ey, [int]$tol) {
  $ev = Invoke-Probe { [void][FsvNative]::Post($child, 0x200, 0, [FsvNative]::Pos($cx, $cy)) }
  $expected = "($ex,$ey) +/-$tol from client ($cx,$cy)"
  if ($ev.Count -eq 0) { Add-Result $size $scale $name $expected 'no PointerEvent' 'FAIL'; return }
  $e = $ev[-1]
  $ok = ([math]::Abs($e.x - $ex) -le $tol) -and ([math]::Abs($e.y - $ey) -le $tol)
  Add-Result $size $scale $name $expected "($($e.x),$($e.y))" $(if ($ok) { 'PASS' } else { 'FAIL' })
}

function Test-Sequence([string]$size, [string]$scale, [string]$name, [int[]]$expectedMasks, $events) {
  $actual = @($events | Where-Object { $_ } | ForEach-Object { [int]$_.mask })
  $ok = ($actual -join ',') -eq ($expectedMasks -join ',')
  Add-Result $size $scale $name ($expectedMasks -join ',') $(if ($actual.Count) { $actual -join ',' } else { 'no events' }) $(if ($ok) { 'PASS' } else { 'FAIL' })
}

# Pointer corners for the current window state. $mode is '100' or 'auto'.
function Test-Corners([string]$size, [string]$label, $child, [int]$w, [int]$h, [string]$mode) {
  Start-Sleep -Milliseconds 1500
  $cs = [FsvNative]::ClientSize($child)
  if ($mode -eq 'auto') {
    if ($cs[0] * $h -le $cs[1] * $w) { $num = $cs[0]; $den = $w } else { $num = $cs[1]; $den = $h }
  } else { $num = 100; $den = 100 }
  $ax = Get-Axis $cs[0] $w $num $den
  $ay = Get-Axis $cs[1] $h $num $den
  $tol = if ($num -eq $den) { 0 } else { [int][math]::Max(1, [math]::Ceiling($den / [double]$num)) }
  $mx = $ax.First + [int][math]::Floor(($ax.Last - $ax.First) / 2)
  $my = $ay.First + [int][math]::Floor(($ay.Last - $ay.First) / 2)
  Test-Point $size $label "$label mid" $child $ax $ay $mx $my (Get-Expected $ax $mx) (Get-Expected $ay $my) ([math]::Max($tol, 1))
  Test-Point $size $label "$label top-left" $child $ax $ay $ax.First $ay.First 0 0 $tol
  $elx = if ($ax.Fits) { $w - 1 } else { Get-Expected $ax $ax.Last }
  $ely = if ($ay.Fits) { $h - 1 } else { Get-Expected $ay $ay.Last }
  Test-Point $size $label "$label bottom-right" $child $ax $ay $ax.Last $ay.Last $elx $ely $tol
}

# Waits until the window caption state is the expected one (the viewer changes the
# style asynchronously and may be slow right after start-up).
function Wait-Caption($wnd, [bool]$expected, [int]$seconds = 10) {
  $deadline = (Get-Date).AddSeconds($seconds)
  do {
    $has = ([FsvNative]::Style($wnd, -16) -band 0xC00000) -eq 0xC00000
    if ($has -eq $expected) { break }
    Start-Sleep -Milliseconds 200
  } while ((Get-Date) -lt $deadline)
  Start-Sleep -Milliseconds 700
}

# Drives the Display menu commands of the viewer (see .DESCRIPTION).
function Test-DisplayModes([string]$size, $mainWnd, $child, [int]$w, [int]$h) {
  $tag = 'display'
  # Initial placement: the frame may extend by the invisible resize border (up to 16 px).
  $wa = [FsvNative]::WorkArea($mainWnd); $wr = [FsvNative]::WindowRect($mainWnd)
  $inside = ($wr[0] -ge $wa[0] - 16) -and ($wr[1] -ge $wa[1] - 16) -and ($wr[2] -le $wa[2] + 16) -and ($wr[3] -le $wa[3] + 16)
  Add-Result $size $tag 'initial window inside work area' "work=$($wa -join ',') (+/-16)" "window=$($wr -join ',')" $(if ($inside) { 'PASS' } else { 'FAIL' })

  [void][FsvNative]::Post($mainWnd, 0x112, 0xA830, 0)       # WM_SYSCOMMAND: Fit to window
  Test-Corners $size 'Fit to window' $child $w $h 'auto'
  [void][FsvNative]::Post($mainWnd, 0x112, 0xA820, 0)       # Original (100%)
  Test-Corners $size 'Original' $child $w $h '100'

  $vs = [FsvNative]::VirtualScreen($mainWnd)
  foreach ($fs in @(@('Fullscreen across all monitors', 0xA800), @('Fullscreen on current monitor', 0xA810))) {
    [void][FsvNative]::Post($mainWnd, 0x112, $fs[1], 0)
    Wait-Caption $mainWnd $false
    $others = @([FsvNative]::TopWindows([uint32]$viewerProc.Id) | ForEach-Object { [FsvNative]::ClassName($_) + '=' + (([FsvNative]::WindowRect($_)) -join ',') })
    Add-Result $size $tag "$($fs[0]) visible windows" 'info' ($others -join ' ; ') 'INFO'
    $wr = [FsvNative]::WindowRect($mainWnd)
    $style = [FsvNative]::Style($mainWnd, -16)
    $caption = ($style -band 0xC00000) -ne 0
    if ($fs[1] -eq 0xA800) {
      $ok = ($wr -join ',') -eq ($vs -join ',')
      Add-Result $size $tag $fs[0] "window = virtual screen $($vs -join ',')" "window=$($wr -join ',') caption=$caption" $(if ($ok -and -not $caption) { 'PASS' } else { 'FAIL' })
    } else {
      $mon = ($wr[2] - $wr[0] -le $vs[2] - $vs[0]) -and ($wr[0] -ge $vs[0]) -and ($wr[2] -le $vs[2])
      Add-Result $size $tag $fs[0] "window inside virtual screen, no caption" "window=$($wr -join ',') caption=$caption" $(if ($mon -and -not $caption) { 'PASS' } else { 'FAIL' })
    }
    Test-Corners $size $fs[0] $child $w $h '100'
    [void][FsvNative]::Post($mainWnd, 0x111, 215, 0)         # WM_COMMAND IDS_TB_FULLSCREEN: leave fullscreen
    Wait-Caption $mainWnd $true
    $style = [FsvNative]::Style($mainWnd, -16)
    $caption = ($style -band 0xC00000) -eq 0xC00000
    $wr = [FsvNative]::WindowRect($mainWnd)
    Add-Result $size $tag "leave $($fs[0])" 'captioned window restored' "style caption=$caption window=$($wr -join ',')" $(if ($caption) { 'PASS' } else { 'FAIL' })
  }
}

# --- main ------------------------------------------------------------------
$serverProc = $null
$viewerProc = $null
$stopFile = Join-Path $WorkDir 'stop'
try {
  foreach ($size in $Sizes) {
    $w, $h = [int[]]($size -split 'x')
    $script:logFile = Join-Path $WorkDir "rfb-$size.jsonl"
    Remove-Item -LiteralPath $stopFile -ErrorAction SilentlyContinue
    $serverProc = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -WindowStyle Hidden -PassThru -ArgumentList @(
      '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $serverScript, '-Port', $Port, '-Width', $w, '-Height', $h,
      '-LogFile', $script:logFile, '-StopFile', $stopFile, '-DurationSeconds', 600)
    $deadline = (Get-Date).AddSeconds(40)
    while (-not (Test-Path -LiteralPath $script:logFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    if (-not (Wait-Log { $_.event -eq 'listening' } 40)) { throw "Fake server did not start for $size." }

    foreach ($scale in $Scales) {
      $before = @(Get-Log).Count
      $viewerProc = Start-Process -FilePath $Viewer -PassThru -ArgumentList @(
        '-host=127.0.0.1', "-port=$Port", "-scale=$scale", '-encoding=raw', '-viewonly=no')
      try {
        $connected = Wait-Log { $_.msg -eq 'FramebufferUpdateRequest' } $ConnectTimeoutSeconds
        if (-not $connected -or $viewerProc.HasExited) {
          Add-Result $size $scale 'connect' 'viewer connects' 'no session' 'FAIL'
          continue
        }
        Start-Sleep -Seconds 2   # let the window finish sizing/layout

        $top = [FsvNative]::TopWindows([uint32]$viewerProc.Id)
        $child = [IntPtr]::Zero; $area = 0; $classes = @()
        foreach ($t in $top) {
          $classes += [FsvNative]::ClassName($t)
          foreach ($c in [FsvNative]::Descendants($t)) {
            # The remote desktop is a TvnWindowClass descendant; the session view
            # around it (FsvSessionView, toolbar included) is larger.
            if (-not [FsvNative]::Visible($c)) { continue }
            $classes += "  child:" + [FsvNative]::ClassName($c)
            if ([FsvNative]::ClassName($c) -ne 'TvnWindowClass') { continue }
            $sz = [FsvNative]::ClientSize($c)
            if ($sz[0] * $sz[1] -gt $area) { $area = $sz[0] * $sz[1]; $child = $c }
          }
        }
        if ($child -eq [IntPtr]::Zero) {
          Add-Result $size $scale 'find desktop window' 'child window' "none (top: $($classes -join ';'))" 'FAIL'
          continue
        }
        $mainWnd = $top[0]
        $cs = [FsvNative]::ClientSize($child)
        Add-Result $size $scale 'DPI' 'info' ([FsvNative]::Dpi($mainWnd, [uint32]$viewerProc.Id)) 'INFO'
        Add-Result $size $scale 'desktop window' 'info' "class=$([FsvNative]::ClassName($child)) client=$($cs[0])x$($cs[1])" 'INFO'

        $log = @(Get-Log | Select-Object -Skip $before)
        $enc = @($log | Where-Object { $_.msg -eq 'SetEncodings' } | Select-Object -Last 1)
        $encList = if ($enc.Count) { @($enc[0].encodings) } else { @() }
        $negative = @($encList | Where-Object { $_ -lt 0 })
        $extMouse = $encList -contains -316
        Add-Result $size $scale 'SetEncodings' 'info' "negative pseudo-encodings: $($negative -join ',') ; extended mouse buttons (-316): $extMouse" 'INFO'

        # Scale ratio num/den
        if ($scale -eq 'auto') {
          if ($cs[0] * $h -le $cs[1] * $w) { $num = $cs[0]; $den = $w } else { $num = $cs[1]; $den = $h }
        } else { $num = [long]$scale; $den = 100 }
        $ax = Get-Axis $cs[0] $w $num $den
        $ay = Get-Axis $cs[1] $h $num $den
        $tol = if ($num -eq $den) { 0 } else { [int][math]::Max(1, [math]::Ceiling($den / [double]$num)) }
        Add-Result $size $scale 'model' 'info' "disp=$($ax.Disp)x$($ay.Disp) cent=($($ax.Cent),$($ay.Cent)) fits=$($ax.Fits)/$($ay.Fits) ratio=$num/$den tol=$tol" 'INFO'

        # Mid point first so that the following move to the corner is not deduplicated.
        $mx = $ax.First + [int][math]::Floor(($ax.Last - $ax.First) / 2)
        $my = $ay.First + [int][math]::Floor(($ay.Last - $ay.First) / 2)
        Test-Point $size $scale 'pointer mid' $child $ax $ay $mx $my (Get-Expected $ax $mx) (Get-Expected $ay $my) ([math]::Max($tol, 1))
        Test-Point $size $scale 'pointer top-left' $child $ax $ay $ax.First $ay.First 0 0 $tol
        $elx = if ($ax.Fits) { $w - 1 } else { Get-Expected $ax $ax.Last }
        $ely = if ($ay.Fits) { $h - 1 } else { Get-Expected $ay $ay.Last }
        $lbl = if ($ax.Fits -and $ay.Fits) { 'pointer bottom-right' } else { 'pointer bottom-right (viewport)' }
        Test-Point $size $scale $lbl $child $ax $ay $ax.Last $ay.Last $elx $ely $tol

        # Buttons (message, wParam flags)
        $px = $mx; $py = $my
        $sets = @(
          @('left', 0x201, 0x202, 0x1, 1), @('middle', 0x207, 0x208, 0x10, 2), @('right', 0x204, 0x205, 0x2, 4))
        foreach ($b in $sets) {
          $px = [math]::Min($ax.Last, $px + 5)
          $ev = Invoke-Probe {
            [void][FsvNative]::Post($child, $b[1], $b[3], [FsvNative]::Pos($px, $py))
            [void][FsvNative]::Post($child, $b[2], 0, [FsvNative]::Pos($px, $py))
          }
          Test-Sequence $size $scale "button $($b[0]) down/up" @($b[4], 0) $ev
        }

        # Wheel messages carry screen coordinates.
        function Send-Wheel([uint32]$msg, [int]$delta, [int]$cx, [int]$cy) {
          $sp = [FsvNative]::ToScreen($child, $cx, $cy)
          $wp = ([long]($delta -band 0xFFFF) -shl 16)
          [void][FsvNative]::Post($child, $msg, $wp, [FsvNative]::Pos($sp[0], $sp[1]))
        }
        $wheels = @(
          @('wheel up (button 4)', 0x20A, 120, @(8, 0)), @('wheel down (button 5)', 0x20A, -120, @(16, 0)),
          @('hwheel left (button 6)', 0x20E, -120, @(32, 0)), @('hwheel right (button 7)', 0x20E, 120, @(64, 0)))
        $i = 0
        foreach ($wh in $wheels) {
          $i++
          $wx = $mx - 20 + 4 * $i; $wy = $my + 7 * $i
          $ev = Invoke-Probe { Send-Wheel $wh[1] $wh[2] $wx $wy }
          Test-Sequence $size $scale $wh[0] $wh[3] $ev
        }

        # X buttons
        $xev = @()
        $xev += Invoke-Probe {
          [void][FsvNative]::Post($child, 0x20B, ((1 -shl 16) -bor 0x20), [FsvNative]::Pos($mx + 9, $my + 9))
          [void][FsvNative]::Post($child, 0x20C, (1 -shl 16), [FsvNative]::Pos($mx + 9, $my + 9))
        }
        $xev += Invoke-Probe {
          [void][FsvNative]::Post($child, 0x20B, ((2 -shl 16) -bor 0x40), [FsvNative]::Pos($mx + 11, $my + 11))
          [void][FsvNative]::Post($child, 0x20C, (2 -shl 16), [FsvNative]::Pos($mx + 11, $my + 11))
        }
        $masks = @($xev | Where-Object { $_ } | ForEach-Object { [int]$_.mask })
        if ($extMouse) {
          Add-Result $size $scale 'X1/X2 buttons' 'extended encoding advertised' "masks: $($masks -join ',')" 'PASS'
        } else {
          $bad = @($masks | Where-Object { $_ -ne 0 })
          Add-Result $size $scale 'X1/X2 send no undefined mask bits' 'all masks 0 (no ext encoding)' $(if ($masks.Count) { "masks: $($masks -join ',')" } else { 'no events' }) $(if ($bad.Count -eq 0) { 'PASS' } else { 'FAIL' })
        }
        if ($DisplayModes -and $scale -eq $Scales[0]) {
          Test-DisplayModes $size $mainWnd $child $w $h
        }
      } finally {
        if ($viewerProc) { Stop-Process -Id $viewerProc.Id -Force -ErrorAction SilentlyContinue; $viewerProc.WaitForExit(3000) | Out-Null }
        Wait-Log { $_.event -eq 'disconnected' } 5 | Out-Null
        Start-Sleep -Milliseconds 300
      }
    }
    # stop this size's server
    New-Item -ItemType File -Path $stopFile -Force | Out-Null
    if ($serverProc) { $serverProc.WaitForExit(5000) | Out-Null }
    if ($serverProc -and -not $serverProc.HasExited) { Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $stopFile -ErrorAction SilentlyContinue
  }
} finally {
  if ($viewerProc -and -not $viewerProc.HasExited) { Stop-Process -Id $viewerProc.Id -Force -ErrorAction SilentlyContinue }
  if ($serverProc -and -not $serverProc.HasExited) { Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue }
  Remove-Item -LiteralPath $stopFile -ErrorAction SilentlyContinue
  Restore-Registry
}

Write-Host "Viewer under test: $Viewer"
$results | Format-Table Size, Scale, Check, Expected, Actual, Result -AutoSize -Wrap | Out-String -Width 250 | Write-Host
$fail = @($results | Where-Object Result -eq 'FAIL')
foreach ($f in $fail) { Write-Host ("FAIL: {0} {1} | {2} | expected {3} | actual {4}" -f $f.Size, $f.Scale, $f.Check, $f.Expected, $f.Actual) }
Write-Host ("PASS: {0}  FAIL: {1}" -f @($results | Where-Object Result -eq 'PASS').Count, $fail.Count)
if ($fail.Count -gt 0) { exit 1 }
