# Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
# Modified for FarSightVNC on 2026-11-02: build/run native GPU presentation regression test.
# Requires an existing Release|x64 viewer build; does not rebuild production projects.
param([switch]$BuildOnly)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$libDir = Join-Path $repo 'build\bin\x64\Release'
$buildDir = Join-Path $repo 'build-logs\gpu-presenter-test'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found.' }
$devCmd = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$libraries = @('region', 'thread', 'log-writer', 'util', 'win-system', 'io-lib', 'file-lib')
foreach ($library in $libraries) {
  if (-not (Test-Path (Join-Path $libDir "$library.lib"))) {
    throw "Missing $library.lib; first run tools/build.ps1 -Configurations Release -Platforms x64."
  }
}
New-Item -ItemType Directory -Force $buildDir | Out-Null
$exe = Join-Path $buildDir 'gpu-presenter-test.exe'
$response = Join-Path $buildDir 'compile.rsp'
$driver = Join-Path $buildDir 'compile.cmd'
$argsLines = @('/nologo', '/EHsc', '/O2', '/MT', '/W3', '/DWIN32', '/DNDEBUG',
  '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0601', "/I`"$repo`"",
  "/Fo`"$buildDir\\`"", "/Fe`"$exe`"",
  "`"$(Join-Path $PSScriptRoot 'gpu-presenter-test.cpp')`"",
  "`"$(Join-Path $repo 'tvnviewer\GpuPresenter.cpp')`"", '/link',
  '/SUBSYSTEM:CONSOLE', "/LIBPATH:`"$libDir`"")
$argsLines += $libraries | ForEach-Object { "$_.lib" }
$argsLines += @('user32.lib', 'gdi32.lib', 'advapi32.lib', 'ole32.lib', 'shell32.lib', 'ws2_32.lib', 'winmm.lib', 'version.lib')
Set-Content -LiteralPath $response -Value ($argsLines -join ' ') -Encoding ASCII
$driverLines = @('@echo off', "call `"$devCmd`" x64 >nul",
  'if errorlevel 1 exit /b %errorlevel%', "cl.exe @`"$response`"", 'exit /b %errorlevel%')
Set-Content -LiteralPath $driver -Value $driverLines -Encoding ASCII
& $env:ComSpec /d /c "`"$driver`""
if ($LASTEXITCODE -ne 0) { throw "GPU test compilation failed ($LASTEXITCODE)." }
if (-not $BuildOnly) {
  & $exe
  if ($LASTEXITCODE -ne 0) { throw "GPU presenter test failed ($LASTEXITCODE)." }
}
