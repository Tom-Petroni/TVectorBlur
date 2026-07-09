# Release

## Before Releasing

1. Confirm the scaffolded plugin name and backend in `config/build_profile.json`.
2. Verify the package metadata in `node.json`.
3. Run local syntax and cargo checks.
4. Let GitHub Actions validate the matrix on `main`.

## Artifacts

The release pipeline should produce:

- a bundled install archive
- a synchronized `publish/` directory
- versioned binaries under the expected package layout

## Manual Sanity Checks

- Python files import correctly
- the packaged binary layout is complete
- the plugin loads in the intended Nuke versions

## Suggested Local Commands

```powershell
cd work
cargo check -p xtask
python -m compileall .\TVectorBlur
```

If the plugin has already been scaffolded to a different name, use that package name instead.
