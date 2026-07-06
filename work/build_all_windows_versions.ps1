param(
    [string[]]$Versions = @("13.0", "13.1", "13.2", "14.0", "14.1", "15.0", "15.1", "15.2", "16.0", "17.0"),
    [string]$Configuration = "Release",
    [string]$CudaArchitectures = "",
    [string]$CudaRoot = "",
    [string]$Generator = "Visual Studio 17 2022",
    [switch]$DeployToPublish,
    [switch]$RequireAllVersions
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$singleBuildScript = Join-Path $scriptRoot "scripts\build_windows.ps1"

foreach ($version in $Versions) {
    $nukeRoot = Get-ChildItem "C:\Program Files" -Directory |
        Where-Object { $_.Name -match ("^Nuke" + [regex]::Escape($version) + "v\d+$") } |
        Select-Object -First 1 -ExpandProperty FullName

    if (-not $nukeRoot) {
        $message = "Unable to find a Nuke installation for version '$version' under C:\Program Files."
        if ($RequireAllVersions) {
            throw $message
        }
        Write-Warning $message
        continue
    }

    Write-Host ""
    Write-Host "=== Building Nuke $version (windows) ===" -ForegroundColor Cyan
    & $singleBuildScript `
        -NukeRoot $nukeRoot `
        -Configuration $Configuration `
        -CudaArchitectures $CudaArchitectures `
        -CudaRoot $CudaRoot `
        -Generator $Generator

    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for Nuke $version."
    }
}

if ($DeployToPublish) {
    $workBin = Join-Path $scriptRoot "TVectorBlur\bin"
    $publishBin = Join-Path (Split-Path $scriptRoot -Parent) "publish\TVectorBlur\bin"
    if (Test-Path $publishBin) {
        Remove-Item -Recurse -Force $publishBin
    }
    New-Item -ItemType Directory -Force -Path $publishBin | Out-Null
    Copy-Item -LiteralPath $workBin\* -Destination $publishBin -Recurse -Force
    Write-Host ""
    Write-Host "Publish bin synced to $publishBin" -ForegroundColor Green
}

Write-Host ""
Write-Host "All requested versions built successfully." -ForegroundColor Green
