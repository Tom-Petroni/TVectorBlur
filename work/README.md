# Work Source

Source workspace for the reusable Nuke plugin template.

## Structure

- `crates/t-vector-blur-nuke`: current placeholder native crate
- `xtask`: Nuke SDK fetch, build orchestration, and packaging
- `TVectorBlur/`: current placeholder Python package
- `docs/`: template documentation
- `scripts/`: automation and smoke-test scripts

## Typical Workflow

1. Run the scaffold script from the repository root.
2. Replace the native implementation in the generated crate.
3. Adjust the Python bootstrap/menu files for your node.
4. Run local checks.
5. Push and let GitHub Actions validate the matrix.

## Quick Build

```powershell
cargo xtask --compile --nuke-versions 17.0 --target-platform windows --output-to-package --limit-threads
```

## Documentation

- [Installation](docs/INSTALL.md)
- [Build](docs/BUILD.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Node Reference](docs/NODE_REFERENCE.md)
- [Release](docs/RELEASE.md)
- [Template Workflow](docs/TEMPLATE_WORKFLOW.md)
