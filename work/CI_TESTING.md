# CI Testing

This repository now uses the same 2-layer validation strategy as `TBlur` and `TNoise`:

1. GitHub-hosted build CI
2. Self-hosted runtime smoke CI

## 1. GitHub-hosted build CI

Workflow:

- `.github/workflows/nuke-build.yml`

What it does:

- downloads the Nuke installer for each supported `major.minor` version
- extracts a buildable local Nuke root under `work/target/nuke/deps/<version>/`
- builds the CUDA NDK plugin on GitHub-hosted Windows and Linux runners
- verifies that the expected binary was produced
- bundles release artifacts and syncs `publish/TVectorBlur/bin/`

Supported hosted build matrix:

- `13.0`
- `13.1`
- `13.2`
- `14.0`
- `14.1`
- `15.0`
- `15.1`
- `15.2`
- `16.0`
- `17.0`

What it does not prove:

- that Nuke itself can launch the plugin at runtime
- that the node can be created and rendered inside a licensed Nuke session
- that the CUDA path works on a real GPU

## 2. Self-hosted runtime smoke CI

Workflow:

- `.github/workflows/nuke-runtime-smoke.yml`

What it does:

- launches a real Nuke executable in headless mode
- loads the plugin from `publish/`
- creates the node
- renders one frame

This is the workflow that proves the plugin actually works inside Nuke.

## Required self-hosted runner setup

Each runtime runner must have:

- a valid Nuke license accessible from that machine
- Nuke installed locally for the versions you want to test
- GitHub Actions runner labels:
  - `self-hosted`
  - `nuke`
  - one OS label: `windows` or `linux`

Each installed Nuke version must expose an environment variable with this pattern:

```text
NUKE_13_0_EXECUTABLE
NUKE_13_1_EXECUTABLE
NUKE_13_2_EXECUTABLE
NUKE_14_0_EXECUTABLE
NUKE_14_1_EXECUTABLE
NUKE_15_0_EXECUTABLE
NUKE_15_1_EXECUTABLE
NUKE_15_2_EXECUTABLE
NUKE_16_0_EXECUTABLE
NUKE_17_0_EXECUTABLE
```
