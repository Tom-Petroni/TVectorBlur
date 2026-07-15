# Architecture

## Overview

Le repo est split en trois couches :

- a native plugin crate
- a Python package loaded by Nuke
- a build/orchestration layer driven by `xtask`

## Native Layer

`work/crates/t-vector-blur-nuke`

Responsibilities:

- compile a `cdylib` that Nuke can load
- include Nuke SDK headers extracted from installers
- link against the expected Nuke libraries
- expose the node class used by the Python package

## Python Layer

`work/TVectorBlur`

Responsibilities:

- register the plugin path
- resolve the correct binary for the current Nuke version and platform
- create menu entries
- host light bootstrap and UI helper code

## Build Layer

`work/xtask`

Responsibilities:

- fetch Nuke installers for the configured versions
- extract the required SDK artifacts
- build the native plugin for a selected target
- package the result into the expected `bin/<version>/<os>/<arch>/` layout

## Runtime Flow

1. Nuke loads the top-level `init.py`.
2. The TVectorBlur package is added to the Nuke plugin path.
3. The package loader resolves the correct binary folder for the running session.
4. Nuke loads the native plugin binary.
5. The menu bootstrap exposes the node class in the UI.

## Build Flow

1. `xtask` uses `node_build_config.json` and `config/nuke_versions.json`.
2. Missing Nuke SDK artifacts are downloaded and extracted.
3. The native crate is compiled for the selected target platform.
4. The binary is copied into the package layout.
5. Validation scripts confirm the expected matrix and package import behavior.
