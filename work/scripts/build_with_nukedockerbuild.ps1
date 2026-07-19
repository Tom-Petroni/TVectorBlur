[CmdletBinding()]
param(
    [string[]]$NukeVersions = @("17.0"),
    [ValidateSet("windows", "linux")]
    [string[]]$Platforms = @("windows", "linux"),
    [string]$NukeDockerBuildRoot = "",
    [switch]$CudaBackend,
    [string]$CudaImage = "nvidia/cuda:12.6.3-devel-ubi8",
    [string]$CudaArchitectures = "75,86,89,90",
    [switch]$SkipBaseImageBuild,
    [switch]$RebuildBaseImage,
    [switch]$RebuildBuilderImage,
    [switch]$ValidatePackage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Convert-ToWslPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $normalized = $fullPath -replace "\\", "/"

    if ($normalized -match "^([A-Za-z]):/(.*)$") {
        return "/mnt/$($matches[1].ToLowerInvariant())/$($matches[2])"
    }

    throw "Cannot convert path to WSL format: $Path"
}

function Get-PythonLauncher {
    if (Get-Command python -ErrorAction SilentlyContinue) {
        return @("python")
    }

    if (Get-Command py -ErrorAction SilentlyContinue) {
        return @("py", "-3")
    }

    throw "Python was not found in PATH."
}

function Normalize-NukeVersion {
    param([Parameter(Mandatory = $true)][string]$Version)

    $trimmed = $Version.Trim()
    if ($trimmed -match '^\d+$') {
        return "$trimmed.0"
    }

    if ($trimmed -match '^\d+\.\d+$') {
        return $trimmed
    }

    return $trimmed
}

function Format-CudaArchitectures {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Architectures,
        [Parameter(Mandatory = $true)]
        [string]$Delimiter
    )

    $tokens = @([regex]::Split($Architectures.Trim(), '[,;\s]+') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($tokens.Count -eq 0) {
        throw "No CUDA architectures were provided."
    }

    return ($tokens -join $Delimiter)
}

function Get-VsDevCmdPath {
    $candidates = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "VsDevCmd.bat introuvable. Installe Visual Studio Build Tools 2022 avec le workload C++."
}

function Get-CudaToolkitPath {
    if (-not [string]::IsNullOrWhiteSpace($env:CUDA_PATH) -and (Test-Path -LiteralPath (Join-Path $env:CUDA_PATH "bin\\nvcc.exe"))) {
        return $env:CUDA_PATH
    }

    $cudaRoot = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA"
    if (-not (Test-Path -LiteralPath $cudaRoot)) {
        throw "CUDA toolkit introuvable. Installe CUDA Toolkit sous Windows ou definis CUDA_PATH."
    }

    $candidate = Get-ChildItem -LiteralPath $cudaRoot -Directory |
        Sort-Object Name -Descending |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "bin\\nvcc.exe") } |
        Select-Object -First 1

    if ($null -eq $candidate) {
        throw "Aucun CUDA toolkit Windows valide n'a ete trouve dans $cudaRoot."
    }

    return $candidate.FullName
}

