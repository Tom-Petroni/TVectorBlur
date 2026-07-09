# CI Testing

This repository now uses a 2-layer validation strategy:

1. GitHub-hosted build CI
2. Self-hosted runtime smoke CI

## 1. GitHub-hosted build CI

Workflow:

- `.github/workflows/nuke-build.yml`

What it does:

- downloads the Nuke installer for each supported `major.minor` version
- extracts the NDK files needed to compile: `include/` + `DDImage`
- builds the plugin on GitHub-hosted Windows/Linux runners
- verifies that the expected binary was produced
- reads `config/build_profile.json` to decide whether the node is `cpu`, `cuda`, or `hybrid`

What it does not prove:

- that Nuke itself can launch the plugin at runtime
- that the node can be created and rendered inside a licensed Nuke session

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

Example on Windows:

```text
NUKE_16_0_EXECUTABLE=C:\Program Files\Nuke16.0v9\Nuke16.0.exe
```

Example on Linux:

```text
NUKE_16_0_EXECUTABLE=/usr/local/Nuke16.0v9/Nuke16.0
```

## What you need to provide

To test every supported OS/version combination at runtime, you need:

- one Windows self-hosted runner with all target Nuke versions installed
- one Linux self-hosted runner with all target Nuke versions installed

If one runner only has `16.0` and `17.0`, then only `16.0` and `17.0` can be runtime-tested on that machine.

## Practical recommendation

Use GitHub-hosted CI for:

- build validation
- packaging validation
- per-version binary generation

Use self-hosted CI for:

- real Nuke launch
- plugin load verification
- node creation
- frame execution smoke tests

Current baseline in this repository:

- plugin example: `TVectorBlur`
- backend profile: `cpu`
- published matrix: `windows` + `linux`
