# Work Source

Workspace source de `TVectorBlur`.

## Structure

- `crates/t-vector-blur-nuke`: crate native du node
- `xtask`: fetch SDK Nuke, orchestration des builds et packaging
- `TVectorBlur/`: package Python de travail
- `docs/`: documentation technique
- `scripts/`: scripts de validation et smoke tests

## Workflow typique

1. coder dans le crate natif et le package Python `work/`
2. lancer les checks locaux
3. builder une cible Nuke locale
4. verifier le package obtenu
5. pousser et laisser la CI valider la matrice

## Quick Build

Depuis la racine du repo, avec Windows natif + Linux Docker/WSL2 :

```powershell
.\work\scripts\build_with_nukedockerbuild.ps1 -NukeVersions 17.0 -Platforms windows,linux -ValidatePackage
```

Options utiles :

- `-SkipBaseImageBuild`: reutilise uniquement les images NukeDockerBuild locales
- `-RebuildBuilderImage`: reconstruit l'image `tvectorblur-builder:*`
- `-CudaArchitectures "75,86,89,90"`: ajuste les architectures CUDA CMake

Build natif manuel depuis `work/` :

```powershell
cargo xtask --compile --nuke-versions 16.0 --target-platform windows --output-to-package --limit-threads --cuda-backend
```

## Documentation

- [Installation](docs/INSTALL.md)
- [Build](docs/BUILD.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Node Reference](docs/NODE_REFERENCE.md)
- [Release](docs/RELEASE.md)
