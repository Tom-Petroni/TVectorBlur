# Changelog

## 2026-07-19 -
2.0.5
- Added local NukeDockerBuild CUDA build wrappers for Windows/Linux release matrices.
- Published local Windows and Linux binaries for Nuke 13.0 through 17.0.

## 2026-07-16 -
2.0.4
- Fixed Linux GitHub Actions builds by installing the missing GLU development package on runners.
- Carries forward the TVectorBlur console log cleanup from 2.0.3.

## 2026-07-16 -
2.0.3
- Removed the TVectorBlur runtime performance logs from the Nuke console.
- Promoted TVectorBlur to stable release metadata.

## 2026-07-15 -
2.0.2
- Fixed the release detector to read VERSION from the repository root in GitHub Actions.

## 2026-07-15 -
2.0.1
- TVectorBlur aligned to the TCollection node repository standard.
- GitHub Actions build and runtime workflows normalized.
- Package loader validation added for release packaging.

## 2026-05-13 -
2.0.0
- Node migrated into TSuite monorepo structure (`work` + `publish`).
- Version tracking initialized.
