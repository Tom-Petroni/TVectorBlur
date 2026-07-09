# Template Workflow

This repository is meant to be cloned, renamed, and reused for new Nuke node projects.

## 1. Scaffold a New Node

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Initialize-NukeTemplate.ps1 -NodeName MyNode -Backend cpu
```

Examples:

- CPU node: `-NodeName MyNode -Backend cpu`
- full CUDA node: `-NodeName MyFilter -Backend cuda`
- mixed CPU/CUDA node: `-NodeName MyHybrid -Backend hybrid`

What the script updates:

- `config/build_profile.json`
- `node.json`
- `work/<NodeName>/`
- `publish/<NodeName>/`
- `work/crates/<node-name>-nuke/`
- C++ file stems such as `t_vector_blur.cpp`
- package-qualified Python imports and expected binary names

The scaffold intentionally clears `work/<NodeName>/bin` and `publish/<NodeName>/bin` so the next build always produces fresh binaries for the new node.

## 2. Replace the Implementation

After scaffolding:

- edit the native C++ sources in `work/crates/<node-name>-nuke/src/`
- adjust the Rust bridge if needed
- update the Python bootstrap and menu files under `work/<NodeName>/`
- replace the icons in `work/<NodeName>/resources/`

## 3. Validate Locally

```powershell
cd work
cargo check -p my-node-nuke
python -m compileall .\MyNode
```

Optional first package build:

```powershell
cargo xtask --compile --nuke-versions 17.0 --target-platform windows --output-to-package --limit-threads
```

## 4. Let GitHub Actions Validate the Matrix

Push to `main` or open a pull request and the workflow will:

- fetch Nuke SDK/installers
- compile the configured versions from `13.0` through `17.0`
- validate the packaging layout
- bundle the install payload
- sync `publish/` after a successful `main` build

## 5. Compatibility Expectations

This template gives you a strong default for multi-version builds, not a magical guarantee that every future node compiles unchanged.

Typical outcomes:

- most CPU nodes should compile with little or no infrastructure work
- CUDA and hybrid nodes reuse the same CI/build pipeline, but may need version-specific code guards
- when a build fails, the issue is usually in node compatibility details rather than in the pipeline itself
