param(
    [string]$Configuration = "Release",
    [string]$CudaArchitectures = "native",
    [string]$CudaRoot = "",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$SearchRoot = "C:\Program Files"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SingleBuildScript = Join-Path $ScriptDir "build_windows.ps1"

$nukeRoots = Get-ChildItem $SearchRoot -Directory |
    Where-Object { $_.Name -match '^Nuke\d+\.\d+v\d+$' } |
    Sort-Object Name

if (-not $nukeRoots) {
    throw "No Nuke installations were found under '$SearchRoot'."
}

foreach ($nukeRoot in $nukeRoots) {
    Write-Host "==== Building TVectorBlur for $($nukeRoot.FullName) ===="
    & $SingleBuildScript `
        -NukeRoot $nukeRoot.FullName `
        -Configuration $Configuration `
        -CudaArchitectures $CudaArchitectures `
        -CudaRoot $CudaRoot `
        -Generator $Generator
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for '$($nukeRoot.FullName)'."
    }
}
