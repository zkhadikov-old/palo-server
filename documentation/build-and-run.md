# Build et execution

## Prerequis Debian 13

D'apres `README`, `CMakeLists.txt` et les dependances linkees:

- Toolchain: `build-essential`, `cmake`, `pkg-config`
- Libs: Boost (`thread/system/regex`), OpenSSL 3 (minimum requis par CMake), ICU, zlib, google-perftools
- Optionnels: `bison`, `flex`, `gperf`, `doxygen`

Exemple installation:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake pkg-config \
  libboost-dev libboost-system-dev libboost-thread-dev libboost-regex-dev \
  libssl-dev libicu-dev zlib1g-dev \
  google-perftools libgoogle-perftools-dev \
  bison flex gperf doxygen
```

## Build standard

Depuis la racine du repo:

```bash
cmake -S . -B build -G "Unix Makefiles" -DPROJECT_BUILD_TYPE=Release
cmake --build build -- -j"$(nproc)"
```

Sorties attendues:

- binaire principal: `build/usr/bin/palo`
- module HTTPS (si active en mode module): `build/usr/lib/libhttps.palo.so*` — chargement et configuration TLS : voir `documentation/configuration.md` (section HTTPS).

## Options CMake importantes

- `PROJECT_BUILD_TYPE={Release|Debug|RelWithDebInfo}`
- `ENABLE_HTTPS={Module|Builtin}` (sinon OFF si valeur invalide)
- `ENABLE_TRACE_OPTION={ON|OFF}`
- `ENABLE_PROFILE={ON|OFF}` (flags `-pg`)
- `ENABLE_TIME_PROFILER={ON|OFF}`
- `ENABLE_USE_BISON={ON|OFF}`
- `ENABLE_USE_FLEX={ON|OFF}`
- `ENABLE_USE_GPERF={ON|OFF}`
- `ENABLE_32BIT={ON|OFF}`

## Execution locale

Exemple minimal:

```bash
./build/usr/bin/palo -h 127.0.0.1 7777 -d /tmp/palo-data
```

ou en utilisant un fichier init:

```bash
./build/usr/bin/palo -h 0.0.0.0 7777 -d /var/lib/palo -i /etc/palo/palo.ini -o /var/log/palo/palo.log
```

## Validation rapide

1. Verifier que le process ecoute sur le port attendu.
2. Verifier la creation/lecture des fichiers sous `data-directory`.
3. Verifier la sortie log (`error|warning|info|debug|trace`).
4. Tester l'endpoint d'info serveur via HTTP.

## Doxygen

Si `doxygen` est disponible pendant la configuration CMake, la cible `doc` est creee:

```bash
cmake --build build --target doc
```
