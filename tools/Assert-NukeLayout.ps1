param(
    [string]$BinRoot = "",
    [string]$ConfigPath = "config/nuke_versions.json",
    [string]$BuildProfilePath = "config/build_profile.json",
    [string]$RequireVersion = "",
    [string]$RequireOs = ""
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
function Normalize-NukeVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Version
    )

    $trimmed = $Version.Trim()
    if ($trimmed -match '^\d+\.\d+$') {
        return $trimmed
    }

    throw "Invalid Nuke version '$Version'. Expected a major.minor string like 13.0 or 16.0."
}

$profileScript = Join-Path $PSScriptRoot "Get-NukeBuildProfile.ps1"
$profile = & $profileScript -ConfigPath $BuildProfilePath
$pluginBinaryName = [string]$profile.plugin_binary_name

if ([string]::IsNullOrWhiteSpace($BinRoot)) {
    $BinRoot = [string]$profile.default_bin_root
}

$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$supportedVersions = @($config.supported_versions)
$supportedOs = @($config.build_targets | ForEach-Object { $_.os_name } | Sort-Object -Unique)
$supportedArch = @($config.build_targets | ForEach-Object { $_.arch_name } | Sort-Object -Unique)

if (-not (Test-Path -LiteralPath $BinRoot)) {
    throw "Bin root not found: $BinRoot"
}

if (-not [string]::IsNullOrWhiteSpace($RequireVersion)) {
    $RequireVersion = Normalize-NukeVersion -Version $RequireVersion
    if ($RequireVersion -notin $supportedVersions) {
        throw "Required version '$RequireVersion' is not listed in $ConfigPath."
    }
}

if (-not [string]::IsNullOrWhiteSpace($RequireOs) -and $RequireOs -notin $supportedOs) {
    throw "Required OS '$RequireOs' is not one of: $($supportedOs -join ', ')"
}

$versionDirs = @(
    Get-ChildItem -LiteralPath $BinRoot -Directory |
    Where-Object { $_.Name -ne '__pycache__' }
)

foreach ($dir in $versionDirs) {
    if ($dir.Name -notin $supportedVersions) {
        throw "Invalid version directory '$($dir.Name)' under '$BinRoot'. Expected only major.minor folders from config."
    }
}

foreach ($versionDir in $versionDirs) {
    $osDirs = @(Get-ChildItem -LiteralPath $versionDir.FullName -Directory)
    if ($osDirs.Count -eq 0) {
        throw "Version folder '$($versionDir.Name)' contains no OS subdirectories."
    }

    foreach ($osDir in $osDirs) {
        if ($osDir.Name -notin $supportedOs) {
            throw "Invalid OS directory '$($osDir.Name)' under '$($versionDir.Name)'."
        }

        $archDirs = @(Get-ChildItem -LiteralPath $osDir.FullName -Directory)
        if ($archDirs.Count -eq 0) {
            throw "OS folder '$($versionDir.Name)/$($osDir.Name)' contains no architecture subdirectories."
        }

        foreach ($archDir in $archDirs) {
            if ($archDir.Name -notin $supportedArch) {
                throw "Invalid architecture directory '$($archDir.Name)' under '$($versionDir.Name)/$($osDir.Name)'."
            }

            $files = @(Get-ChildItem -LiteralPath $archDir.FullName -File | Where-Object { $_.Name -ne '.gitkeep' })
            if ($files.Count -eq 0) {
                throw "Architecture folder '$($versionDir.Name)/$($osDir.Name)/$($archDir.Name)' contains no binary."
            }

            $expectedBinary = if ($osDir.Name -eq 'windows') { "$pluginBinaryName.dll" } else { "lib$pluginBinaryName.so" }
            if ($files.Count -ne 1 -or $files[0].Name -ne $expectedBinary) {
                $found = if ($files.Count -gt 0) { $files.Name -join ', ' } else { '<none>' }
                throw "Unexpected binaries under '$($versionDir.Name)/$($osDir.Name)/$($archDir.Name)'. Expected '$expectedBinary', found '$found'."
            }
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($RequireVersion)) {
    $requiredVersionPath = Join-Path $BinRoot $RequireVersion
    if (-not (Test-Path -LiteralPath $requiredVersionPath)) {
        throw "Required version folder '$RequireVersion' is missing under '$BinRoot'."
    }

    if (-not [string]::IsNullOrWhiteSpace($RequireOs)) {
        $requiredOsPath = Join-Path $requiredVersionPath $RequireOs
        if (-not (Test-Path -LiteralPath $requiredOsPath)) {
            throw "Required OS folder '$RequireVersion/$RequireOs' is missing under '$BinRoot'."
        }
    }
}

Write-Host "Validated Nuke binary layout in '$BinRoot'." -ForegroundColor Green