function Invoke-InVsDevShell {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command
    )

    $vsDevCmd = Get-VsDevCmdPath
    $cmdLine = "call `"$vsDevCmd`" -arch=amd64 -host_arch=amd64 -no_logo >nul && $Command"
    & cmd.exe /d /v:on /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "Commande echouee dans le shell Visual Studio: $Command"
    }
}

function Invoke-WindowsCudaBuilds {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Versions,
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,
        [Parameter(Mandatory = $true)]
        [string]$CudaArchitectures
    )

    $cudaPath = Get-CudaToolkitPath
    $workRoot = Join-Path $RepoRoot "work"
    $cudaBin = Join-Path $cudaPath "bin"
    $cmakeCudaArchitectures = Format-CudaArchitectures -Architectures $CudaArchitectures -Delimiter ";"

    Push-Location $workRoot
    try {
        foreach ($version in $Versions) {
            Write-Host ""
            Write-Host "=== Building TVectorBlur CUDA for Nuke $version (windows) ===" -ForegroundColor Cyan
            $buildCommand = @(
                "set `"CUDA_PATH=$cudaPath`"",
                "set `"TVECTORBLUR_CUDA_ARCHITECTURES=$cmakeCudaArchitectures`"",
                "set `"PATH=$cudaBin;!PATH!`"",
                "cargo xtask --compile --nuke-versions $version --target-platform windows --output-to-package --limit-threads --cuda-backend"
            ) -join " && "
            Invoke-InVsDevShell -Command $buildCommand
        }
    }
    finally {
        Pop-Location
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\\.."))

if ([string]::IsNullOrWhiteSpace($NukeDockerBuildRoot)) {
    $NukeDockerBuildRoot = Join-Path (Split-Path -Parent $repoRoot) "NukeDockerBuild"
}

if (-not (Test-Path -LiteralPath $repoRoot)) {
    throw "Repo root not found: $repoRoot"
}

if (-not (Test-Path -LiteralPath $NukeDockerBuildRoot)) {
    throw "NukeDockerBuild root not found: $NukeDockerBuildRoot"
}

$scriptPath = Join-Path $repoRoot "work\\scripts\\build_with_nukedockerbuild.sh"
$normalizedVersions = @($NukeVersions | ForEach-Object { Normalize-NukeVersion -Version ([string]$_) })
$linuxPlatforms = @($Platforms | Where-Object { $_ -eq "linux" })
$windowsPlatforms = @($Platforms | Where-Object { $_ -eq "windows" })

if ($CudaBackend) {
    Write-Verbose "-CudaBackend is accepted for compatibility; TVectorBlur is always CUDA-enabled."
}

if ($windowsPlatforms.Count -gt 0) {
    Invoke-WindowsCudaBuilds -Versions $normalizedVersions -RepoRoot $repoRoot -CudaArchitectures $CudaArchitectures
}

if ($linuxPlatforms.Count -gt 0) {
    $null = wsl --status
    if ($LASTEXITCODE -ne 0) {
        throw "WSL is not available on this machine."
    }

    if (-not (Test-Path -LiteralPath $scriptPath)) {
        throw "Missing shell entrypoint: $scriptPath"
    }

    $wslCudaArchitectures = Format-CudaArchitectures -Architectures $CudaArchitectures -Delimiter ","

    $wslArgs = @(
        "bash",
        (Convert-ToWslPath -Path $scriptPath),
        "--repo-root",
        (Convert-ToWslPath -Path $repoRoot),
        "--nukedockerbuild-root",
        (Convert-ToWslPath -Path $NukeDockerBuildRoot),
        "--versions",
        ($normalizedVersions -join ","),
        "--platforms",
        ($linuxPlatforms -join ","),
        "--cuda-image",
        $CudaImage,
        "--cuda-architectures",
        $wslCudaArchitectures
    )

    if ($SkipBaseImageBuild) {
        $wslArgs += "--skip-base-image-build"
    }

    if ($RebuildBaseImage) {
        $wslArgs += "--rebuild-base-image"
    }

    if ($RebuildBuilderImage) {
        $wslArgs += "--rebuild-builder-image"
    }

    & wsl @wslArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ($ValidatePackage) {
    $pythonCommand = @(Get-PythonLauncher)
    $validationScript = Join-Path $repoRoot "work\\scripts\\validate_package_import.py"
    $packageRoot = Join-Path $repoRoot "work"
    $validationArgs = @(
        $validationScript,
        "--package-root",
        $packageRoot,
        "--package-name",
        "TVectorBlur",
        "--node-class",
        "TVectorBlur"
    )

    foreach ($platform in $Platforms) {
        $validationArgs += @("--include-os", $platform)
    }

    $pythonExe = $pythonCommand[0]
    $pythonPrefixArgs = @()
    if ($pythonCommand.Length -gt 1) {
        $pythonPrefixArgs = $pythonCommand[1..($pythonCommand.Length - 1)]
    }

    & $pythonExe @pythonPrefixArgs @validationArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
