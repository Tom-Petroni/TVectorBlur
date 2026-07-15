# Release

## Before Releasing

1. Confirm the version in `VERSION`, `node.json`, and `node_build_config.json`.
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
python -m compileall .\TVectorBlur .\scripts
```

Si `publish/TVectorBlur/bin` contient deja des binaires valides, tu peux aussi verifier le
loader package avec :

```powershell
cd ..
python work/scripts/validate_package_import.py --package-root publish --package-name TVectorBlur --node-class TVectorBlur
```
