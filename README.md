# TVectorBlur

CUDA NDK implementation of `TVectorBlur` for Nuke, packaged so one repository can build and ship binaries for multiple Nuke versions on Windows and Linux.

Current package version: `1.0.0`

## Repository layout

- `src/`: NDK/CUDA sources
- `resources/`: icon and UI resources
- `bin/<major.minor>/<os>/<arch>/`: compiled plugin binaries picked automatically by the Python loader
- `scripts/`: local build entry points for Windows and Linux
- `docs/`: installation and release notes
- `.github/workflows/build.yml`: GitHub Actions matrix for self-hosted runners
- `.github/workflows/release.yml`: tag-driven build/package workflow for commercial release zips

## Important constraint

Nuke plugins are ABI-coupled to the Nuke SDK. That means there is not a single binary for "all Nuke versions".

The portable way to do this is:

1. build one binary per Nuke `major.minor`
2. store each result under `bin/<major.minor>/<os>/<arch>/`
3. let `_plugin_loader.py` select the matching binary at runtime

That is exactly what this repository is prepared to do.

## Local build

### Windows

```powershell
./scripts/build_windows.ps1 -NukeRoot "C:\Program Files\Nuke16.0v9"
```

Optional:

- `-Configuration Release|Debug`
- `-CudaArchitectures native|89|86|...`
- `-CudaRoot "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"`

Build every Nuke version installed on the workstation:

```powershell
./scripts/build_all_windows.ps1 -CudaRoot "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
```

### Linux

```bash
./scripts/build_linux.sh /opt/Nuke16.0v9
```

Optional:

- second arg: build type, default `Release`
- third arg: CUDA architectures, default `native`

## GitHub Actions

The workflow uses self-hosted runners because Nuke and the NDK cannot be fetched on public runners by default.

Repository variables expected by the workflow:

- `NUKE_13_0_ROOT`
- `NUKE_14_0_ROOT`
- `NUKE_15_0_ROOT`
- `NUKE_16_0_ROOT`
- `NUKE_15_1_ROOT`
- `NUKE_17_0_ROOT`

Recommended runner labels:

- Windows runner: `self-hosted`, `windows`, `x64`, `nuke`, `cuda`
- Linux runner: `self-hosted`, `linux`, `x64`, `nuke`, `cuda`

If you want to support more versions, add another matrix entry and the corresponding repository variable.

## Packaging a release

Build first, then package a target:

```powershell
python ./scripts/package_release.py --target 16.0/windows/x86_64
```

The zip will be written to `dist/` and will contain only the runtime package files plus the matching binary target.

Package every target already present under `bin/`:

```powershell
python ./scripts/package_all_releases.py
```

More distribution notes:

- [docs/INSTALL.md](docs/INSTALL.md)
- [docs/RELEASES.md](docs/RELEASES.md)

## Installation in `.nuke`

This repository is already structured like a Nuke package. To install it locally, put the folder in `.nuke` and add its path from your global `.nuke/init.py`, or package it as part of your studio plugin deployment.
