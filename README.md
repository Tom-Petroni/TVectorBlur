# TVectorBlur

CUDA vector blur node for Nuke, structured like `TBlur` with a clean split between:

- `publish/`: the install payload copied into `.nuke`
- `work/`: source, build tooling, smoke tests, and local package staging

## Install

1. Copy everything from `publish/` into your `.nuke/` folder.
2. Restart Nuke.

Expected result:

- `C:/Users/<user>/.nuke/init.py`
- `C:/Users/<user>/.nuke/TVectorBlur/`

## Compatibility

- Nuke: `13.0`, `14.0`, `15.0`, `16.0`, `17.0`
- OS: Windows, Linux
- Backend: CUDA

Binaries are versioned in:

`publish/TVectorBlur/bin/<nuke_version>/<os>/`

## Build From Source

Windows:

```powershell
cd work
./scripts/build_windows.ps1 -NukeRoot "C:\Program Files\Nuke16.0v9" -CudaRoot "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
```

All installed Windows versions:

```powershell
cd work
./build_all_windows_versions.ps1 -CudaRoot "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
```

Linux:

```bash
cd work
./scripts/build_linux.sh /opt/Nuke16.0v9
```

Linux self-hosted runner:

- setup guide: `work/docs/LINUX_RUNNER.md`
- env generator: `work/scripts/write_linux_runner_env.sh`
- host validator: `work/scripts/check_linux_runner.sh`

## CI / Releases

Main workflows:

- `.github/workflows/nuke-build.yml`
- `.github/workflows/nuke-runtime-smoke.yml`
- `.github/workflows/version-tag.yml`

Runner environment variables expected by the self-hosted workflows:

- `NUKE_13_0_ROOT`
- `NUKE_14_0_ROOT`
- `NUKE_15_0_ROOT`
- `NUKE_16_0_ROOT`
- `NUKE_17_0_ROOT`
- `NUKE_13_0_EXECUTABLE`
- `NUKE_14_0_EXECUTABLE`
- `NUKE_15_0_EXECUTABLE`
- `NUKE_16_0_EXECUTABLE`
- `NUKE_17_0_EXECUTABLE`

Repository variables expected by the build workflow:

- `ENABLE_LINUX_BUILDS=true` to enable Linux build jobs
- `ENABLE_WINDOWS_BUILDS=false` to disable Windows build jobs

## Repository Layout

```text
TVectorBlur/
  publish/   # install payload for .nuke
  work/      # source, native build scripts, smoke helpers
```
