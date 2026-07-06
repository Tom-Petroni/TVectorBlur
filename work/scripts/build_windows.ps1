param(
    [Parameter(Mandatory = $true)]
    [string]$NukeRoot,
    [string]$Configuration = "Release",
    [string]$CudaArchitectures = "native",
    [string]$CudaRoot = "",
    [string]$Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$NukeVersion = [regex]::Match((Split-Path $NukeRoot -Leaf), "\d+\.\d+").Value
if (-not $NukeVersion) {
    throw "Unable to detect Nuke version from path '$NukeRoot'."
}

$BuildDir = Join-Path $ProjectRoot ("build\windows-nuke-" + $NukeVersion)

$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    $cacheText = Get-Content -LiteralPath $cacheFile -Raw
    if ($cacheText -notmatch [regex]::Escape($ProjectRoot)) {
        Remove-Item -Recurse -Force $BuildDir
    }
}

$cmakeArgs = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-G", $Generator,
    "-A", "x64",
    "-DTVECTORBLUR_NUKE_ROOT=$NukeRoot",
    "-DCMAKE_CUDA_ARCHITECTURES=$CudaArchitectures"
)

if ($CudaRoot) {
    $cmakeArgs += @("-T", "cuda=$CudaRoot")
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed for '$NukeRoot'."
}

& cmake --build $BuildDir --config $Configuration --target TVectorBlurCUDA
if ($LASTEXITCODE -ne 0) {
    throw "Build failed for '$NukeRoot'."
}

& python (Join-Path $PSScriptRoot "sync_publish_bins.py")
if ($LASTEXITCODE -ne 0) {
    throw "Unable to sync publish binaries after building '$NukeRoot'."
}
