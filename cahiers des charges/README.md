# Cahier des charges — application d’administration Palo (JS)

Ce document sert de **base avant développement** : tu peux le compléter ou me dicter les sections (par message), puis on figera le périmètre et l’ordre des travaux.

---

## 1. Contexte et objectifs

- **Problème / besoin** :
- **Utilisateurs cibles** (admin, métier, dev) :
- **Objectifs mesurables** (ex. « CRUD bases sans ligne de commande ») :

---

## 2. Périmètre fonctionnel

### 2.1 À inclure (MVP)

- **Écran / fenêtre de login** : saisie **identifiant** et **mot de passe** ; **adresse du serveur Palo** (**IP ou nom d’hôte**, valeur par défaut : **`localhost`**) pour cibler l’API ; validation vers l’authentification Palo (session), puis accès à l’application.
- **Bases de données** (après authentification) : **liste** des bases présentes sur le serveur ; **création** d’une **nouvelle** base (flux et champs requis à détailler : nom, type, etc.).
- **Dimensions et éléments** (une fois une **base** choisie ou créée) : **création de dimensions** ; **peuplement** de chaque dimension avec des **éléments**. Les éléments sont **représentés en arbre** dans l’interface : un élément peut être un **groupe** (consolidé) regroupant d’autres éléments en **hiérarchie** parent/enfant — la navigation et l’édition reflètent cette structure (dépliage, filiation).
- **Cubes** : **création** et liste des cubes dans une base (dimensions d’axes à préciser selon API).
- **Règles** : pour un **cube** sélectionné, **création / édition** des règles (selon périmètre MVP).
- Droits / utilisateurs (si applicable) : …

### 2.2 Hors périmètre (pour plus tard)

- …

---

## 3. Contraintes techniques

- **Serveur Palo** : l’application est **servie par le même serveur Palo** que la documentation HTTP **API** (fichiers statiques déployés avec le binaire, chemins dédiés type `/api/...` — même principe pour l’app admin). L’utilisateur peut toutefois **indiquer l’IP ou le hostname** du serveur pour les appels API (défaut **`localhost`** ou origine courante si l’app est ouverte sur le même hôte). À préciser : **port** (ex. 7777), **protocole** (http/https), URL de base des appels. **CORS** : moins critique si l’app est servie **en same-origin** par Palo ; à traiter si accès depuis une autre origine : …
- **Authentification** : **login + mot de passe** saisis par l’utilisateur dans une interface dédiée ; appel session Palo (`/server/login` ou équivalent documenté), stockage sécurisé du token / session côté client (détail à préciser : cookie, `sessionStorage`, etc.).
- **Navigateurs cibles** : …
- **Phase développement** : les fichiers **JS** (et CSS éventuels) sont chargés via un **fichier HTML simple** ouvert **directement dans le navigateur** (workflow local) — **sans** reconstruire ni réinstaller le paquet **`.deb`** à chaque modification. Le serveur Palo cible reste celui saisi dans l’interface (ex. `localhost` avec Palo déjà lancé). Si des contraintes **CORS** ou **modules ES** empêchent l’ouverture en `file://`, recours possible à un **serveur HTTP minimal local** (ex. `python -m http.server`) — à valider selon les essais.

---

## 4. Stack JavaScript

- **Langage** : **JavaScript pur** (sans framework type React, Vue, Svelte, etc.) — modules ES ou scripts classiques selon choix d’implémentation.
- **Build** : **aucun bundler obligatoire** ; compilation/transpilation seulement si besoin de compatibilité navigateurs (à préciser).
- **CSS / UI** : **Bootstrap 5** (grille, formulaires, listes, boutons, utilitaires) — chargement typique via **CDN** en développement ; pour la **livraison** ou le **hors-ligne**, embarquer les fichiers **CSS/JS Bootstrap** en local avec l’app (dossier type `vendor/`). Une feuille **`app.css` minimal** peut compléter (colonnes type Finder, arbre d’éléments).
- **Icônes (optionnel)** : **Bootstrap Icons** (cohérent avec Bootstrap) ou **Tabler Icons** / **SVG inline** si l’on évite une dépendance supplémentaire.
- **Hébergement — développement** : **page HTML locale** + scripts associés, ouverte dans le navigateur pour itérer vite **sans** cycle paquet Debian à chaque changement (voir §3).
- **Hébergement — livraison / cible** : l’app est **servie par le serveur Palo**, sur le **même modèle** que le dossier **`Api/`** (ressources installées avec les données du serveur, routes HTTP enregistrées pour les fichiers HTML/JS/CSS). Pas d’hébergement séparé (nginx, etc.) **requis** pour le périmètre nominal une fois intégrée.

