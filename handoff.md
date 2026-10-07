# Handoff — guillemets dans `name_path`

À faire dans ce dépôt (`palo-server`), puis compiler. Ne pas ajouter de paramètre d’URL `separator`. Ne pas modifier `StringUtils`.

## Problème

L’add-in Excel (`palo-excel-addin`, version **1.0.3.1**) envoie déjà les noms qui contiennent une virgule entre guillemets. Exemple réel, paramètre décodé :

```
name_path=01VD,TOUT,"Frais bureau, tél, port",2026,Cumulé Juin,Réalisé
```

Le serveur répond HTTP 400. `CMD_NAME_PATH` découpe sur chaque virgule et ignore les guillemets, donc il voit 8 segments au lieu de 6. `PaloJob::findPath` lève alors `ERROR_INVALID_COORDINATES` / `wrong number of path elements`.

Fichier : `Library/PaloDispatcher/PaloJob.cpp`, contrôle `jobRequest->pathName->size() != numDimensions`. Ne pas modifier ce contrôle.

## Correction

Brancher les chemins de noms de cellules sur les fonctions quote **déjà utilisées** par `name_elements` et `name_children`.

Fichier unique : `Library/PaloHttpServer/PaloHttpRequest.cpp`, dans `PaloHttpRequest::parse` (le `switch` des commandes, vers les lignes 612–646).

Remplacer :

```cpp
case PaloRequestHandler::CMD_NAME_PATH:
    fillVectorString(paloJobRequest->pathName, valueStart, valuePtr, ',');
    break;

case PaloRequestHandler::CMD_NAME_PATH_TO:
    fillVectorString(paloJobRequest->pathToName, valueStart, valuePtr, ',');
    break;
```

par :

```cpp
case PaloRequestHandler::CMD_NAME_PATH:
    fillVectorStringQuote(paloJobRequest->pathName, valueStart, valuePtr, ',');
    break;

case PaloRequestHandler::CMD_NAME_PATH_TO:
    fillVectorStringQuote(paloJobRequest->pathToName, valueStart, valuePtr, ',');
    break;
```

Et :

```cpp
case PaloRequestHandler::CMD_NAME_PATHS:
    fillVectorVectorString(paloJobRequest->pathsName, valueStart, valuePtr, ':', ',');
    break;
```

par :

```cpp
case PaloRequestHandler::CMD_NAME_PATHS:
    fillVectorVectorStringQuote(paloJobRequest->pathsName, valueStart, valuePtr, ':', ',');
    break;
```

Les signatures existent déjà (`PaloHttpRequest.h`). `pathName` et `pathToName` sont des `vector<string>*`. `pathsName` est un `vector<vector<string>>*`. L’ordre des séparateurs de `name_paths` reste `':'` entre chemins, `','` entre éléments, comme `CMD_NAME_CHILDREN`.

Ne pas changer `CMD_NAME_DIMENSIONS`, `CMD_NAME_AREA`, ni les chemins d’identifiants (`path`, `paths`).

## Comportement attendu

`fillVectorStringQuote` appelle `StringUtils::getNextElement(..., quote=true)` (`Library/Collections/StringUtils.cpp`, vers la ligne 640).

- Un segment qui **ne commence pas** par `"` est coupé à la virgule, comme aujourd’hui. `01VD,TOUT,2026` reste trois noms. Les URL de production sans guillemet ne changent pas.
- Un segment qui **commence** par `"` est lu jusqu’au `"` fermant suivi d’une virgule, ou jusqu’à la fin. Les guillemets entourants ne font pas partie du nom.
- `""` à l’intérieur du segment devient un seul `"`.

Pour l’URL ci-dessus, `pathName` doit contenir exactement :

1. `01VD`
2. `TOUT`
3. `Frais bureau, tél, port`
4. `2026`
5. `Cumulé Juin`
6. `Réalisé`

Le nom cherché dans la dimension est `Frais bureau, tél, port`, sans guillemets.

`name_paths` passe par `StringUtils::splitString2(..., quote=true)`, le même parseur que `name_children`. L’add-in actuel lit les cellules une par une via `name_path` ; `name_paths` est aligné pour le même cas, pas pour corriger l’appel qui échoue.

## Contrat déjà envoyé par l’add-in

Ne pas modifier l’add-in pour cette correction. Dans `palo-excel-addin`, `paloQuoteNamePathSegment` (`docs/assets/palo-api.js`) fait ceci :

- nom sans virgule : inchangé ;
- nom avec virgule : entouré de `"`, et chaque `"` interne doublé.

Exemple : `a"b,c` part comme `"a""b,c"`, et le serveur doit restituer `a"b,c`.

## À ne pas faire

- Pas de paramètre `separator=|` (ou autre). Un autre caractère casse dès qu’un nom le contient, et les clients existants continuent d’envoyer des virgules.
- Pas de nouveau parseur. `getNextElement` avec `quote=true` est déjà le comportement de `name_elements`.
- Ne pas guillemeter tous les noms côté serveur. Seul le client quote, et seulement s’il y a une virgule.

## Vérification

Après compilation et redémarrage du serveur, rejouer l’appel qui renvoie 400 :

```
/cell/value?sid=...&name_database=DWH&name_cube=PP_BUDGET&name_path=01VD,TOUT,"Frais bureau, tél, port",2026,Cumulé Juin,Réalisé
```

Attendu : plus de HTTP 400 « wrong number of path elements ». La valeur de la cellule, ou une erreur « element not found » si le nom n’existe pas dans la dimension. Une cellule sans virgule dans les noms doit garder la même réponse qu’avant.
