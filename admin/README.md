# Interface d’administration Palo (prototype)

Application en **HTML / CSS / JavaScript** pur (voir `cahiers des charges/README.md`).

## Lancer en développement

1. Démarrer le serveur Palo avec **CORS** activé pour l’origine de l’interface (sinon le navigateur bloque `fetch` vers un autre hôte/port). Le fichier **`palo.ini.sample`** du dépôt active par défaut `cross-origin *` ; sans cette ligne, l’admin servie sur un autre port (ex. 8080) échoue au login avec `ERR_FAILED` / « Failed to fetch ».

   **Via `palo.ini`** (dans le répertoire données, chargé par défaut sauf si Palo est lancé avec `-n` / `--load-init-file` pour désactiver l’init file) :

   ```
   cross-origin *
   ```

   Ou une origine précise :

   ```
   cross-origin "http://localhost:8080"
   ```

   Équivalent à l’option ligne de commande **`-g`** / **`--cross-origin`**.

   **Via la ligne de commande** :

   ```bash
   palo -g '*' -h 7777
   ```

   Ou une origine précise :

   ```bash
   palo -g 'http://localhost:8080' -h 7777
   ```

   **Attention** : `http://127.0.0.1:8080` et `http://localhost:8080` sont deux origines différentes ; la valeur doit correspondre à l’URL réelle de l’interface.

2. Ouvrir `index.html` **via un petit serveur HTTP local** (recommandé pour `fetch`), par exemple depuis ce dossier :

   ```bash
   python3 -m http.server 8080
   ```

   Puis ouvrir `http://127.0.0.1:8080/index.html`.

3. Indiquer l’URL du serveur Palo (ex. `http://127.0.0.1:7777`), identifiant et mot de passe. Le mot de passe est envoyé en **MD5 (hex)** comme l’API Palo (`/server/login`).

## Fichiers

| Fichier | Rôle |
|--------|------|
| `index.html` | Structure des colonnes (connexion → bases → dimensions/cubes → détail) |
| **Bootstrap 5** (CDN jsDelivr) | CSS + JS bundle dans `index.html` |
| `css/app.css` | Compléments (largeurs de colonnes, arbre d’éléments) |
| `js/palo-csv.js` | Parse des réponses `text/plain` au format Palo (champs `;`) |
| `js/api.js` | Client HTTP, session `sid`, en-têtes `X-PALO-*` |
| `js/app.js` | Comportement UI |
| `vendor/md5.js` | CryptoJS (MD5), copié depuis `Api/md5.js` |

## CORS / origine croisée

Sans **`-g`**, Palo n’envoie pas `Access-Control-Allow-Origin` : toute page sur une autre origine (ex. `localhost:8080` appelant `127.0.0.1:7777`) est bloquée par le navigateur. Le code serveur ajoute aussi `Access-Control-Allow-Headers` / `Expose-Headers` pour les en-têtes `X-PALO-*` lorsque CORS est activé.

## Intégration future au binaire Palo

Servir ce dossier comme les ressources `Api/` (handlers HTTP statiques + installation CMake) — à faire quand le flux de livraison le prévoit.
