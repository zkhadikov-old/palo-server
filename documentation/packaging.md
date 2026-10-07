# Packaging et deploiement

Ce guide privilegie le deploiement **sur VM** via un package Debian `.deb` et un service `systemd`.

## Package Debian

Le packaging local est pilote par `packaging/build-deb.sh`.

Le script:

1. build le projet (si `build/usr/bin/palo` absent),
2. assemble un arbre de staging sous `build/pkg-root`,
3. copie binaire/config/service/logrotate/scripts Debian,
4. produit `build/palo-server_<version>_amd64.deb`.

### Fichiers de packaging

- controle Debian: `packaging/debian/control`
- scripts: `packaging/debian/postinst`, `packaging/debian/prerm`
- service: `packaging/systemd/palo.service`
- rotation logs: `packaging/logrotate/palo`

### Regle versionning .deb

Lors de toute reconstruction `.deb`, incrementer la revision Debian et garder en synchro:

- `packaging/debian/control` (`Version`)
- `packaging/build-deb.sh` (`VERSION`)

Verifier ensuite le metadata:

```bash
dpkg-deb -I build/palo-server_<version>_amd64.deb
```

### Construire le .deb

Depuis la racine du repo:

```bash
cd packaging
./build-deb.sh
```

Le fichier `.deb` est genere sous `build/`.

### Installer sur une VM Debian

Copier le `.deb` sur la VM puis:

```bash
sudo apt update
sudo apt install -y ./palo-server_<version>_amd64.deb
```

Alternative si vous avez deja lance `dpkg -i`:

```bash
sudo apt --fix-broken install
```

Pourquoi: `dpkg -i` installe uniquement le fichier local et echoue si les
dependances ne sont pas deja presentes; `apt install ./...deb` resout et
installe automatiquement les dependances.

### Diagnostic des dependances introuvables

Si `apt` ne trouve pas un package (`Candidate: (none)`), verifier la
disponibilite des dependances declarees:

```bash
apt-cache policy libtcmalloc-minimal4 libicu76 \
  libboost-thread1.83.0 libboost-system1.83.0 libboost-regex1.83.0
```

Si un ou plusieurs paquets sont absents, reconstruire le `.deb` dans un
environnement Debian 13 (machine, container ou chroot Debian 13) pour aligner
les ABI/runtime packages avec la cible.

Preparer l'utilisateur systeme si besoin (selon ce que fait `postinst`):

```bash
sudo useradd --system --home /var/lib/palo --shell /usr/sbin/nologin palo || true
sudo mkdir -p /var/lib/palo /var/log/palo /etc/palo
sudo chown -R palo:palo /var/lib/palo /var/log/palo
```

## Service systemd

Le service fourni lance:

```bash
/usr/bin/palo -h 0.0.0.0 7777 -d /var/lib/palo -i /etc/palo/palo.ini -o /var/log/palo/palo.log
```

Il tourne sous user/groupe `palo` avec `Restart=on-failure`.

Points d'attention:

- `-h 0.0.0.0` expose toutes interfaces;
- verifier l'existence des chemins (`/var/lib/palo`, `/var/log/palo`);
- ajuster hardening systemd (voir `operations-and-risks.md`).

### Activer et demarrer

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now palo.service
sudo systemctl status palo.service
```

### Journaux

Selon la configuration, les logs sont dans:

- fichier: `/var/log/palo/palo.log`
- ou journal systemd:

```bash
sudo journalctl -u palo.service -f
```