---

## 5. UX / écrans (liste souhaitée)

### 5.1 Navigation « colonnes » (type Finder macOS)

Principe : **panneaux verticaux** empilés **de gauche à droite**, comme le **navigateur de fichiers** d’Apple (Finder) : chaque niveau de contexte ouvre un **nouveau panneau à droite** du précédent, en conservant la **fil d’Ariane** visuel (colonnes précédentes restent visibles et permettent de revenir en sélectionnant un niveau).

| Ordre (gauche → droite) | Contenu du panneau |
|-------------------------|--------------------|
| 1 | **Connexion** : serveur Palo, identifiant, mot de passe, validation. |
| 2 | **Base de données** : liste des bases ; **création** d’une nouvelle base. |
| 3 | **Contenu de la base sélectionnée** : **cubes** et **dimensions** (liste + actions de **création**). |
| 4a (branche dimension) | Clic sur une **dimension** → panneau **éléments** : arbre hiérarchique (groupes / enfants), création et édition des éléments. |
| 4b (branche cube) | Clic sur un **cube** → panneau **règles** : création / édition des règles pour ce cube. |

- **Comportement** : ouverture successive des panneaux vers la **droite** ; retour possible en cliquant une colonne à gauche ou un item parent (détail d’implémentation : fermeture des panneaux suivants, ou conservation de l’historique — à trancher).
- **Arborescence des éléments** : conservée dans le panneau dimension (dépliage, structure groupe / sous-éléments).

### 5.2 Détail par étape (rappel)

- **Login** : champs **serveur Palo** (IP / hostname, défaut `localhost`), **login**, **mot de passe**, erreurs (refus, serveur injoignable).
- **Bases** : liste + **création** dans le 2ᵉ panneau.
- **Cubes / dimensions** : liste + **création** dans le 3ᵉ panneau après choix d’une base.
- **Éléments** : **vue arborescente** dans le panneau ouvert par clic sur une dimension ; parenté / groupe (interactions précises : glisser-déposer, sélecteur de parent, etc. — à préciser).
- **Règles** : panneau ouvert par clic sur un **cube**.

---

## 6. API Palo utilisée

- Endpoints ou familles d’API (`/database/...`, `/dimension/...`, `/element/...`, `/cube/...`, règles / `rule` selon doc) : à minima **bases**, **dimensions**, **éléments**, **cubes**, **règles** — alignés sur le parcours UX §5 — …
- Préférence identifiants numériques vs noms (`name_database`, etc.) : …

---

## 7. Critères de fin / recette

- **Livraison** : l’application est **accessible via le serveur Palo** (chargement HTML/JS/CSS comme pour la doc **API**), sans dépendre d’un autre serveur web pour le périmètre nominal une fois l’intégration packaging faite.
- **Développement** : possibilité de travailler depuis la **page HTML locale** (sans réinstaller le `.deb` à chaque itération) tout en validant les appels contre un Palo en cours d’exécution.
- Connexion réussie avec identifiants valides Palo ; échec explicite si identifiants invalides.
- Saisie du serveur : comportement correct avec défaut `localhost` et avec une autre IP / hostname saisi par l’utilisateur.
- Après login : affichage cohérent de la **liste des bases** ; **création** d’une base avec succès et apparition dans la liste (ou message d’erreur métier si échec).
- Dans une base : **création de dimension(s)** et de **cube(s)** ; **éléments** avec hiérarchie **visible en arbre** dans le panneau dimension ; **règles** éditables depuis le panneau cube.
- Navigation **colonnes** (§5) : enchaînement connexion → bases → cubes/dimensions → (dimension → éléments | cube → règles) **cohérent** et lisible.
- …

---

## 8. Notes libres

*(Dicte ici les priorités, interdits, ou références.)*

---

*Dernière mise à jour : Bootstrap 5 pour l’UI (voir §4).*
