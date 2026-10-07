# Architecture technique

## Vue d'ensemble

Le binaire principal est `palo` (cible definie dans `CMakeLists.txt`). Il agrege une grande partie du code sous `Library/*` et les sources sous `Programs/*`.

Les composants majeurs observes:

- `Programs/palo.cpp`: point d'entree Linux/Windows, gestion signaux, sequence de startup/shutdown.
- `Programs/PaloOptions.cpp`: parsing CLI + fichier init (`palo.ini`), verification des options, injection des reglages globaux.
- `Library/Olap/*`: coeur serveur OLAP (creation/chargement/commit).
- `Library/PaloHttpServer`, `Library/HttpServer`, `Library/Network/*`: interfaces reseau et traitement HTTP.
- `Library/Worker/*`: workers externes pour login/dimensions/cubes.
- `Library/HttpsServer/*`: support HTTPS (builtin ou module `libhttps.palo.so`).

## Flux de demarrage (simplifie)

1. **Initialisation process**
   - setup signaux (SIGINT/SIGTERM/SIGSEGV, etc.).
   - niveau de log initial.
2. **Parsing options**
   - options CLI parsees via `PaloOptions`.
   - puis lecture optionnelle de `palo.ini`.
3. **Validation**
   - coherence couples `http host/port`, `admin host/port`, mapping HTTPS.
   - verifications encryption (`none/optional/required`) et bornes numeriques.
4. **Preparation execution**
   - lock de repertoire data (`DirLock`), fichier log, niveau verbose.
5. **Creation/chargement serveur**
   - ouverture modules externes (extensions), eventuelle creation serveur externe.
   - creation serveur par defaut, chargement des DB depuis disque.
   - application des settings globaux (`updateGlobals`).
6. **Exposition interfaces**
   - creation interface HTTP multi-thread (`MTPaloHttpInterface`).
   - ajout des servers/ports, run loop bloquante (`iface->run()`).
7. **Arret propre**
   - arret workers/timers, commit/sauvegarde optionnels, destruction contexte.

## Modules et extension

Le projet supporte des modules externes charges depuis `extensionsDirectory`:

- interface HTTP externe,
- interface HTTPS externe,
- job analyser externe,
- creation serveur externe.

Ce mecanisme est central pour le decouplage (fonctionnalites optionnelles, patching, variantes de comportement) sans modifier le coeur.

## Build et composition binaire

Dans `CMakeLists.txt`:

- sources collectees recursivement sur de nombreux sous-dossiers `Library/*` + `Programs`.
- `Programs/palorun.cpp` est explicitement exclu de la cible `palo`.
- cible optionnelle `https.palo` en module partage si `ENABLE_HTTPS=Module`.
- sortie par defaut:
  - binaire: `build/usr/bin/palo`
  - libs: `build/usr/lib*/`

## Particularites multi-plateforme

- Linux: `main` natif dans `Programs/palo.cpp`.
- Windows: `PaloMain` exporte et wrapper `Programs/palorun.cpp` charge `paloserver.dll`.

## Dossiers principaux

- `Programs/`: entrees et orchestration runtime.
- `Library/`: logique metier, moteur OLAP, reseau, parser, scheduler, etc.
- `Api/`: ressources d'API/documentation en ligne deployees dans data dir.
- `Config/`: templates de headers config/version generes par CMake.
- `packaging/`: scripts Debian, service systemd, logrotate.
