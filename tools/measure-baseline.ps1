<#
.SYNOPSIS
  Collects a resource-usage baseline for one or more tvnviewer builds.

.DESCRIPTION
  For each viewer and repetition, opens a view-only session against a running
  VNC server, samples the process every second for the given duration and
  reports CPU time, peak working set, private memory, handle count and GDI/USER
  object counts. Results are written to a CSV file and summarized (median of
  the repetitions) on the console.

  Numbers are only comparable between viewers measured on the same machine,
  server, desktop content and network conditions.

.EXAMPLE
  .\tools\measure-baseline.ps1 -Viewers @{ built = '.\x64\Release\tvnviewer.exe'; official = 'C:\...\tvnviewer.exe' } -Password <test password>
#>
param(
  [Parameter(Mandatory = $true)][hashtable]$Viewers,
  [string]$HostName = '127.0.0.1',
  [int]$Port = 5910,
  [Parameter(Mandatory = $true)][string]$Password,
  [string]$Encoding = 'tight',
  [string]$Scale = 'auto',
  [int]$DurationSeconds = 30,
  [int]$Repetitions = 3,
  [string]$OutCsv = (Join-Path $PSScriptRoot '..\build-logs\baseline.csv')
)

$ErrorActionPreference = 'Stop'

Add-Type -Namespace Native -Name User32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
'@
$GR_GDIOBJECTS = 0
$GR_USEROBJECTS = 1

function Get-Median([double[]]$values) {
  $sorted = $values | Sort-Object
  $count = $sorted.Count
  if ($count -eq 0) { return $null }
  if ($count % 2) { return $sorted[[int](($count - 1) / 2)] }
  return ($sorted[$count / 2 - 1] + $sorted[$count / 2]) / 2
}

$rows = @()
foreach ($name in $Viewers.Keys) {
  $path = $Viewers[$name]
  if (-not (Test-Path $path)) { throw "Viewer not found: $path" }
  for ($run = 1; $run -le $Repetitions; $run++) {
    $arguments = @("-host=$HostName", "-port=$Port", "-password=$Password", '-viewonly=yes',
      "-encoding=$Encoding", "-scale=$Scale")
    $process = Start-Process -FilePath $path -ArgumentList $arguments -PassThru
    $peakWorkingSet = 0; $peakPrivate = 0; $peakHandles = 0; $peakGdi = 0; $peakUser = 0
    try {
      for ($second = 0; $second -lt $DurationSeconds; $second++) {
        Start-Sleep -Seconds 1
        $process.Refresh()
        if ($process.HasExited) { throw "$name exited during run $run" }
        $peakWorkingSet = [math]::Max($peakWorkingSet, $process.WorkingSet64)
        $peakPrivate = [math]::Max($peakPrivate, $process.PrivateMemorySize64)
        $peakHandles = [math]::Max($peakHandles, $process.HandleCount)
        $peakGdi = [math]::Max($peakGdi, [Native.User32]::GetGuiResources($process.Handle, $GR_GDIOBJECTS))
        $peakUser = [math]::Max($peakUser, [Native.User32]::GetGuiResources($process.Handle, $GR_USEROBJECTS))
      }
      $cpu = $process.TotalProcessorTime.TotalSeconds
    } finally {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      Start-Sleep -Seconds 1
    }
    $rows += [pscustomobject]@{
      Viewer          = $name
      Run             = $run
      DurationSeconds = $DurationSeconds
      CpuSeconds      = [math]::Round($cpu, 2)
      CpuPercentOneCore = [math]::Round(100 * $cpu / $DurationSeconds, 1)
      PeakWorkingSetMB = [math]::Round($peakWorkingSet / 1MB, 1)
      PeakPrivateMB   = [math]::Round($peakPrivate / 1MB, 1)
      PeakHandles     = $peakHandles
      PeakGdiObjects  = $peakGdi
      PeakUserObjects = $peakUser
    }
  }
}

New-Item -ItemType Directory -Force (Split-Path $OutCsv) | Out-Null
$rows | Export-Csv -Path $OutCsv -NoTypeInformation -Encoding UTF8

$summary = foreach ($group in ($rows | Group-Object Viewer)) {
  [pscustomobject]@{
    Viewer            = $group.Name
    Runs              = $group.Count
    MedianCpuPercent  = Get-Median ($group.Group.CpuPercentOneCore)
    MedianPeakWSMB    = Get-Median ($group.Group.PeakWorkingSetMB)
    MedianPeakPrivMB  = Get-Median ($group.Group.PeakPrivateMB)
    MedianPeakHandles = Get-Median ($group.Group.PeakHandles)
    MedianPeakGdi     = Get-Median ($group.Group.PeakGdiObjects)
    MedianPeakUser    = Get-Median ($group.Group.PeakUserObjects)
  }
}
$summary | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
Write-Host "Raw samples: $OutCsv"
