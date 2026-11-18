<#
.SYNOPSIS
    Builds Tessera and puts it in Release\Tessera-<version>.zip.

.DESCRIPTION
    The version is the one in CMakeLists.txt. The zip holds one Tessera\ folder with Tessera.exe and the manual
    (README.md). An existing zip for the same version is never replaced unless -Force is given. A row for the
    version goes into Release\README.md if it hasn't one yet. Tests run only when -RunTests is given.

.PARAMETER Notes
    What the release brings, for its row in Release\README.md.
#>

[CmdletBinding()]
param([string] $Notes = '', [switch] $RunTests, [switch] $Force)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$wip     = Split-Path -Parent $PSScriptRoot
$root    = Split-Path -Parent $wip
$build   = Join-Path $env:LOCALAPPDATA 'Tessera-build'
$release = Join-Path $root 'Release'

$cmake = Get-Content (Join-Path $wip 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(Tessera VERSION (\d+)\.(\d+)\.(\d+)') { throw 'No version found in CMakeLists.txt.' }
$version = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
$rc = Get-Content (Join-Path $wip 'Src\Tessera\Tessera.rc') -Raw
$commas = ($version -replace '\.', ',') + ',0'
if ($rc -notmatch [regex]::Escape("`"FileVersion`", `"$version`"") -or $rc -notmatch [regex]::Escape("`"ProductVersion`", `"$version`"") -or
    $rc -notmatch "FILEVERSION $commas" -or $rc -notmatch "PRODUCTVERSION $commas") {
    throw "Tessera.rc doesn't say version $version, as CMakeLists.txt does."
}
$zip = Join-Path $release "Tessera-$version.zip"
if ((Test-Path $zip) -and -not $Force) { throw "$zip is already there. Raise the version, or use -Force to replace it." }
Write-Host "Tessera $version" -ForegroundColor Cyan

& (Join-Path $PSScriptRoot 'Build.ps1')
if ($RunTests) { & (Join-Path $PSScriptRoot 'Run-Tests.ps1') }

# What goes into the zip is the exe just built, at this version: checked, not assumed.
$built = (Get-Item -LiteralPath (Join-Path $build 'Tessera.exe')).VersionInfo.FileVersion
if ($built -ne $version) { throw "The built Tessera.exe says version $built, not $version." }

$stage = Join-Path ([IO.Path]::GetTempPath()) ("Tessera-release-" + [guid]::NewGuid().ToString('N'))
$folder = Join-Path $stage 'Tessera'
New-Item -ItemType Directory -Force $folder | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $build 'Tessera.exe') -Destination $folder
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $folder
    Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $folder   # the licence asks to travel with every copy
    New-Item -ItemType Directory -Force $release | Out-Null
    $partial = "$zip.partial.zip"
    if (Test-Path $partial) { Remove-Item -LiteralPath $partial -Force }
    Compress-Archive -Path $folder -DestinationPath $partial -CompressionLevel Optimal
    Move-Item -LiteralPath $partial -Destination $zip -Force
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}

$table = Join-Path $release 'README.md'
$Notes = $Notes -replace '\|', '/' -replace '\s*[\r\n]+\s*', ' '   # one row of the table, whatever the notes hold
$lines = if (Test-Path $table) { @(Get-Content $table -Encoding UTF8) } else { @('# Releases', '', '| Version | Date | Notes |', '|---|---|---|') }
if (-not ($lines | Where-Object { $_ -match "^\|\s*$([regex]::Escape($version))\s*\|" })) {
    $header = [array]::IndexOf($lines, '|---|---|---|')
    $row = "| $version | $(Get-Date -Format 'yyyy-MM-dd') | $Notes |"
    $lines = if ($header -ge 0) { $lines[0..$header] + $row + $(if ($header + 1 -lt $lines.Count) { $lines[($header + 1)..($lines.Count - 1)] } else { @() }) } else { $lines + $row }
    [IO.File]::WriteAllLines($table, [string[]]$lines, (New-Object Text.UTF8Encoding $false))
}

Write-Host ''
Write-Host "  $zip" -ForegroundColor Green
