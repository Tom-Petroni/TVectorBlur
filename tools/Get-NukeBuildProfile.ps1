param(
    [string]$ConfigPath = "config/build_profile.json",
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
if (-not (Test-Path -LiteralPath $ConfigPath)) {
    throw "Build profile not found: $ConfigPath"
}

$profile = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json

$supportedBackends = @("cpu", "cuda", "hybrid")
if ($profile.backend -notin $supportedBackends) {
    throw "Unsupported backend '$($profile.backend)'. Expected one of: $($supportedBackends -join ', ')"
}

$pluginKey = [string]$profile.plugin_key
$pluginBinaryName = [string]$profile.plugin_binary_name

if ([string]::IsNullOrWhiteSpace($pluginKey)) {
    throw "Build profile must define plugin_key."
}

if ([string]::IsNullOrWhiteSpace($pluginBinaryName)) {
    throw "Build profile must define plugin_binary_name."
}

$needsCudaToolkit = $profile.backend -in @("cuda", "hybrid")
$xtaskCudaFlag = if ($needsCudaToolkit) { "--cuda-backend" } else { "" }
$defaultBinRoot = Join-Path $repoRoot (Join-Path (Join-Path "publish" $pluginKey) "bin")

$result = [pscustomobject]@{
    plugin_key                = $pluginKey
    plugin_binary_name        = $pluginBinaryName
    backend                   = [string]$profile.backend
    default_bin_root          = $defaultBinRoot
    needs_cuda_toolkit        = $needsCudaToolkit
    xtask_cuda_flag           = $xtaskCudaFlag
    supports_gpu_runtime_test = $needsCudaToolkit
}

if ($AsJson) {
    $result | ConvertTo-Json -Depth 8 -Compress
} else {
    $result
}
