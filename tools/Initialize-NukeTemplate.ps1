param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Z][A-Za-z0-9]*$')]
    [string]$NodeName,

    [ValidateSet("cpu", "cuda", "hybrid")]
    [string]$Backend = "cpu"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function ConvertTo-KebabCase {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    $segments = [regex]::Matches($Value, '[A-Z]+(?=[A-Z][a-z0-9]|[0-9]|\b)|[A-Z]?[a-z0-9]+') |
        ForEach-Object { $_.Value.ToLowerInvariant() }
    return ($segments -join '-')
}

function ConvertTo-SnakeCase {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    $segments = [regex]::Matches($Value, '[A-Z]+(?=[A-Z][a-z0-9]|[0-9]|\b)|[A-Z]?[a-z0-9]+') |
        ForEach-Object { $_.Value.ToLowerInvariant() }
    return ($segments -join '_')
}

function Get-WorkspaceCrateDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceManifestPath
    )

    $manifest = Get-Content -LiteralPath $WorkspaceManifestPath -Raw
    $match = [regex]::Match($manifest, '"crates/([^"]+)"')
    if (-not $match.Success) {
        throw "Unable to determine the active node crate from '$WorkspaceManifestPath'."
    }

    return $match.Groups[1].Value
}

function Get-CrateState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CrateManifestPath
    )

    $manifest = Get-Content -LiteralPath $CrateManifestPath -Raw

    $packageMatch = [regex]::Match($manifest, '(?m)^name\s*=\s*"([^"]+)"')
    if (-not $packageMatch.Success) {
        throw "Unable to read the crate package name from '$CrateManifestPath'."
    }

    $libMatch = [regex]::Match($manifest, '(?ms)^\[lib\].*?^name\s*=\s*"([^"]+)"')
    if (-not $libMatch.Success) {
        throw "Unable to read the crate lib name from '$CrateManifestPath'."
    }

    return [pscustomobject]@{
        PackageName = $packageMatch.Groups[1].Value
        LibName     = $libMatch.Groups[1].Value
    }
}

function Get-PrimarySourceStem {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDirectory
    )

    $cppFiles = Get-ChildItem -LiteralPath $SourceDirectory -Filter *.cpp -File
    if ($cppFiles.Count -eq 0) {
        throw "No C++ source files were found under '$SourceDirectory'."
    }

    return ($cppFiles | Sort-Object { $_.BaseName.Length } | Select-Object -First 1).BaseName
}

function Rename-PathIfPresent {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OldPath,

        [Parameter(Mandatory = $true)]
        [string]$NewPath
    )

    if (-not (Test-Path -LiteralPath $OldPath)) {
        return
    }

    if ($OldPath -eq $NewPath) {
        return
    }

    if (Test-Path -LiteralPath $NewPath) {
        throw "Destination already exists: $NewPath"
    }

    Rename-Item -LiteralPath $OldPath -NewName (Split-Path -Leaf $NewPath)
}

function Replace-InTextFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RootPath,

        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$Replacements
    )

    $allowedExtensions = @(
        ".cpp", ".h", ".hpp", ".rs", ".py", ".ps1", ".json", ".toml",
        ".md", ".txt", ".yml", ".yaml", ".gitignore"
    )
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)

    $files = Get-ChildItem -LiteralPath $RootPath -Recurse -File | Where-Object {
        $fullPath = $_.FullName
        $extension = $_.Extension.ToLowerInvariant()

        if ($fullPath -like "*\.git\*") { return $false }
        if ($fullPath -like "*\work\target\*") { return $false }
        if ($fullPath -like "*\__pycache__\*") { return $false }
        if ($allowedExtensions -notcontains $extension -and $_.Name -notin @(".gitignore")) {
            return $false
        }

        return $true
    }

    foreach ($file in $files) {
        $original = [System.IO.File]::ReadAllText($file.FullName)
        $updated = $original

        foreach ($entry in $Replacements.GetEnumerator()) {
            $updated = $updated.Replace([string]$entry.Key, [string]$entry.Value)
        }

        if ($updated -ne $original) {
            [System.IO.File]::WriteAllText($file.FullName, $updated, $utf8NoBom)
        }
    }
}

function Reset-BinDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BinDirectory
    )

    if (Test-Path -LiteralPath $BinDirectory) {
        Get-ChildItem -LiteralPath $BinDirectory -Force | Remove-Item -Recurse -Force
    } else {
        New-Item -ItemType Directory -Path $BinDirectory | Out-Null
    }

    Set-Content -LiteralPath (Join-Path $BinDirectory ".gitkeep") -Value "" -NoNewline
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$workRoot = Join-Path $repoRoot "work"
$publishRoot = Join-Path $repoRoot "publish"
$buildProfilePath = Join-Path $repoRoot "config\build_profile.json"
$nodeJsonPath = Join-Path $repoRoot "node.json"
$workspaceManifestPath = Join-Path $workRoot "Cargo.toml"

$buildProfile = Get-Content -LiteralPath $buildProfilePath -Raw | ConvertFrom-Json
$currentNodeName = [string]$buildProfile.plugin_key
if ([string]::IsNullOrWhiteSpace($currentNodeName)) {
    throw "config/build_profile.json does not define plugin_key."
}

if ($NodeName -eq $currentNodeName -and $Backend -eq [string]$buildProfile.backend) {
    Write-Host "Template already matches '$NodeName' with backend '$Backend'." -ForegroundColor Yellow
    exit 0
}

