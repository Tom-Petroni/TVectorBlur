# Build

## Local Build

From `work/`:

```powershell
cargo xtask --compile --nuke-versions 16.0 --target-platform windows --output-to-package --limit-threads --cuda-backend
```

Example alternative:

- Linux native:

```powershell
cargo xtask --compile --nuke-versions 16.0 --target-platform linux --output-to-package --limit-threads --cuda-backend
```

Notes:

- le build Windows doit tourner nativement sous Windows
- le build Linux doit tourner nativement sous Linux
- le cross-build Windows -> Linux n'est pas supporte actuellement

## Build Config

`node_build_config.json` controle :

- le type de backend (`CUDA` ici)
- le fait qu'un build natif soit requis
- le package de travail a alimenter
- le fichier de versions supportees

## Output Layout

Expected packaged output:

`TVectorBlur/bin/<nuke_version>/<os>/<arch>/<binary>`

Examples:

- Windows: `TVectorBlur.dll`
- Linux: `libTVectorBlur.so`

## Validation

Useful helper scripts:

- `tools/Get-NukeBuildMatrix.ps1`
- `scripts/validate_package_import.py`

These are used in CI to keep the build matrix and package import behavior coherent.
