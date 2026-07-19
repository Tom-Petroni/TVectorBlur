# TVectorBlur

TVectorBlur est un node Nuke natif, oriente CUDA, pour produire du blur vectoriel
avec des controles de warp, de mask et de look plus pousses qu'un simple blur
directionnel classique.

## Pourquoi TVectorBlur

- blur guide par vecteurs pour motion, smear et stylisation
- execution native CUDA
- package Python + plugin natif pret a installer dans `.nuke`
- builds multi-versions Nuke sur Windows et Linux

## Structure du repo

```text
TVectorBlur/
  config/         # matrice des versions Nuke supportees
  publish/        # payload a copier dans .nuke
  tools/          # scripts utilitaires CI/build matrix
  work/           # source rust/c++ + scripts de validation
  node_build_config.json
  node.json
  VERSION
  CHANGELOG.md
```

Fichiers importants :

- `config/nuke_versions.json`: versions et plateformes supportees
- `node_build_config.json`: profil de build du node
- `publish/`: package de reference pour l'installation artiste et les zips de release
- `work/`: code source natif, package Python de travail et scripts de verification

## Prerequis

- Nuke SDK/headers accessibles via le workflow `xtask`
- Rust/Cargo
- toolchain C++ compatible Nuke
- CUDA Toolkit pour les builds natifs

## Compiler

Depuis la racine du repo :

```powershell
.\work\scripts\build_with_nukedockerbuild.ps1 -NukeVersions 17.0 -Platforms windows,linux -ValidatePackage
```

Ce flux local est hybride :

- Windows : build natif avec Visual Studio Build Tools + CUDA Toolkit
- Linux : build CUDA dans Docker/WSL2 via `NukeDockerBuild`

`TVectorBlur` est CUDA-only, donc l'option `-CudaBackend` est acceptee par
compatibilite avec les autres nodes mais n'est pas necessaire.

Build natif manuel :

```powershell
cd work
cargo xtask --compile --nuke-versions 16.0 --target-platform windows --output-to-package --limit-threads --cuda-backend
```

Sortie locale attendue :

`work/TVectorBlur/bin/16.0/windows/x86_64/TVectorBlur.dll`

Notes :

- build Windows : a lancer nativement sous Windows
- build Linux : a lancer nativement sous Linux
- le cross-build Windows -> Linux n'est pas supporte actuellement par `xtask`

## Build CI GitHub

Le workflow principal (`.github/workflows/nuke-build.yml`) :

- fait les smoke checks Python/Rust sur `push` et `pull_request`
- lit la matrice de build depuis `config/nuke_versions.json`
- lit le profil du node depuis `node_build_config.json`
- build les binaires CUDA sur GitHub Actions
- recompose le package final a partir de `publish/`
- verifie l'import du package Python avant publication
- genere un zip de release pret a installer dans `.nuke`

Le workflow runtime (`.github/workflows/nuke-runtime-smoke.yml`) :

- lance un vrai Nuke headless sur runners self-hosted
- charge le plugin depuis `publish/`
- cree le node
- execute un smoke test runtime

## Installer dans Nuke

1. copier `publish/init.py` dans `.nuke`
2. copier `publish/TVectorBlur/` dans le meme dossier
3. redemarrer Nuke

Si tu as deja un `.nuke/init.py`, tu peux juste ajouter :

```python
import nuke
nuke.pluginAddPath("./TVectorBlur")
```

## Verification rapide

- le menu `Nodes > TVectorBlur` apparait
- le binaire est present dans `TVectorBlur/bin/<nuke_version>/<os>/<arch>/`
- une release GitHub s'installe juste en dezipant le package dans `.nuke`

## Licence

Usage commercial soumis a la licence du repo (`LICENSE` + `EULA.md`).