$currentCrateDirectoryName = Get-WorkspaceCrateDirectory -WorkspaceManifestPath $workspaceManifestPath
$currentCrateManifestPath = Join-Path $workRoot "crates\$currentCrateDirectoryName\Cargo.toml"
$crateState = Get-CrateState -CrateManifestPath $currentCrateManifestPath
$currentSnakeCaseName = ConvertTo-SnakeCase -Value $currentNodeName
$currentSourceDirectory = Join-Path $workRoot "crates\$currentCrateDirectoryName\src"
$currentSourceStem = Get-PrimarySourceStem -SourceDirectory $currentSourceDirectory

$newSnakeCaseName = ConvertTo-SnakeCase -Value $NodeName
$newKebabCaseName = ConvertTo-KebabCase -Value $NodeName
$newCrateDirectoryName = "$newKebabCaseName-nuke"
$newCratePackageName = "$newKebabCaseName-nuke"
$newCrateLibName = "${newSnakeCaseName}_nuke"
$sourceStemSuffix = if ($currentSourceStem.StartsWith($currentSnakeCaseName)) {
    $currentSourceStem.Substring($currentSnakeCaseName.Length)
} else {
    ""
}
$newSourceStem = "$newSnakeCaseName$sourceStemSuffix"

$currentWorkNodePath = Join-Path $workRoot $currentNodeName
$currentPublishNodePath = Join-Path $publishRoot $currentNodeName
$currentCratePath = Join-Path $workRoot "crates\$currentCrateDirectoryName"

$newWorkNodePath = Join-Path $workRoot $NodeName
$newPublishNodePath = Join-Path $publishRoot $NodeName
$newCratePath = Join-Path $workRoot "crates\$newCrateDirectoryName"

Rename-PathIfPresent -OldPath $currentWorkNodePath -NewPath $newWorkNodePath
Rename-PathIfPresent -OldPath $currentPublishNodePath -NewPath $newPublishNodePath
Rename-PathIfPresent -OldPath $currentCratePath -NewPath $newCratePath

$resourcePairs = @(
    @{ Root = $newWorkNodePath; Old = $currentNodeName; New = $NodeName },
    @{ Root = $newPublishNodePath; Old = $currentNodeName; New = $NodeName }
)

foreach ($pair in $resourcePairs) {
    $resourcesRoot = Join-Path $pair.Root "resources"
    $iconPath = Join-Path $resourcesRoot "$($pair.Old).png"
    $iconsPath = Join-Path $resourcesRoot "$($pair.Old)_Icons.png"

    if (Test-Path -LiteralPath $iconPath) {
        Rename-Item -LiteralPath $iconPath -NewName "$($pair.New).png"
    }

    if (Test-Path -LiteralPath $iconsPath) {
        Rename-Item -LiteralPath $iconsPath -NewName "$($pair.New)_Icons.png"
    }
}

$newSourceDirectory = Join-Path $newCratePath "src"
$sourceFiles = Get-ChildItem -LiteralPath $newSourceDirectory -Filter "$currentSourceStem*.cpp" -File
foreach ($file in $sourceFiles) {
    $newFileName = $file.Name.Replace($currentSourceStem, $newSourceStem)
    Rename-Item -LiteralPath $file.FullName -NewName $newFileName
}

$replacements = New-Object System.Collections.Specialized.OrderedDictionary
$replacements.Add($crateState.PackageName, $newCratePackageName)
$replacements.Add($crateState.LibName, $newCrateLibName)
$replacements.Add($currentSourceStem, $newSourceStem)
$replacements.Add($currentNodeName, $NodeName)

Replace-InTextFiles -RootPath $repoRoot -Replacements $replacements

$updatedBuildProfile = Get-Content -LiteralPath $buildProfilePath -Raw | ConvertFrom-Json
$updatedBuildProfile.plugin_key = $NodeName
$updatedBuildProfile.plugin_binary_name = $NodeName
$updatedBuildProfile.backend = $Backend
$updatedBuildProfile | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $buildProfilePath

$updatedNodeJson = Get-Content -LiteralPath $nodeJsonPath -Raw | ConvertFrom-Json
$updatedNodeJson.key = $NodeName
$updatedNodeJson.label = $NodeName
$updatedNodeJson.class_name = $NodeName
$updatedNodeJson.bootstrap_module = "$NodeName.init"
$updatedNodeJson.notes = "Generated from the multi-version Nuke node template."
$updatedNodeJson | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $nodeJsonPath

Reset-BinDirectory -BinDirectory (Join-Path $newWorkNodePath "bin")
Reset-BinDirectory -BinDirectory (Join-Path $newPublishNodePath "bin")

Write-Host "Template scaffold updated successfully." -ForegroundColor Green
Write-Host "Node name  : $NodeName"
Write-Host "Backend    : $Backend"
Write-Host "Crate path : work/crates/$newCrateDirectoryName"
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Replace the native implementation in work/crates/$newCrateDirectoryName/src/"
Write-Host "  2. Adjust Python menu/bootstrap files under work/$NodeName/"
Write-Host "  3. Run: cd work"
Write-Host "  4. Run: cargo check -p $newCratePackageName"
Write-Host "  5. Run: cargo xtask --compile --nuke-versions 17.0 --target-platform windows --output-to-package --limit-threads"
