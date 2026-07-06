# Releases

## Commercial release checklist

1. Build each supported Nuke version on each supported OS.
2. Package each target with `scripts/package_release.py`.
3. Verify the zip contents contain only the matching target binary subtree.
4. Test-load the package in a clean `.nuke` environment.
5. Publish the zip files as GitHub release assets or copy them to your store/CDN.

## Example packaging commands

### Windows

```powershell
python ./scripts/package_release.py --target 16.0/windows
```

### Linux

```bash
python3 ./scripts/package_release.py --target 16.0/linux
```

## Recommended public naming

- `TVectorBlur-1.0.0-nuke13.0-windows.zip`
- `TVectorBlur-1.0.0-nuke16.0-linux.zip`

## Licensing note

This repository does not impose an open-source license automatically. Choose your commercial license terms separately before selling or redistributing binaries.
