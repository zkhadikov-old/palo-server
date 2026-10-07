paloexport — export JSON (structure cube) via l’API HTTP Palo
=============================================================

Dépendances build : OpenSSL (déjà requis par le serveur), sockets POSIX.
Pas de libcurl.

  cmake -S . -B build -DENABLE_PALOEXPORT=ON -DENABLE_PALOIMPORT=ON
  cmake --build build --target paloexport
  cmake --build build --target paloimport   # import : JSON + nlohmann_json (FetchContent si absent du système)

  ./paloimport -f dump.json -u USER -p PASS -D base_cible           # plan dry-run sur stdout
  ./paloimport -f dump.json -u USER -p PASS -D base_cible --execute # import réel sur le serveur

  # JSON « données seules » (paloexport_version 7, objet cell_data, sans dimensions/cube dans le fichier) :
  # connexion pour valider les chemins vs le cube existant, puis --execute pour /cell/replace.
  ./paloimport -f cells_only.json -u USER -p PASS -D base_existante --execute

Exemple :

  ./paloexport -u admin -h 127.0.0.1 -P 7777 -p motdepasse -D dwh -C mon_cube > dump.json

  ./paloexport … --with-data > dump_avec_cellules.json   # paloexport_version 7 + objet cell_data

  ./paloexport … --with-data --data-block-size 5000        # taille max de bloc pour /cell/export

  ./paloexport -v …   # journal des requêtes HTTP sur stderr (mot de passe masqué dans l’URL)

Sortie : JSON sur stdout ; erreurs et verbose sur stderr.

**`--with-data`** : exporte en plus un objet **`cell_data`** (cube principal puis chaque cube d’attributs
référencé par les dimensions d’axe). Source : **`/cell/export`** (cellules de base non vides,
`skip_empty=1`, `base_only=1`, valeurs stockées sans règles). Chaque cellule : `path` (noms d’éléments
par dimension), `type` Palo (1 numérique, 2 texte), `exists`, `value`. Reprise par blocs tant que la
ligne de progression n’est pas `1000;1000`. Schéma **`paloexport_version` 7** ; sans `--with-data` le
fichier reste en version **6**. `paloimport` ignore pour l’instant `cell_data` (import structure seule).

Cahier des charges — export (paloexport) — évolutions possibles (todo)
------------------------------------------------------------------------
- **Base** : enrichir avec `/database/info` (`type`, `status`, compteurs utiles à la validation).
- **Cube** : champs additionnels de `/cube/info` (`number_cells`, `number_filled_cells`, cohérence du type cube).
- **Cubes d’attributs** : pour chaque `attributes_cube`, documenter la structure (ex. dimensions du cube
  en **noms** via `/cube/info`) pour vérification / import — partiellement couvert par `cell_data` avec `--with-data`.
- **Droits** : optionnellement, noms des `rights_cube` liés aux dimensions (`/database/dimensions`).
- **Dimensions** : métadonnées `/dimension/info` (niveaux max, etc.) pour contrôle de cohérence.
- **Import des cellules** : rejouer `cell_data` dans `paloimport` (hors périmètre actuel).

Cahier des charges — import de structure (paloimport ou équivalent)
--------------------------------------------------------------------
**Implémentation** : la cible CMake `paloimport` (`Programs/paloimport/`) suit ce document. Par défaut :
lecture / validation du JSON et **plan dry-run** sur stdout. Avec **`--execute`** : import réel via
l’API HTTP (création de la base si absente, dimensions d’axe, éléments des dimensions d’attributs,
cube, règles — voir l’ordre ci-dessous).

Le JSON **`paloexport_version` 6** (structure seule) ou **7** (avec `cell_data` si export `--with-data`)
est conçu pour être rejoué par noms (sans IDs Palo) côté structure. Les anciens fichiers avec
**`palodump_version`** restent acceptés par `paloimport` (le même numéro de schéma).

**Tableau `dimensions`** : d’abord les dimensions d’**axe** du cube (dans le même ordre que
`cube.dimensions_in_order`), puis les **dimensions d’attributs** référencées par ces axes (sans
doublon). Chaque entrée a le même schéma (`name`, `type`, `elements`, …). Le champ **`type`** est le
code Palo **dimension** tel que renvoyé par l’API : `0` = normale, `2` = dimension d’attributs (le
préfixe `#` éventuel dans le nom est une convention, pas le type). Une dimension normale avec
attributs inclut en plus **`attributes_dimension`** et **`attributes_cube`** (noms). Une dimension
d’attributs (`type` = `2`) inclut **`associated_normal_dimension`** (nom de la dimension normale
associée).

**Ordre global de l’import** (respecter cet enchaînement) :

1. **Dimensions** — Créer en base toutes les dimensions **normales** requises pour les axes du futur
   cube (noms alignés sur le JSON). Pour chacune, rejouer la structure d’**éléments** des dimensions
   d’axe (deux passes décrites plus bas). Les dimensions d’attributs de type `2` ne se créent pas à la
   main : elles existent déjà une fois les dimensions normales créées.

2. **Attributs** — Ensuite seulement, rejouer les **éléments** des dimensions d’**attributs**
   (entrées `type` = `2` du JSON, deux passes), une fois la dimension normale parente en place.

3. **Cube** — Enfin, **créer le cube** en rattachant les dimensions d’axe dans l’ordre
   `cube.dimensions_in_order`, puis appliquer les **règles** du cube sur ce cube existant.

**Création des dimensions d’attributs (type `2`)** : ne **pas** les créer explicitement à l’import
(`/dimension/create` ou équivalent). Palo les crée **automatiquement** avec toute dimension normale ;
les entrées `type` = `2` du JSON servent à documenter la structure et à **synchroniser les éléments**
(uniquement) une fois la dimension normale parente en place. Appliquer les **deux passes** ci-dessous
sur la liste `elements` de ces dimensions **sans** recréer la dimension elle-même.

Stratégie obligatoire en **deux passes** par dimension (ordre des passes libre entre dimensions,
mais les deux passes s’appliquent à chaque dimension concernée) :

1. **Première passe — créer tous les éléments**  
   Créer **chaque** entrée de la liste `elements`, **y compris** les éléments de type consolidé (4).  
   Pour les consolidés : les créer **sans enfants** (arbre vide sous ce nœud).  
   Ne pas se limiter aux seuls « éléments de base » : en cas de **consolidations de
   consolidations**, les nœuds intermédiaires doivent exister avant la seconde passe, sinon des
   références d’enfants peuvent pointer vers des éléments encore inexistants.

2. **Seconde passe — remplir les consolidations**  
   Pour chaque élément dont le type indique un consolidé et pour lequel `children` (et `weights`)
   est défini dans le JSON, appeler l’API appropriée (ex. `/element/replace` avec `name_children`
   et `weights`, ou `/element/append` selon le scénario) pour rattacher les enfants.

Les **deux passes** ci-dessous s’appliquent **à l’intérieur** de chaque étape « dimensions » ou
« attributs » ci-dessus (remplissage des listes `elements`). La base de données peut déjà exister
vide ou non selon le scénario d’import.
