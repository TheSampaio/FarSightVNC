# Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
<#
.SYNOPSIS
  Long-running session test: samples viewer memory, handles and GDI/USER objects.

.DESCRIPTION
  Starts a loopback-only test server (tools/test-server.ps1), opens one
  view-only viewer per renderer (default: gdi and gpu) and samples each viewer
  every -IntervalSeconds for -Hours. Writes one CSV row per sample and prints
  a per-renderer trend: values after the warm-up, at the end, and the
  least-squares slope per hour. The viewer windows show the server desktop,
  which contains the viewers themselves, so the server keeps sending updates.
  On exit (normal or Ctrl+C) it stops the viewers and the server, removes the
  server test configuration and the viewer history entry it created.

.EXAMPLE
  .\tools\long-session.ps1 -Viewer .\build\bin\x64\Release\fsvnc-viewer.exe -Server C:\...\tvnserver.exe -Hours 2
#>
param(
  [Parameter(Mandatory = $true)][string]$Viewer,
  [Parameter(Mandatory = $true)][string]$Server,
  [int]$Port = 5929,
  [double]$Hours = 2,
  [int]$IntervalSeconds = 60,
  [int]$WarmupMinutes = 10,
  [string[]]$Renderers = @('gdi', 'gpu'),
  [string]$OutCsv = (Join-Path $PSScriptRoot '..\build-logs\long-session.csv')
)

$ErrorActionPreference = 'Stop'
$Viewer = (Resolve-Path -LiteralPath $Viewer).Path
Add-Type -Namespace Native -Name LongSessionUser32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
'@

$password = -join ((48..57) + (97..122) | Get-Random -Count 8 | ForEach-Object { [char]$_ })
$historyKey = "HKCU:\Software\TightVNC\Viewer\History\127.0.0.1::$Port"
$historyExisted = Test-Path -LiteralPath $historyKey
$serverScript = Join-Path $PSScriptRoot 'test-server.ps1'
New-Item -ItemType Directory -Force (Split-Path $OutCsv) | Out-Null
$viewers = @{}

try {
  & $serverScript -Server $Server -Port $Port -Password $password
  foreach ($renderer in $Renderers) {
    $viewers[$renderer] = Start-Process -FilePath $Viewer -PassThru -ArgumentList @(
      '-host=127.0.0.1', "-port=$Port", "-password=$password", '-viewonly=yes', "-renderer=$renderer")
  }
  'Renderer,Minute,Alive,PrivateMB,WorkingSetMB,Handles,Gdi,User,CpuSeconds' | Set-Content -Encoding UTF8 $OutCsv
  $start = Get-Date
  $end = $start.AddHours($Hours)
  while ((Get-Date) -lt $end) {
    $minute = [Convert]::ToString([math]::Round(((Get-Date) - $start).TotalMinutes, 2), [Globalization.CultureInfo]::InvariantCulture)
    foreach ($renderer in $Renderers) {
      $process = $viewers[$renderer]
      $process.Refresh()
      if ($process.HasExited) {
        "$renderer,$minute,False,,,,,," | Add-Content $OutCsv
        continue
      }
      $row = @($renderer, $minute, 'True',
        [math]::Round($process.PrivateMemorySize64 / 1MB, 2),
        [math]::Round($process.WorkingSet64 / 1MB, 2),
        $process.HandleCount,
        [Native.LongSessionUser32]::GetGuiResources($process.Handle, 0),
        [Native.LongSessionUser32]::GetGuiResources($process.Handle, 1),
        [math]::Round($process.TotalProcessorTime.TotalSeconds, 1))
      (($row | ForEach-Object { [Convert]::ToString($_, [Globalization.CultureInfo]::InvariantCulture) }) -join ',') |
        Add-Content $OutCsv
    }
    Start-Sleep -Seconds $IntervalSeconds
  }
} finally {
  foreach ($process in $viewers.Values) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
  & $serverScript -Stop -RemoveConfig | Out-Null
  if (-not $historyExisted -and (Test-Path -LiteralPath $historyKey)) { Remove-Item -LiteralPath $historyKey -Recurse }
}

function Get-Slope($points, [string]$field) {
  # Least-squares slope of $field against time, per hour.
  $n = $points.Count
  if ($n -lt 2) { return 0 }
  $inv = [Globalization.CultureInfo]::InvariantCulture
  $xs = @($points | ForEach-Object { [double]::Parse($_.Minute, $inv) / 60 })
  $ys = @($points | ForEach-Object { [double]::Parse($_.$field, $inv) })
  $mx = ($xs | Measure-Object -Average).Average
  $my = ($ys | Measure-Object -Average).Average
  $num = 0.0; $den = 0.0
  for ($i = 0; $i -lt $n; $i++) { $num += ($xs[$i] - $mx) * ($ys[$i] - $my); $den += ($xs[$i] - $mx) * ($xs[$i] - $mx) }
  if ($den -eq 0) { return 0 }
  [math]::Round($num / $den, 3)
}

$samples = Import-Csv $OutCsv
$summary = foreach ($renderer in $Renderers) {
  $all = @($samples | Where-Object Renderer -eq $renderer)
  $alive = @($all | Where-Object Alive -eq 'True')
  $steady = @($alive | Where-Object { [double]::Parse($_.Minute, [Globalization.CultureInfo]::InvariantCulture) -ge $WarmupMinutes })
  $first = $steady | Select-Object -First 1
  $last = $alive | Select-Object -Last 1
  [pscustomobject]@{
    Renderer          = $renderer
    Samples           = $all.Count
    Died              = $all.Count -ne $alive.Count
    PrivateMB         = "$($first.PrivateMB) -> $($last.PrivateMB)"
    PrivateMBPerHour  = Get-Slope $steady 'PrivateMB'
    Handles           = "$($first.Handles) -> $($last.Handles)"
    HandlesPerHour    = Get-Slope $steady 'Handles'
    Gdi               = "$($first.Gdi) -> $($last.Gdi)"
    GdiPerHour        = Get-Slope $steady 'Gdi'
    User              = "$($first.User) -> $($last.User)"
    UserPerHour       = Get-Slope $steady 'User'
  }
}
$summary | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
Write-Host "Samples: $OutCsv"
