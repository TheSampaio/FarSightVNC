<#
.SYNOPSIS
  Compares tvnviewer builds on rendering cost and latency against a fake RFB server.

.DESCRIPTION
  For every display mode and repetition, and for each viewer in turn
  (A,B,A,B... so that slow machine drift affects all builds equally), this
  script:
    1. starts tools\fake-rfb-server.py on 127.0.0.1 (animated Raw framebuffer),
    2. launches the viewer against it,
    3. runs tools\render-probe.py, which posts WM_MOUSEMOVE messages to the
       viewer window (input latency) and watches a marker pixel (display
       latency) while this script samples the viewer process,
    4. records CPU (percent of one core), peak working set, peak private bytes,
       handles, GDI and USER objects, effective frame rate and latencies.
  A markdown table and the raw per-run JSON are written to build-logs\.

  Only processes started here are stopped, and the viewer's connection-history
  registry entry is removed unless it already existed. Nothing needs
  administrator rights; no HKLM, firewall or service is touched. No real mouse
  or keyboard input is generated. While it runs the viewer window sits topmost
  at the top-left of the screen: do not use the machine, and do not run other
  heavy jobs, if you want comparable numbers.

  Modes: 'fit' = -scale=auto (viewer scales to the window); '100' = windowed at
  100 percent (the 1280x800 window shows the top-left part of the screen).

.EXAMPLE
  .\tools\measure-render.ps1 -Viewers ([ordered]@{ official = 'C:\...\tvnviewer.exe'; built = '.\build\bin\x64\Release\tvnviewer.exe' })
  .\tools\measure-render.ps1 -Viewers $v -Size 1920x1080 -Rects 8 -Repetitions 5 -DurationSeconds 30
