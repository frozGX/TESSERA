<#
.SYNOPSIS
    Builds Tessera and its test programs.

.DESCRIPTION
    Output goes to %LOCALAPPDATA%\Tessera-build, outside the Drive-synced project folder, because the sync
    client can briefly lock fresh files and C++ builds write a lot of them.
#>

[CmdletBinding()]
param([switch] $Clean)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$wip   = Split-Path -Parent $PSScriptRoot
$build = Join-Path $env:LOCALAPPDATA 'Tessera-build'

$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $installer -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'No Visual Studio installation with the C++ tools was found.' }

$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmake  = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja  = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

$configure = "`"$cmake`" -S `"$wip`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=`"$ninja`""
$compile   = "`"$cmake`" --build `"$build`""
& cmd.exe /c "`"$vcvars`" >nul && $configure && $compile"
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

Write-Host ''
Write-Host "  $build\Tessera.exe" -ForegroundColor Green
Write-Host ('  {0:N0} KB' -f ((Get-Item "$build\Tessera.exe").Length / 1KB))
