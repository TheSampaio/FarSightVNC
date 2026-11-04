<#
.SYNOPSIS
  Starts or stops a local, loopback-only TightVNC server for viewer tests.

.DESCRIPTION
  Writes a test configuration to HKCU\Software\TightVNC\Server (used by
  tvnserver.exe in application mode, not by the TightVNC service, which reads
  HKLM) and starts tvnserver.exe -run. The server listens on 127.0.0.1 only,
  so no firewall rule is needed and nothing is exposed to the network.
  No installation and no administrator rights are required.

  -Stop terminates the test server. -RemoveConfig also deletes the HKCU key.

.EXAMPLE
  .\tools\test-server.ps1 -Server C:\...\tvnserver.exe -Password <8-char test password>
  .\tools\test-server.ps1 -Stop -RemoveConfig
#>
param(
  [string]$Server,
  [int]$Port = 5910,
  [string]$Password,
  [switch]$Stop,
  [switch]$RemoveConfig
)

$ErrorActionPreference = 'Stop'
$configKey = 'HKCU:\Software\TightVNC\Server'
$pidFile = Join-Path $env:TEMP 'farsightvnc-test-server.pid'

if ($Stop) {
  if (Test-Path $pidFile) {
    Stop-Process -Id ([int](Get-Content $pidFile)) -Force -ErrorAction SilentlyContinue
    Remove-Item $pidFile
  }
  if ($RemoveConfig -and (Test-Path $configKey)) { Remove-Item $configKey -Recurse }
  Write-Host 'Test server stopped.'
  return
}

if (-not $Server -or -not (Test-Path $Server)) { throw 'Pass -Server with the path to tvnserver.exe.' }
if (-not $Password -or $Password.Length -gt 8) { throw 'Pass -Password with 1 to 8 ASCII characters.' }

# VNC stores the password DES-encrypted with a fixed, publicly known key whose
# bits are used in reversed order (classic VNC convention). This is
# obfuscation, not protection: use a throwaway test password only.
function Get-ReversedBits([byte]$value) {
  $result = 0
  for ($bit = 0; $bit -lt 8; $bit++) {
    if ($value -band (1 -shl $bit)) { $result = $result -bor (1 -shl (7 - $bit)) }
  }
  [byte]$result
}
$key = [byte[]](23, 82, 107, 6, 35, 78, 88, 7) | ForEach-Object { Get-ReversedBits $_ }
$plain = New-Object byte[] 8
[Text.Encoding]::ASCII.GetBytes($Password).CopyTo($plain, 0)
$des = [Security.Cryptography.DES]::Create()
$des.Mode = 'ECB'
$des.Padding = 'None'
$encrypted = $des.CreateEncryptor([byte[]]$key, (New-Object byte[] 8)).TransformFinalBlock($plain, 0, 8)

New-Item -Path $configKey -Force | Out-Null
$settings = [ordered]@{
  RfbPort = $Port; AcceptRfbConnections = 1; AcceptHttpConnections = 0
  UseVncAuthentication = 1; UseControlAuthentication = 0
  LoopbackOnly = 1; AllowLoopback = 1; RunControlInterface = 0
  EnableFileTransfers = 1; RemoveWallpaper = 0; UseD3D = 1; UseMirrorDriver = 0; LogLevel = 0
}
foreach ($name in $settings.Keys) {
  New-ItemProperty -Path $configKey -Name $name -PropertyType DWord -Value $settings[$name] -Force | Out-Null
}
New-ItemProperty -Path $configKey -Name Password -PropertyType Binary -Value $encrypted -Force | Out-Null

$process = Start-Process -FilePath $Server -ArgumentList '-run' -PassThru
$process.Id | Out-File $pidFile
Start-Sleep -Seconds 3
$listening = Get-NetTCPConnection -State Listen -OwningProcess $process.Id -ErrorAction SilentlyContinue |
  Where-Object LocalPort -eq $Port
if (-not $listening) { throw "Test server did not start listening on port $Port." }
Write-Host "Test server PID $($process.Id) listening on $($listening[0].LocalAddress):$Port"
