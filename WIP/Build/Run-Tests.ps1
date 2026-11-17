<#
.SYNOPSIS
    Runs Tessera's checks. Nothing appears on the screen.

.DESCRIPTION
    LayoutTests checks the layout edits and the settings file. Then Tessera draws its editor and its drag
    overlay into pictures, on a desktop of its own (HiddenRun), and they are saved as PNGs in
    %LOCALAPPDATA%\Tessera-build\snapshots to be looked at.

    UiTest also runs long random sessions in the editor and in settings (1,500 and 1,200 actions) and shows
    and hides the drag overlay 200 times, checking afterwards that nothing crashed, no preset was damaged and
    no drawing resources were left held. Give it another seed to run a different session:
        HiddenRun.exe 400 UiTest.exe Tessera.exe log.txt 42

    Not covered: dragging a real window. That needs mouse input on the desktop in use.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$build = Join-Path $env:LOCALAPPDATA 'Tessera-build'
$shots = Join-Path $build 'snapshots'

& (Join-Path $build 'LayoutTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Layout checks failed.' }

# The editor and settings windows, clicked through on a hidden desktop by UiTest.
$log = Join-Path $build 'uitest.log'
if (Test-Path $log) { Remove-Item $log -Force }
& (Join-Path $build 'HiddenRun.exe') 400 (Join-Path $build 'UiTest.exe') (Join-Path $build 'Tessera.exe') $log
$uiCode = $LASTEXITCODE
if (Test-Path $log) { Get-Content $log | ForEach-Object { Write-Host "  $_" } }
if ($uiCode -ne 0) { throw "The interface checks failed ($uiCode)." }

New-Item -ItemType Directory -Force $shots | Out-Null
Get-ChildItem $shots -File | Remove-Item -Force
& (Join-Path $build 'HiddenRun.exe') 20 (Join-Path $build 'Tessera.exe') --snapshot $shots
if ($LASTEXITCODE -ne 0) { throw "The snapshot run failed ($LASTEXITCODE)." }

Add-Type -AssemblyName System.Drawing
foreach ($bmp in Get-ChildItem $shots -Filter *.bmp) {
    $image = [System.Drawing.Image]::FromFile($bmp.FullName)
    try { $image.Save([IO.Path]::ChangeExtension($bmp.FullName, '.png'), [System.Drawing.Imaging.ImageFormat]::Png) } finally { $image.Dispose() }
    Remove-Item $bmp.FullName -Force
}
Get-ChildItem $shots -Filter *.png | ForEach-Object { Write-Host "  $($_.FullName)" -ForegroundColor Green }
$timings = Join-Path $shots 'timings.txt'
if (Test-Path $timings) { Get-Content $timings | ForEach-Object { Write-Host "  $_" } }
