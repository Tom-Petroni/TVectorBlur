# Publish Output

This directory contains the packaged plugin layout that is ready to copy into a Nuke `.nuke` environment.

## Contents

- `TVectorBlur/`: current placeholder plugin package
- `init.py`: bootstrap that registers the packaged plugin folder

## Binary Layout

Compiled binaries are expected here:

`TVectorBlur/bin/<nuke_version>/<os>/<arch>/`

Examples:

- `TVectorBlur/bin/17.0/windows/x86_64/TVectorBlur.dll`
- `TVectorBlur/bin/17.0/linux/x86_64/libTVectorBlur.so`

## Notes

- This folder is refreshed by the GitHub Actions `sync_publish` job after successful matrix builds.
- The placeholder plugin name is intentionally generic.
- For a real node, run the scaffold script from the repository root to rename the template before shipping.
