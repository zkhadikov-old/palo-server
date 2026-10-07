# Exploitation, risques et ameliorations

## Exploitation courante

- **Logs**: fichier dedie recommande (`/var/log/palo/palo.log`) et rotation via logrotate.
- **Data**: persistance sous `data-directory` (par defaut `./Data`, packaging: `/var/lib/palo`).
- **Arret propre**: le serveur gere les signaux et peut commit/sauvegarder a l'arret.
- **Ports**: au moins un endpoint `http` ou `admin` doit etre configure.

## Risques identifies

- **Exposition reseau**: defaults ou examples en `0.0.0.0` sans TLS obligatoire.
- **Configuration sensible**: cles TLS et passphrases potentiellement stockees en clair.
- **Dette build/tooling**:
  - CMake monolithique important,
  - generation optionnelle de sources (bison/flex/gperf) selon l'environnement.
- **Observabilite limitee**:
  - pas de healthcheck standard ni endpoints de readiness documentes ici.

## Recommandations prioritaires

1. **Securiser l'exposition**
   - imposer reverse proxy/TLS en frontal,
   - reduire bind reseau par defaut.
2. **Renforcer service systemd**
   - ajouter hardening (`NoNewPrivileges`, `ProtectSystem`, `ProtectHome`, etc.),
   - verifier ACL minimales sur data/log.
3. **Stabiliser packaging**
   - verrouiller dependances runtime selon Debian cible,
   - automatiser controle de metadata `.deb`.
4. **Documenter l'exploitation**
   - runbook incidents (demarrage, stop, corruption data, restore),
   - checklist de verification post-deploiement.

## Idee de roadmap technique

- **Court terme**: hardening systemd + doc ops + validation packaging.
- **Moyen terme**: CI build/test package et automatisation release.
- **Long terme**: decoupage du CMake principal et ajout de tests d'integration demarrage/API.
