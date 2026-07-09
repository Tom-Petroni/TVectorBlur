# Nuke Plugin Template

Generic Nuke plugin template with multi-version build automation for Windows and Linux.

## Purpose

This repository is a reusable starting point for building Nuke nodes with:

- native C++ or Rust/C++ plugin code
- optional CPU, CUDA, or hybrid backends
- GitHub Actions matrix builds across supported Nuke versions
- packaged output ready to drop into `.nuke`

It is intentionally template-first. The repository should not be treated as a product node on its own.

## Supported Build Matrix

- Nuke: `13.0` -> `17.0`
- OS: Windows, Linux
- Architecture: `x86_64`

Binary layout:

`publish/<PluginName>/bin/<nuke_version>/<os>/<arch>/`

Local working layout during development:

`work/<PluginName>/bin/<nuke_version>/<os>/<arch>/`

## Current Template Name

The bundled placeholder implementation uses the generic name `TVectorBlur`.

You are expected to rename it for your real node project with the scaffold command below.

## Scaffold a New Node

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\Initialize-NukeTemplate.ps1 -NodeName MyNode -Backend cpu
```

Examples:

- CPU node: `-NodeName MyNode -Backend cpu`
- CUDA node: `-NodeName MyNode -Backend cuda`
- hybrid node: `-NodeName MyNode -Backend hybrid`

What the scaffold updates:

- `config/build_profile.json`
- `node.json`
- `work/<PluginName>/`
- `publish/<PluginName>/`
- `work/crates/<plugin-name>-nuke/`
- C++ file stems, package-qualified imports, and expected binary names

Detailed workflow:

- [work/docs/TEMPLATE_WORKFLOW.md](work/docs/TEMPLATE_WORKFLOW.md)

## Local Build

```powershell
cd work
cargo xtask --compile --nuke-versions 17.0 --target-platform windows --output-to-package --limit-threads
```

Local build output is written to the source workspace first:

`work/TVectorBlur/bin/17.0/windows/x86_64/TVectorBlur.dll`

Other targets:

- native Linux: `--target-platform linux`
- Windows to Linux via WSL: recommended for local Linux builds
- Windows to Linux via Zig: `--use-zig --target-platform linux`

## GitHub Actions

Main workflow:

- `.github/workflows/nuke-build.yml`

It:

- downloads Nuke installers and extracts the required SDK pieces
- builds the configured versions on GitHub-hosted runners
- writes per-job binaries into `work/<PluginName>/bin/...`
- validates the expected package layout
- produces a bundled install artifact
- syncs `publish/` from validated build artifacts after successful `main` builds

Optional runtime smoke workflow:

- `.github/workflows/nuke-runtime-smoke.yml`

It requires self-hosted runners with a real Nuke installation and valid license.

## Repository Layout

```text
Template-Node-Nuke/
  config/    # build profile + version matrix
  publish/   # install payload for .nuke
  tools/     # scaffold + validation helpers
  work/      # source workspace, docs, crate, scripts, xtask
```

## Notes

- The template infrastructure has already been validated in CI across the supported matrix.
- New nodes should usually compile with minimal infrastructure work, but version-specific code adjustments can still be needed depending on the NDK APIs you use.

## License

Commercial usage is governed by `LICENSE` and `EULA.md`.
