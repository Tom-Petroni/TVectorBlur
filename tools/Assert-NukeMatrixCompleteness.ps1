param(
    [string]$BinRoot = "",
    [string]$ConfigPath = "config/nuke_versions.json",
    [string]$BuildProfilePath = "config/build_profile.json",
    [string]$SelectedVersions = "",
    [bool]$IncludeWindows = $true,
    [bool]$IncludeLinux = $true
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (
    -not [System.IO.Path]::IsPathRooted($ConfigPath) `
    -and -not (Test-Path -LiteralPath $ConfigPath)
) {
    $ConfigPath = Join-Path $repoRoot $ConfigPath
}
if (
    -not [System.IO.Path]::IsPathRooted($BuildProfilePath) `
    -and -not (Test-Path -LiteralPath $BuildProfilePath)
) {
    $BuildProfilePath = Join-Path $repoRoot $BuildProfilePath
}
$matrixScript = Join-Path $PSScriptRoot "Get-NukeBuildMatrix.ps1"
$profileScript = Join-Path $PSScriptRoot "Get-NukeBuildProfile.ps1"

$matrix = & $matrixScript -Mode build -ConfigPath $ConfigPath -SelectedVersions $SelectedVersions -IncludeWindows:$IncludeWindows -IncludeLinux:$IncludeLinux
$profile = & $profileScript -ConfigPath $BuildProfilePath
$pluginBinaryName = [string]$profile.plugin_binary_name

if ([string]::IsNullOrWhiteSpace($BinRoot)) {
    $BinRoot = [string]$profile.default_bin_root
}

if (-not (Test-Path -LiteralPath $BinRoot)) {
    throw "Bin root not found: $BinRoot"
}

foreach ($entry in @($matrix.include)) {
    $expectedBinary = "{0}{1}.{2}" -f $entry.lib_prefix, $pluginBinaryName, $entry.lib_ext
    $expectedPath = Join-Path $BinRoot $entry.nuke_version
    $expectedPath = Join-Path $expectedPath $entry.os_name
    $expectedPath = Join-Path $expectedPath $entry.arch_name
    $expectedPath = Join-Path $expectedPath $expectedBinary

    if (-not (Test-Path -LiteralPath $expectedPath)) {
        throw "Missing expected binary for Nuke $($entry.nuke_version) on $($entry.os_name): $expectedPath"
    }
}

Write-Host "Validated complete binary matrix in '$BinRoot'." -ForegroundColor Green
