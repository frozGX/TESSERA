<#
.SYNOPSIS
    Installs the newest Release\Tessera-<version>.zip into Portable\Tessera, the copy that is actually run.

.DESCRIPTION
    Tessera.cfg (the presets and settings) is left alone. Tessera has to be closed first (tray icon, Exit):
    a running exe can't be replaced, and this script never closes it for you.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root     = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$portable = Join-Path $root 'Portable\Tessera'

$zip = Get-ChildItem (Join-Path $root 'Release') -Filter 'Tessera-*.zip' | Where-Object { $_.Name -match '^Tessera-\d+\.\d+\.\d+\.zip$' } |
    Sort-Object { [version]($_.BaseName -replace '^Tessera-', '') } | Select-Object -Last 1
if (-not $zip) { throw 'No release zip found. Run Make-Release.ps1 first.' }

# A Tessera whose path can't be read is one running as administrator: taken for this one, to be safe.
$running = Get-Process Tessera -ErrorAction SilentlyContinue |
    Where-Object { -not $_.Path -or $_.Path.StartsWith($portable, [StringComparison]::OrdinalIgnoreCase) }
if ($running) { throw 'Tessera is running from the Portable folder. Exit it from its tray icon, then run this again.' }

$stage = Join-Path ([IO.Path]::GetTempPath()) ("Tessera-portable-" + [guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath $stage
    New-Item -ItemType Directory -Force $portable | Out-Null
    Copy-Item -Path (Join-Path $stage 'Tessera\*') -Destination $portable -Recurse -Force
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "  $($zip.Name) -> $portable" -ForegroundColor Green
