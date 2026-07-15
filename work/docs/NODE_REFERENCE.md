# Node Reference

## Overview

TVectorBlur est un node Nuke natif, oriente CUDA, pour produire du blur
vectoriel avec des controles avances de warp, de mask et de faconnage du rendu.

## Current Scope

Le repo couvre aujourd'hui :

- un `Iop` natif charge par Nuke
- un package Python `TVectorBlur` pour le bootstrap/menu
- un packaging multi-version Windows/Linux
- des comportements optionnels bases sur warp, mask et transformations d'espace

## Notes

- la doc artiste plus detaillee des parametres UI pourra etre enrichie quand le node sera stabilise
- la reference technique du build et du packaging est dans `BUILD.md` et `RELEASE.md`
