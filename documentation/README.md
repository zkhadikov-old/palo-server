# Documentation du projet `palo-server`

Cette documentation a ete creee a partir de l'analyse du code source et des scripts de build/packaging presents dans ce depot.

## Sommaire

- `documentation/architecture.md`: vue d'ensemble technique, flux d'initialisation, composants.
- `documentation/build-and-run.md`: compilation, execution locale, options CMake, validation.
- `documentation/configuration.md`: fonctionnement de `palo.ini`, options principales, **HTTPS/TLS**, exemples.
- `documentation/packaging.md`: packaging Debian (`.deb`), service systemd, deploiement sur VM.
- `documentation/operations-and-risks.md`: exploitation, observabilite, points de vigilance et ameliorations recommandees.

## Perimetre analyse

- Build system: `CMakeLists.txt`, modules CMake sous `Modules/CMake`.
- Entrees executable: `Programs/palo.cpp` et `Programs/palorun.cpp`.
- Parsing/configuration runtime: `Programs/PaloOptions.cpp` et `palo.ini.sample`.
- Packaging: `packaging/build-deb.sh`, `packaging/debian/*`, `packaging/systemd/palo.service`.

## Note importante

Le projet est historique (PALO 5.1) et plusieurs conventions sont anciennes (niveaux de dependances, patterns CMake, runtime tuning). Les documents ci-dessous distinguent explicitement:

- ce qui est **observe dans le code actuel**,
- et ce qui est **recommande** pour moderniser/simplifier l'exploitation.
