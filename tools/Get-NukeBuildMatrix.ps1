param(
    [ValidateSet("build", "runtime")]
    [string]$Mode = "build",

    [string]$ConfigPath = "config/nuke_versions.json",

    [string]$SelectedVersions = "",

    [bool]$IncludeWindows = $true,

    [bool]$IncludeLinux = $true,

    [bool]$EnableGpu = $false,

    [switch]$AsJson
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (
    -not [System.IO.Path]::IsPathRooted($ConfigPath) `
    -and -not (Test-Path -LiteralPath $ConfigPath)
) {
    $ConfigPath = Join-Path $repoRoot $ConfigPath
}
function Parse-VersionList {
    param(
        [string]$Raw,
        [string[]]$DefaultVersions
    )

    if ([string]::IsNullOrWhiteSpace($Raw)) {
        return $DefaultVersions
    }

    $parsed = @(
        $Raw.Split(",") |
        ForEach-Object { $_.Trim() } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )

    if ($parsed.Count -eq 0) {
        return $DefaultVersions
    }

    return $parsed
}

$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$supportedVersions = @($config.supported_versions)
$versions = @(Parse-VersionList -Raw $SelectedVersions -DefaultVersions $supportedVersions)

$unsupported = @($versions | Where-Object { $_ -notin $supportedVersions })
if ($unsupported.Count -gt 0) {
    throw "Unsupported Nuke versions requested: $($unsupported -join ', ')"
}

if ($Mode -eq "build") {
    $include = @()
    foreach ($version in $versions) {
        foreach ($target in $config.build_targets) {
            if (-not $IncludeWindows -and $target.target_platform -eq "windows") {
                continue
            }
            if (-not $IncludeLinux -and $target.target_platform -eq "linux") {
                continue
            }

            $include += [pscustomobject]@{
                runner          = $target.runner
                target_platform = $target.target_platform
                os_name         = $target.os_name
                arch_name       = $target.arch_name
                lib_ext         = $target.lib_ext
                lib_prefix      = $target.lib_prefix
                nuke_version    = $version
            }
        }
    }

    $result = [pscustomobject]@{
        include = $include
    }
} else {
    $include = @()
    foreach ($version in $versions) {
        if ($IncludeWindows) {
            $target = $config.runtime_targets.windows
            $include += [pscustomobject]@{
                runner_labels_json = (@($target.runner_labels) | ConvertTo-Json -Compress)
                platform_name      = $target.platform_name
                nuke_version       = $version
                enable_gpu         = $EnableGpu
            }
        }

        if ($IncludeLinux) {
            $target = $config.runtime_targets.linux
            $include += [pscustomobject]@{
                runner_labels_json = (@($target.runner_labels) | ConvertTo-Json -Compress)
                platform_name      = $target.platform_name
                nuke_version       = $version
                enable_gpu         = $EnableGpu
            }
        }
    }

    if ($include.Count -eq 0) {
        throw "At least one runtime target must be enabled."
    }

    $result = [pscustomobject]@{
        include = $include
    }
}

if ($AsJson) {
    $result | ConvertTo-Json -Depth 8 -Compress
} else {
    $result
}
