# CI Testing

Ce repo utilise une validation en 2 couches :

1. build CI sur runners GitHub-hosted
2. smoke runtime sur runners self-hosted avec un vrai Nuke

## 1. Build CI GitHub-hosted

Workflow :

- `.github/workflows/nuke-build.yml`

Ce qu'il fait :

- telecharge/extrait les morceaux du SDK Nuke necessaires au build
- lit `config/nuke_versions.json` pour la matrice des versions
- lit `node_build_config.json` pour le profil du node
- build les binaires CUDA sur Windows et Linux
- verifie la presence du binaire attendu
- recompose le package final a partir de `publish/`
- valide l'import du package Python avec `work/scripts/validate_package_import.py`
- publie un zip de release lors des releases

Ce que ca ne prouve pas :

- qu'un vrai Nuke licencie charge bien le plugin au runtime
- que la creation du node et le rendu d'une frame fonctionnent dans l'application

## 2. Runtime smoke sur self-hosted

Workflow :

- `.github/workflows/nuke-runtime-smoke.yml`

Ce qu'il fait :

- lance un vrai executable Nuke en headless
- charge le package depuis `publish/`
- cree `TVectorBlur`
- execute un smoke test runtime

Optionnellement, tu peux aussi activer le chemin GPU via l'input `enable_gpu`
si ton runner dispose d'un runtime CUDA adapte.

## Setup requis pour les runners self-hosted

Chaque runner runtime doit avoir :

- une licence Nuke valide
- Nuke installe localement pour les versions voulues
- les labels GitHub Actions :
  - `self-hosted`
  - `nuke`
  - un label OS : `windows` ou `linux`

Chaque version installee doit exposer une variable d'environnement de cette forme :

```text
NUKE_13_0_EXECUTABLE
NUKE_13_1_EXECUTABLE
NUKE_13_2_EXECUTABLE
NUKE_14_0_EXECUTABLE
NUKE_14_1_EXECUTABLE
NUKE_15_0_EXECUTABLE
NUKE_15_1_EXECUTABLE
NUKE_15_2_EXECUTABLE
NUKE_16_0_EXECUTABLE
NUKE_17_0_EXECUTABLE
```

Exemple Windows :

```text
NUKE_16_0_EXECUTABLE=C:\Program Files\Nuke16.0v9\Nuke16.0.exe
```

Exemple Linux :

```text
NUKE_16_0_EXECUTABLE=/usr/local/Nuke16.0v9/Nuke16.0
```

## Recommandation pratique

Utilise GitHub-hosted CI pour :

- valider les builds
- valider le packaging
- generer les binaires par version

Utilise self-hosted runtime CI pour :

- lancer un vrai Nuke
- verifier le chargement du plugin
- verifier la creation du node
- lancer une frame de smoke test

Baseline actuelle du repo :

- node : `TVectorBlur`
- backend : `CUDA`
- matrice publiee : `windows` + `linux`
