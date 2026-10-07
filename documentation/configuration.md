# Configuration runtime (`palo.ini`)

Le fichier de reference est `palo.ini.sample`.

## Regle d'evaluation des options

Ordre applique par `PaloOptions`:

1. options CLI,
2. puis options du fichier init (si active).

Consequence: le fichier init peut ecraser certains choix CLI, sauf options explicitement reservees a la ligne de commande (ex: changement de repertoire/data-dir dans certaines conditions).

Les options de type "toggle" inversent un etat a chaque occurrence.

## Options reseau indispensables

Le serveur exige au moins une de ces familles:

- `http <address> <port>`
- `admin <address> <port>`

Exemple simple:

```ini
http "" 7777
```

## CORS (frontend JS / navigateur)

Une option `cross-origin` existe (voir `palo.ini.sample`). Quand elle est definie, le serveur ajoute des headers CORS aux reponses HTTP et repond aux requetes de preflight `OPTIONS`.

Exemple permissif (toutes origines):

```ini
cross-origin *
```

Exemple restrictif (une origine):

```ini
cross-origin https://mon-frontend.exemple
```

## Encryption / HTTPS

### Options dans `palo.ini` ou en ligne de commande

| Role | Fichier init | CLI (equivalent) |
|------|----------------|------------------|
| Activer TLS cote serveur | `encryption optional` ou `required` | `-X optional` / `-X required` |
| Port TLS pour chaque endpoint HTTP | `https <port>` | `-H <port>` (repeter pour chaque `http`) |
| Fichiers materiel TLS | `key-files <ca> <private> <dh>` | `-K <ca> -K <private> -K <dh>` |
| Mot de passe cle PEM (si chiffree) | `password <secret>` | `-p <secret>` |

Les chemins dans `key-files` sont **relatifs au repertoire des donnees** (`data-directory`), sauf si tu passes des chemins absolus.

Trois fichiers sont attendus (voir aussi `palo.ini.sample`, section SSL) :

1. **`<ca>`** — PEM de confiance pour verifier les clients ou la chaine ; en usage courant auto-signe, le meme fichier que le certificat serveur peut servir pour `<ca>` et `<private>`.
2. **`<private>`** — Certificat serveur **et** cle privee (souvent un seul PEM), ou cert + cle dans la convention que le code charge (`SSL_CTX_use_certificate_chain_file` puis cle).
3. **`<dh>`** — Parametres Diffie-Hellman (PEM), generes avec `openssl dhparam`.

### Regles de coherence

- **`encryption none`** : pas de TLS ; les ports et options `https` / `-H` sont **ignores** au demarrage.
- **`encryption optional`** : HTTP et HTTPS coexistent ; l’API peut etre utilisee en clair ou en TLS selon le port.
- **`encryption required`** : seul **`/server/info`** reste accessible en HTTP clair ; le reste doit passer par HTTPS. Des **ports HTTPS** doivent etre definis pour chaque ligne `http` / `admin` qui doit offrir du TLS.

- Pour **chaque** couple `http <adresse> <port>` (ou paire admin), si tu actives HTTPS, il faut **exactement un** `https <port_tls>` dans le **meme ordre** : le serveur associe la *i*-eme ligne HTTP a la *i*-eme valeur HTTPS (`Programs/PaloHttpInterface.cpp`). Meme logique en CLI : une option `-H` par `-h` (dans l’ordre).

### Build : HTTPS integre ou module

CMake : `ENABLE_HTTPS` vaut `Builtin`, `Module` ou est desactive.

- **`Builtin`** : le support TLS est lie dans l’executable `palo` ; aucun fichier `.palo.so` supplementaire.
- **`Module`** (defaut dans le depot) : le code TLS est dans `libhttps.palo.so`. Au demarrage, Palo charge les extensions depuis le repertoire `extensions` (`-E <dir>` / option `extensions` dans `palo.ini`, defaut relatif souvent `usr/lib`). Il doit exister **`libhttps.palo.so`** compatible (la revision du module doit correspondre a celle du serveur, voir les logs si le chargement echoue).

Apres compilation locale typique :

- binaire : `build/usr/bin/palo`
- module HTTPS : `build/usr/lib/libhttps.palo.so*`

Exemple : lancer depuis la racine du build pour que le chemin par defaut `usr/lib` resolve correctement, ou copier le `.so` vers le repertoire passe a `-E`.

### Exemple minimal (`palo.ini`)

Apres avoir genere `server.pem` et `dh.pem` dans le repertoire des donnees (ou ailleurs avec chemins absolus) :

```ini
encryption optional
http "" 7777
https 7778
key-files server.pem server.pem dh.pem
```

Puis demarrer avec ce fichier init. En HTTPS, les URLs sont du type `https://<hote>:7778/...`.

### Exemple ligne de commande (sans fichier, extraits)

```bash
./palo -d /var/lib/palo \
  -X optional \
  -h 0.0.0.0 7777 -H 7778 \
  -K /etc/palo/ca.pem -K /etc/palo/server.pem -K /etc/palo/dh.pem \
  -p mot-de-passe-cle-si-besoin
```

(Ordre des `-K` : CA, cle/cert serveur, DH — comme `key-files`.)

### Generer des fichiers de test (OpenSSL)

Exemples rapides (cles faibles historiques dans `palo.ini.sample` ; en production preferer RSA 2048+ et DH adequat) :

```bash
# Certificat auto-signe + cle (un seul PEM pour CA + serveur en test)
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout server.pem -out server.pem -subj "/CN=localhost"

# Parametres DH
openssl dhparam -out dh.pem 2048
```

Placer les fichiers la ou `key-files` les trouve (souvent sous `data-directory`).

### Verifier que HTTPS ecoute

```bash
curl -k https://127.0.0.1:7778/server/info
```

(`-k` desactive la verification du certificat cote client pour un auto-signe ; en production, utiliser un certificat valide ou une AC interne.)

### Alternative : reverse proxy

Pour un deploiement classique sur Debian, on expose souvent Palo en HTTP derriere **nginx** ou **haproxy** qui termine le TLS ; dans ce cas Palo peut rester en `encryption none` sur une interface locale et le proxy gere HTTPS vers les clients.

## Logging

- `log -` pour stdout (par defaut)
- `log <fichier>` pour fichier
- `verbose <error|warning|info|debug|trace>`

Exemple production:

```ini
log /var/log/palo/palo.log
verbose info
```

## Data et comportement serveur

Options utiles:

- `data-directory <dir>`
- `template-directory <dir>` (ressources API/doc en ligne)
- `auto-load`, `auto-commit`, `add-new-databases`
- `cache-barrier <n>`
- `session-timeout <s>`
- `maximum-return-cells <n>`
- `goalseek-limit <n>`
- `goalseek-timeout <ms>`

## Workers et extensions

- `worker <exec> <args...>`
- `workerlogin <information|authentication|authorization>`
- `use-cube-worker`
- `use-dimension-worker`
- `extensions <directory>`

Ces options activent des hooks externes pour authn/authz et traitements dedies.

## Securite minimum recommandee

- lier `http` a une interface privee ou reverse proxy;
- activer TLS (`encryption optional|required` + certificats valides);
- ecrire les logs hors stdout en production;
- limiter les droits systeme de l'utilisateur de service;
- proteger strictement `data-directory` et fichiers de cles.
