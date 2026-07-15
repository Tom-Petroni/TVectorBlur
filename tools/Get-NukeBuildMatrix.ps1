param(
    [string]$VersionsConfigPath = "config/nuke_versions.json",
    [string]$NodeConfigPath = "node_build_config.json",
    [string[]]$RequestedVersions = @(),
    [switch]$AsJson
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($VersionsConfigPath)) {
    $VersionsConfigPath = Join-Path $repoRoot $VersionsConfigPath
}
if (-not [System.IO.Path]::IsPathRooted($NodeConfigPath)) {
    $NodeConfigPath = Join-Path $repoRoot $NodeConfigPath
}

function Get-MatrixEntry {
    param(
        [pscustomobject]$Platform,
        [pscustomobject]$Version,
        [string]$BackendMode
    )

    [pscustomobject]@{
        nuke_version    = $Version.version
        full_version    = $Version.full_version_hint
        cpp_standard    = $Version.cpp_standard
        target_platform = $Platform.target_platform
        package_os      = $Platform.package_os
        package_arch    = $Platform.package_arch
        runner          = $Platform.runner
        backend_mode    = $BackendMode
        lib_ext         = if ($Platform.package_os -eq "windows") { "dll" } elseif ($Platform.package_os -eq "linux") { "so" } else { "dylib" }
        lib_prefix      = if ($Platform.package_os -eq "windows") { "" } else { "lib" }
    }
}

$versionsConfig = Get-Content -LiteralPath $VersionsConfigPath -Raw | ConvertFrom-Json
$nodeConfig = Get-Content -LiteralPath $NodeConfigPath -Raw | ConvertFrom-Json
$backendMode = [string]$nodeConfig.build.backend_mode
$nativeBuildRequired = [bool]$nodeConfig.build.native_build_required
$allVersions = @($versionsConfig.versions)

if ($RequestedVersions.Count -gt 0) {
    $requestedLookup = @{}
    foreach ($requestedVersion in $RequestedVersions) {
        $trimmed = [string]$requestedVersion
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $requestedLookup[$trimmed.Trim()] = $true
        }
    }

    $missingVersions = @()
    foreach ($requestedVersion in $requestedLookup.Keys) {
        if (-not ($allVersions.version -contains $requestedVersion)) {
            $missingVersions += $requestedVersion
        }
    }

    if ($missingVersions.Count -gt 0) {
        throw "Unsupported Nuke versions requested: $($missingVersions -join ', ')"
    }

    $selectedVersions = @($allVersions | Where-Object { $requestedLookup.ContainsKey($_.version) })
} else {
    $selectedVersions = $allVersions
}

$cpuEntries = @()
$cudaEntries = @()

if ($nativeBuildRequired -and $backendMode -eq "CPU") {
    foreach ($version in $selectedVersions) {
        foreach ($platform in $versionsConfig.platforms.cpu) {
            $cpuEntries += Get-MatrixEntry -Platform $platform -Version $version -BackendMode $backendMode
        }
    }
}

if ($nativeBuildRequired -and ($backendMode -eq "CUDA" -or $backendMode -eq "Hybrid")) {
    foreach ($version in $selectedVersions) {
        foreach ($platform in $versionsConfig.platforms.cuda) {
            $cudaEntries += Get-MatrixEntry -Platform $platform -Version $version -BackendMode $backendMode
        }
    }
}

$result = [pscustomobject]@{
    native_build_required = $nativeBuildRequired
    has_cpu = $cpuEntries.Count -gt 0
    has_cuda = $cudaEntries.Count -gt 0
    cpu = [pscustomobject]@{
        include = $cpuEntries
    }
    cuda = [pscustomobject]@{
        include = $cudaEntries
    }
}

if ($AsJson) {
    $result | ConvertTo-Json -Depth 8 -Compress
} else {
    $result
}
