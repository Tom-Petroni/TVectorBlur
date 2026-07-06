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

- Nuke: `13.0`, `13.1`, `13.2`, `14.0`, `14.1`, `15.0`, `15.1`, `15.2`, `16.0`, `17.0`
- OS: Windows, Linux
- Backend: CUDA

Binaries are versioned in:

`publish/TVectorBlur/bin/<nuke_version>/<os>/`

## Build From Source

GitHub-hosted build command, same idea as `TBlur` / `TNoise`:

```bash
cd work
cargo xtask --compile --nuke-versions 16.0 --target-platform windows --output-to-package --limit-threads
```

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
- `work/CI_TESTING.md`

`nuke-build.yml` is now the hosted pipeline:

- downloads the Nuke installers per supported minor version
- extracts a buildable local Nuke root into `work/target/nuke/deps/`
- builds `TVectorBlur` on GitHub-hosted Windows and Linux runners
- bundles the multi-version package and syncs `publish/`

Runner environment variables expected by the self-hosted workflows:

- `NUKE_13_0_ROOT`
- `NUKE_13_1_ROOT`
- `NUKE_13_2_ROOT`
- `NUKE_14_0_ROOT`
- `NUKE_14_1_ROOT`
- `NUKE_15_0_ROOT`
- `NUKE_15_1_ROOT`
- `NUKE_15_2_ROOT`
- `NUKE_16_0_ROOT`
- `NUKE_17_0_ROOT`
- `NUKE_13_0_EXECUTABLE`
- `NUKE_13_1_EXECUTABLE`
- `NUKE_13_2_EXECUTABLE`
- `NUKE_14_0_EXECUTABLE`
- `NUKE_14_1_EXECUTABLE`
- `NUKE_15_0_EXECUTABLE`
- `NUKE_15_1_EXECUTABLE`
- `NUKE_15_2_EXECUTABLE`
- `NUKE_16_0_EXECUTABLE`
- `NUKE_17_0_EXECUTABLE`

## Repository Layout

```text
TVectorBlur/
  publish/   # install payload for .nuke
  work/      # source, native build scripts, smoke helpers
```
