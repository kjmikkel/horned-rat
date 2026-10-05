<#
.SYNOPSIS
Installs or uninstalls the horned-rat WinG proxy in the GOG release of Shadow of the Horned Rat.

.EXAMPLE
./install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat'

.EXAMPLE
./install.ps1 -GameDir '...\Warhammer - Shadow of the Horned Rat' -Uninstall
#>
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    # The GOG game folder, or its WARFB subfolder.
    [Parameter(Mandatory)] [string] $GameDir,

    # The proxy DLL to install.
    [Parameter(ParameterSetName = 'Install')] [string] $SourceDll = (Join-Path $PSScriptRoot 'wing32.dll'),

    # Install even if the game files do not match the supported build.
    [Parameter(ParameterSetName = 'Install')] [switch] $Force,

    [Parameter(ParameterSetName = 'Uninstall', Mandatory)] [switch] $Uninstall,

    # SHA-256 of the supported build (spec §8). Overridable for tests only.
    [hashtable] $SupportedHashes = @{
        'WHSHR.EXE'  = 'df462ba4dc29d0832c44f14207b47459f281e6dd4a9e3f3ad972fd43a5921326'
        'wing32.dll' = 'dfc3a949141dad973ad2eb4b66e3c8dc8dc7851fe8f360cd42697aab439d70c0'
    }
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Sha256([string] $Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

if (Test-Path -LiteralPath (Join-Path $GameDir 'WHSHR.EXE')) {
    $warfb = $GameDir
} elseif (Test-Path -LiteralPath (Join-Path $GameDir 'WARFB\WHSHR.EXE')) {
    $warfb = Join-Path $GameDir 'WARFB'
} else {
    throw "WHSHR.EXE not found in '$GameDir' or its WARFB subfolder."
}
$exe = Join-Path $warfb 'WHSHR.EXE'
$wing = Join-Path $warfb 'wing32.dll'
$orig = Join-Path $warfb 'wing32_orig.dll'

if ($Uninstall) {
    if (-not (Test-Path -LiteralPath $orig)) {
        throw "wing32_orig.dll not found in '$warfb': horned-rat is not installed, or the original was lost. Use GOG Galaxy 'Verify / Repair' to restore the original game files."
    }
    Remove-Item -LiteralPath $wing -Force -ErrorAction SilentlyContinue
    Rename-Item -LiteralPath $orig -NewName 'wing32.dll'
    Write-Output "Uninstalled horned-rat from '$warfb'."
    return
}

if (-not (Test-Path -LiteralPath $SourceDll)) {
    throw "Proxy DLL not found: '$SourceDll'."
}
$originalWing = if (Test-Path -LiteralPath $orig) { $orig } else { $wing }
if (-not $Force) {
    if ((Get-Sha256 $exe) -ne $SupportedHashes['WHSHR.EXE']) {
        throw 'Unsupported game version: WHSHR.EXE does not match the supported GOG build. Use -Force to install anyway.'
    }
    if ((Get-Sha256 $originalWing) -ne $SupportedHashes['wing32.dll']) {
        throw 'Unsupported game version: the original wing32.dll does not match the supported GOG build. Use -Force to install anyway.'
    }
}
if (-not (Test-Path -LiteralPath $orig)) {
    Rename-Item -LiteralPath $wing -NewName 'wing32_orig.dll'
}
Copy-Item -LiteralPath $SourceDll -Destination $wing -Force
Write-Output "Installed horned-rat into '$warfb'."
