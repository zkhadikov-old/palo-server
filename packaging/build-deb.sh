#!/usr/bin/env bash
set -euo pipefail

# Simple local .deb builder for Palo
#
# Usage:
#   cd packaging
#   ./build-deb.sh
#
# The script will:
#   - build Palo in ../build if needed
#   - assemble a Debian package tree under ../build/pkg-root
#   - create a .deb file in ../build/

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
STAGING_DIR="${BUILD_DIR}/pkg-root"
VERSION="5.1.4-12"
PKG_NAME="palo-server_${VERSION}_amd64.deb"

echo "Root directory : ${ROOT_DIR}"
echo "Build directory: ${BUILD_DIR}"
echo "Staging dir   : ${STAGING_DIR}"

mkdir -p "${BUILD_DIR}"

echo "Configuring CMake (Release, paloexport inclus dans le paquet)..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DPROJECT_BUILD_TYPE=Release -DENABLE_PALOEXPORT=ON

if [ ! -x "${BUILD_DIR}/usr/bin/palo" ]; then
  echo "Building Palo (full)..."
  cmake --build "${BUILD_DIR}" -- -j"$(nproc)"
else
  echo "Palo binary already present; incremental build."
fi
# Toujours construire paloexport pour le .deb
cmake --build "${BUILD_DIR}" --target paloexport -- -j"$(nproc)"

echo "Preparing staging tree..."
rm -rf "${STAGING_DIR}"
mkdir -p \
  "${STAGING_DIR}/usr/bin" \
  "${STAGING_DIR}/usr/lib" \
  "${STAGING_DIR}/etc/palo" \
  "${STAGING_DIR}/var/lib/palo" \
  "${STAGING_DIR}/var/log/palo" \
  "${STAGING_DIR}/lib/systemd/system" \
  "${STAGING_DIR}/etc/logrotate.d" \
  "${STAGING_DIR}/DEBIAN"

# Install binaries
cp "${BUILD_DIR}/usr/bin/palo" "${STAGING_DIR}/usr/bin/palo"
if [ -x "${BUILD_DIR}/usr/bin/paloexport" ]; then
  cp "${BUILD_DIR}/usr/bin/paloexport" "${STAGING_DIR}/usr/bin/paloexport"
else
  echo "WARNING: ${BUILD_DIR}/usr/bin/paloexport manquant — paquet sans paloexport (vérifiez ENABLE_PALOEXPORT et la compilation)." >&2
fi

# Install https module library if present
if [ -f "${BUILD_DIR}/usr/lib/libhttps.palo.so" ]; then
  cp "${BUILD_DIR}/usr/lib/libhttps.palo.so" "${STAGING_DIR}/usr/lib/libhttps.palo.so"
fi

# Install default configuration
cp "${ROOT_DIR}/palo.ini.sample" "${STAGING_DIR}/etc/palo/palo.ini"

# Install Api directory into the data directory so that templates are available
if [ -d "${ROOT_DIR}/Api" ]; then
  cp -R "${ROOT_DIR}/Api" "${STAGING_DIR}/var/lib/palo/Api"
fi

# Install systemd unit
cp "${ROOT_DIR}/packaging/systemd/palo.service" "${STAGING_DIR}/lib/systemd/system/palo.service"

# Install logrotate configuration
cp "${ROOT_DIR}/packaging/logrotate/palo" "${STAGING_DIR}/etc/logrotate.d/palo"

# Install Debian metadata scripts
cp "${ROOT_DIR}/packaging/debian/control" "${STAGING_DIR}/DEBIAN/control"
cp "${ROOT_DIR}/packaging/debian/postinst" "${STAGING_DIR}/DEBIAN/postinst"
cp "${ROOT_DIR}/packaging/debian/prerm" "${STAGING_DIR}/DEBIAN/prerm"
chmod 755 "${STAGING_DIR}/DEBIAN/postinst" "${STAGING_DIR}/DEBIAN/prerm"

echo "Building .deb package..."
# --root-owner-group: fichiers en root:root dans l'archive (evite avertissements en build non-root)
dpkg-deb --build --root-owner-group "${STAGING_DIR}" "${BUILD_DIR}/${PKG_NAME}"

echo
echo "Package created: ${BUILD_DIR}/${PKG_NAME}"
echo "You can install it with:"
echo "  sudo dpkg -i ${BUILD_DIR}/${PKG_NAME}"

