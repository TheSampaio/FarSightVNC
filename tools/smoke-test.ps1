<#
.SYNOPSIS
  Connection smoke test for tvnviewer against a running VNC server.

.DESCRIPTION
  Starts the viewer in view-only mode (no keyboard or mouse events reach the
  server) once per encoding, checks that the process stays alive, that a TCP
  session to the server is established and that the session window opened.
  Then checks that a wrong password does not establish a session.

  The server must already be running. For a safe local setup, run a server
  bound to the loopback interface only (TightVNC: LoopbackOnly=1,
  AllowLoopback=1) on a port other than the production one.

.EXAMPLE
  .\tools\smoke-test.ps1 -Viewer .\x64\Release\tvnviewer.exe -Port 5910 -Password <test password>
#>
param(
  [Parameter(Mandatory = $true)][string]$Viewer,
  [string]$HostName = '127.0.0.1',
  [int]$Port = 5910,
  [Parameter(Mandatory = $true)][string]$Password,
  [string[]]$Encodings = @('tight', 'zrle', 'hextile', 'rre'),
  [int]$WaitSeconds = 5,
  [switch]$SkipWrongPassword
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Viewer)) { throw "Viewer not found: $Viewer" }

function Invoke-ViewerCase([string]$name, [string[]]$arguments, [bool]$expectSession) {
  $process = Start-Process -FilePath $Viewer -ArgumentList $arguments -PassThru
  try {
    Start-Sleep -Seconds $WaitSeconds
    $process.Refresh()
    $sessions = @(Get-NetTCPConnection -State Established -OwningProcess $process.Id `
        -ErrorAction SilentlyContinue | Where-Object RemotePort -eq $Port).Count
    $alive = -not $process.HasExited
    # The session window title is "<desktop name> - ..." only after the
    # protocol reaches the connected state.
    $title = $process.MainWindowTitle
    $hasSession = $alive -and $sessions -gt 0
    [pscustomobject]@{
      Case         = $name
      Alive        = $alive
      TcpSessions  = $sessions
      Title        = $title
      WorkingSetMB = [math]::Round($process.WorkingSet64 / 1MB, 1)
      Passed       = ($hasSession -eq $expectSession)
    }
  } finally {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
  }
}

$common = @("-host=$HostName", "-port=$Port", '-viewonly=yes')
$results = @()
foreach ($encoding in $Encodings) {
  $results += Invoke-ViewerCase "encoding=$encoding" ($common + "-password=$Password" + "-encoding=$encoding") $true
}
if (-not $SkipWrongPassword) {
  $results += Invoke-ViewerCase 'wrong password' ($common + '-password=wrong-pw') $false
}

$results | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$failures = @($results | Where-Object { -not $_.Passed })
if ($failures.Count -gt 0) {
  Write-Error "$($failures.Count) smoke test case(s) failed."
  exit 1
}
Write-Host 'Smoke test passed.'
