# TVectorBlur Nuke Plugin

`work/` contains the source code, build scripts, smoke tests, and local package
staging used to produce the distributable `publish/` payload.

## Main layout

- `src/`: NDK + CUDA source
- `scripts/`: build, packaging, and runtime smoke helpers
- `TVectorBlur/`: local package staging used during builds
- `build_all_windows_versions.ps1`: batch Windows build helper
- `CMakeLists.txt`: native build entry point

## Quick build

GitHub-hosted style build:

```bash
cd work
cargo xtask --compile --nuke-versions 16.0 --target-platform windows --output-to-package --limit-threads
```

Direct local build from an installed Nuke:

```powershell
cd work
./scripts/build_windows.ps1 -NukeRoot "C:\Program Files\Nuke16.0v9" -CudaRoot "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
```

Build every installed Windows Nuke version:

```powershell
cd work
./build_all_windows_versions.ps1 -CudaRoot "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
```

Linux:

```bash
cd work
./scripts/build_linux.sh /opt/Nuke16.0v9
```

Linux self-hosted runner helpers:

- `scripts/write_linux_runner_env.sh`
- `scripts/check_linux_runner.sh`
- `scripts/start_linux_runner_with_env.sh`
- `docs/LINUX_RUNNER.md`
- `CI_TESTING.md`

## Packaging

Build outputs land in:

`work/TVectorBlur/bin/<nuke_version>/<os>/`

The publish workflows then sync the compiled binaries into:

`publish/TVectorBlur/bin/<nuke_version>/<os>/`

Local build scripts now do this sync automatically so the `publish/` payload
stays ready to zip or ship without a manual copy step.
