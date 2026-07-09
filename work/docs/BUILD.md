# Build

## Local Build

From `work/`:

```powershell
cargo xtask --compile --nuke-versions 17.0 --target-platform windows --output-to-package --limit-threads
```

Example alternatives:

- Linux native:

```powershell
cargo xtask --compile --nuke-versions 17.0 --target-platform linux --output-to-package --limit-threads
```

- Linux via Zig:

```powershell
cargo xtask --compile --nuke-versions 17.0 --target-platform linux --output-to-package --limit-threads --use-zig
```

## Build Profile

`config/build_profile.json` controls:

- plugin package name
- binary name
- backend mode: `cpu`, `cuda`, or `hybrid`

## Output Layout

Expected packaged output:

`<PluginName>/bin/<nuke_version>/<os>/<arch>/<binary>`

Examples:

- Windows: `<PluginName>.dll`
- Linux: `lib<PluginName>.so`

## Validation

Useful helper scripts:

- `tools/Assert-NukeLayout.ps1`
- `tools/Assert-NukeMatrixCompleteness.ps1`
- `tools/Get-NukeBuildMatrix.ps1`
- `tools/Get-NukeBuildProfile.ps1`

These are used both locally and in CI to ensure the package structure stays consistent.
