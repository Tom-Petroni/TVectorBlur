# Publish Output

Ce dossier contient le package installeable de `TVectorBlur`, pret a etre copie
dans un environnement `.nuke`.

## Contenu

- `TVectorBlur/`: package Python + dossier `bin/`
- `init.py`: bootstrap qui enregistre le package dans Nuke

## Layout des binaires

Les binaires compiles sont attendus ici :

`TVectorBlur/bin/<nuke_version>/<os>/<arch>/`

Exemples :

- `TVectorBlur/bin/16.0/windows/x86_64/TVectorBlur.dll`
- `TVectorBlur/bin/16.0/linux/x86_64/libTVectorBlur.so`

## Notes

- `publish/` reste la source de verite du package distribue aux artistes
- les releases GitHub sont assemblees a partir de ce dossier
- les binaires peuvent etre resynchronises depuis la CI quand on le demande