#>
param(
  [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Viewers,
  [string]$Size = '3840x2160',
  [ValidateSet('fit', '100')][string[]]$Modes = @('fit', '100'),
  [int]$Rects = 0,
  [double]$MaxFps = 0,
  [int]$Port = 5931,
  # Extra command line arguments per viewer name, e.g. @{ gpu = @('-renderer=gpu') }.
  [System.Collections.IDictionary]$ViewerArgs = @{},
  [int]$Repetitions = 3,
  [int]$DurationSeconds = 15,
  [int]$WarmupSeconds = 3,
  [switch]$SkipInput,
  [switch]$SkipDisplay,
  [string]$Python = 'python',
  [string]$OutDir = (Join-Path $PSScriptRoot '..\build-logs')
)

$ErrorActionPreference = 'Stop'
if ($Port -in 5800, 5900, 5910) { throw "Port $Port is reserved for other VNC servers; use another port." }
foreach ($name in @($Viewers.Keys)) {
  if (-not (Test-Path $Viewers[$name])) { throw "Viewer not found: $($Viewers[$name])" }
  $Viewers[$name] = (Resolve-Path $Viewers[$name]).Path
}
$serverScript = Join-Path $PSScriptRoot 'fake-rfb-server.py'
$probeScript = Join-Path $PSScriptRoot 'render-probe.py'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $OutDir "render-$stamp"
New-Item -ItemType Directory -Force $runDir | Out-Null

Add-Type -Namespace Native -Name User32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
'@
$GR_GDIOBJECTS = 0
$GR_USEROBJECTS = 1

function Get-Median([double[]]$values) {
  $sorted = @($values | Where-Object { $null -ne $_ } | Sort-Object)
  $count = $sorted.Count
  if ($count -eq 0) { return $null }
  if ($count % 2) { return $sorted[[int](($count - 1) / 2)] }
  return ($sorted[$count / 2 - 1] + $sorted[$count / 2]) / 2
}

function Wait-LogLine([string]$path, [string]$pattern, [int]$timeoutSeconds) {
  $deadline = (Get-Date).AddSeconds($timeoutSeconds)
  while ((Get-Date) -lt $deadline) {
    if ((Test-Path $path) -and (Select-String -Path $path -Pattern $pattern -Quiet)) { return }
    Start-Sleep -Milliseconds 200
  }
  throw "Timed out waiting for '$pattern' in $path"
}

function Stop-Started($process) {
  if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
}

function Invoke-Run([string]$viewerName, [string]$viewerPath, [string]$mode, [int]$run) {
  $tag = "$mode-$viewerName-$run"
  $serverJson = Join-Path $runDir "$tag-server.json"
  $serverLog = Join-Path $runDir "$tag-server.log"
  $probeJson = Join-Path $runDir "$tag-probe.json"
  $probeLog = Join-Path $runDir "$tag-probe.log"
  $server = $null; $viewer = $null; $probe = $null
  try {
    # The server outlives the probe's window by a margin; the probe computes the figures.
    $serverArgs = @("`"$serverScript`"", '--port', $Port, '--size', $Size, '--rects', $Rects, '--max-fps', $MaxFps,
      '--warmup', $WarmupSeconds, '--duration', ($WarmupSeconds + $DurationSeconds + 8), '--out', "`"$serverJson`"")
    $server = Start-Process -FilePath $Python -ArgumentList $serverArgs -PassThru -WindowStyle Hidden `
      -RedirectStandardOutput $serverLog -RedirectStandardError "$serverLog.err"
    Wait-LogLine $serverLog 'LISTENING' 10

    $scaleArg = if ($mode -eq 'fit') { '-scale=auto' } else { '-scale=100' }
    $extra = if ($ViewerArgs.Contains($viewerName)) { @($ViewerArgs[$viewerName]) } else { @() }
    $viewer = Start-Process -FilePath $viewerPath -PassThru -ArgumentList (@(
      '-host=127.0.0.1', "-port=$Port", $scaleArg, '-encoding=raw', '-showcontrols=no') + $extra)
    Wait-LogLine $serverLog 'ACTIVE' 20

    $probeArgs = @("`"$probeScript`"", '--pid', $viewer.Id, '--server-json', "`"$serverJson`"", '--size', $Size,
      '--scale', $(if ($mode -eq 'fit') { 'fit' } else { 'none' }), '--warmup', $WarmupSeconds,
      '--seconds', $DurationSeconds, '--out', "`"$probeJson`"")
    if ($SkipInput) { $probeArgs += '--no-input' }
    if ($SkipDisplay) { $probeArgs += '--no-display' }
    $probe = Start-Process -FilePath $Python -ArgumentList $probeArgs -PassThru -WindowStyle Hidden `
      -RedirectStandardOutput $probeLog -RedirectStandardError "$probeLog.err"

    Start-Sleep -Seconds $WarmupSeconds
    $viewer.Refresh()
    if ($viewer.HasExited) { throw "$viewerName exited during warm-up" }
    $cpuStart = $viewer.TotalProcessorTime.TotalSeconds
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $peakWorkingSet = 0; $peakPrivate = 0; $peakHandles = 0; $peakGdi = 0; $peakUser = 0
    for ($second = 0; $second -lt $DurationSeconds; $second++) {
      Start-Sleep -Milliseconds ([math]::Max(0, ($second + 1) * 1000 - $clock.ElapsedMilliseconds))
      $viewer.Refresh()
      if ($viewer.HasExited) { throw "$viewerName exited during run $run" }
      $peakWorkingSet = [math]::Max($peakWorkingSet, $viewer.WorkingSet64)
      $peakPrivate = [math]::Max($peakPrivate, $viewer.PrivateMemorySize64)
      $peakHandles = [math]::Max($peakHandles, $viewer.HandleCount)
      $peakGdi = [math]::Max($peakGdi, [Native.User32]::GetGuiResources($viewer.Handle, $GR_GDIOBJECTS))
      $peakUser = [math]::Max($peakUser, [Native.User32]::GetGuiResources($viewer.Handle, $GR_USEROBJECTS))
    }
    $cpuSeconds = $viewer.TotalProcessorTime.TotalSeconds - $cpuStart
    $elapsed = $clock.Elapsed.TotalSeconds

    if (-not $probe.WaitForExit(($DurationSeconds + 60) * 1000)) { throw 'probe did not finish' }
    if (-not (Test-Path $probeJson)) {
      throw "probe failed: $(Get-Content "$probeLog.err" -Raw)"
    }
    $result = Get-Content $probeJson -Raw | ConvertFrom-Json
    [pscustomobject]@{
      Mode = $mode; Viewer = $viewerName; Run = $run
      CpuPercentOneCore = [math]::Round(100 * $cpuSeconds / $elapsed, 1)
      PeakWorkingSetMB = [math]::Round($peakWorkingSet / 1MB, 1)
      PeakPrivateMB = [math]::Round($peakPrivate / 1MB, 1)
      PeakHandles = $peakHandles; PeakGdiObjects = $peakGdi; PeakUserObjects = $peakUser
      Fps = $result.server.effective_fps
      UpdateIntervalP95Ms = $result.server.update_interval_ms_p95
      Input = $result.input_latency
      Display = $result.display_latency
    }
  } finally {
    Stop-Started $probe
    Stop-Started $viewer
    Stop-Started $server
    Start-Sleep -Milliseconds 500
  }
}

# The viewer records per-host options in its history; drop the entry afterwards
# unless the user already had one for this host and port.
$historyKey = "HKCU:\Software\TightVNC\Viewer\History\127.0.0.1::$Port"
$historyExisted = Test-Path -LiteralPath $historyKey

$rows = @()
try {
  foreach ($mode in $Modes) {
    for ($run = 1; $run -le $Repetitions; $run++) {
      foreach ($name in $Viewers.Keys) {
        Write-Host "==> mode=$mode run=$run viewer=$name"
        $rows += Invoke-Run $name $Viewers[$name] $mode $run
      }
    }
  }
} finally {
  if (-not $historyExisted -and (Test-Path -LiteralPath $historyKey)) {
    Remove-Item -LiteralPath $historyKey -Recurse
  }
}

function Format-Latency($values, [string]$field) {
  $numbers = @($values | Where-Object { $_ -and $_.samples -gt 0 } | ForEach-Object { $_.$field })
  if ($numbers.Count -eq 0) { return 'n/a' }
  if ($field -eq 'max_ms') { return [string]($numbers | Measure-Object -Maximum).Maximum }
  return [string][math]::Round((Get-Median $numbers), 1)
}

$summary = foreach ($group in ($rows | Group-Object { "$($_.Mode)|$($_.Viewer)" })) {
  $g = $group.Group
  [pscustomobject][ordered]@{
    Mode = $g[0].Mode; Viewer = $g[0].Viewer; Runs = $g.Count
    CpuPercentOneCore = Get-Median $g.CpuPercentOneCore
    PeakWorkingSetMB = Get-Median $g.PeakWorkingSetMB
    PeakPrivateMB = Get-Median $g.PeakPrivateMB
    PeakHandles = Get-Median $g.PeakHandles
    PeakGdiObjects = Get-Median $g.PeakGdiObjects
    Fps = Get-Median $g.Fps
    InputMedianMs = Format-Latency $g.Input 'median_ms'
    InputP95Ms = Format-Latency $g.Input 'p95_ms'
    InputMaxMs = Format-Latency $g.Input 'max_ms'
    DisplayMedianMs = Format-Latency $g.Display 'median_ms'
    DisplayP95Ms = Format-Latency $g.Display 'p95_ms'
  }
}

$lines = @(
  "# Render benchmark $stamp", '',
  "Framebuffer $Size, Raw updates, rects=$Rects, max-fps=$MaxFps; $Repetitions x ${DurationSeconds}s per viewer and mode (warm-up ${WarmupSeconds}s), interleaved.",
  'Values are medians of the repetitions (input/display latency: median of the per-run medians and p95s; max is the largest seen).', '',
  '| Mode | Viewer | Runs | CPU % (1 core) | Peak WS MB | Peak private MB | Handles | GDI | FPS | Input med/p95/max ms | Display med/p95 ms |',
  '|---|---|---|---|---|---|---|---|---|---|---|')
foreach ($s in $summary) {
  $lines += "| $($s.Mode) | $($s.Viewer) | $($s.Runs) | $($s.CpuPercentOneCore) | $($s.PeakWorkingSetMB) | $($s.PeakPrivateMB) | $($s.PeakHandles) | $($s.PeakGdiObjects) | $($s.Fps) | $($s.InputMedianMs) / $($s.InputP95Ms) / $($s.InputMaxMs) | $($s.DisplayMedianMs) / $($s.DisplayP95Ms) |"
}
$markdown = Join-Path $OutDir "render-$stamp.md"
$lines | Set-Content -Path $markdown -Encoding UTF8
@{ options = @{ size = $Size; rects = $Rects; maxFps = $MaxFps; repetitions = $Repetitions; durationSeconds = $DurationSeconds
    warmupSeconds = $WarmupSeconds; modes = $Modes; viewers = $Viewers }
  summary = $summary; runs = $rows } | ConvertTo-Json -Depth 8 | Set-Content -Path (Join-Path $OutDir "render-$stamp.json") -Encoding UTF8

$lines | ForEach-Object { Write-Host $_ }
Write-Host "Results: $markdown (raw per-run files in $runDir)"
