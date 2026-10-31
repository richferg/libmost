#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="$(tr -d '[:space:]' < "${ROOT_DIR}/VERSION")"
DIST_DIR="${ROOT_DIR}/dist"

if [[ -z "${VERSION}" ]]; then
  echo "ERROR: VERSION is empty"
  exit 1
fi

if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "ERROR: dpkg-deb not found"
  exit 1
fi

if command -v dpkg >/dev/null 2>&1; then
  ARCH="$(dpkg --print-architecture)"
else
  ARCH="$(uname -m)"
fi

PKG_NAME="libmost-runtime"
WORK_DIR="$(mktemp -d "${ROOT_DIR}/.pkgroot.XXXXXX")"
PKG_ROOT="${WORK_DIR}/${PKG_NAME}"
OUT_DEB="${ROOT_DIR}/${PKG_NAME}_${VERSION}_${ARCH}.deb"
KVER="$(uname -r)"

cleanup() {
  rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

echo "Building project artifacts..."
make -C "${ROOT_DIR}"
make -C "${ROOT_DIR}/driver"
mkdir -p "${DIST_DIR}"
OUT_DEB="${DIST_DIR}/${PKG_NAME}_${VERSION}_${ARCH}.deb"

echo "Staging package root at ${PKG_ROOT}"
install -d "${PKG_ROOT}/DEBIAN"
install -d "${PKG_ROOT}/lib/systemd/system"
install -d "${PKG_ROOT}/lib/modules/${KVER}/extra"
install -d "${PKG_ROOT}/usr/lib"
install -d "${PKG_ROOT}/usr/lib/libmost/overlays"
install -d "${PKG_ROOT}/usr/sbin"

install -m 0644 "${ROOT_DIR}/driver/src/os8104.ko" "${PKG_ROOT}/lib/modules/${KVER}/extra/"
install -m 0644 "${ROOT_DIR}/driver/overlay/os8104.dtbo" "${PKG_ROOT}/usr/lib/libmost/overlays/"
# I2S audio card "PiMost" on the OS8104 source port; enable with dtoverlay=pimost.
install -m 0644 "${ROOT_DIR}/driver/overlay/pimost.dtbo" "${PKG_ROOT}/usr/lib/libmost/overlays/"
install -m 0644 "${ROOT_DIR}/mostd/mostd.service" "${PKG_ROOT}/lib/systemd/system/"
install -m 0755 "${DIST_DIR}/libmost/libmost.so" "${PKG_ROOT}/usr/lib/"
install -m 0755 "${DIST_DIR}/mostd/bin/mostd" "${PKG_ROOT}/usr/sbin/"
# Bluetooth agent (tools/bt-agent): optional, enable with `systemctl enable mostd-bt-agent`.
install -m 0755 "${ROOT_DIR}/tools/bt-agent/bt_agent.py" "${PKG_ROOT}/usr/sbin/mostd-bt-agent"
install -m 0644 "${ROOT_DIR}/tools/bt-agent/mostd-bt-agent.service" "${PKG_ROOT}/lib/systemd/system/"
install -d "${PKG_ROOT}/etc/systemd/system/bluealsa-aplay.service.d"
install -m 0644 "${ROOT_DIR}/tools/bt-agent/bluealsa-aplay.service.d/pimost.conf" \
  "${PKG_ROOT}/etc/systemd/system/bluealsa-aplay.service.d/"
install -d "${PKG_ROOT}/etc/wireplumber/wireplumber.conf.d"
install -m 0644 "${ROOT_DIR}/tools/bt-agent/wireplumber-no-bluez.conf" \
  "${PKG_ROOT}/etc/wireplumber/wireplumber.conf.d/90-mostd-no-bluez.conf"

cat > "${PKG_ROOT}/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${VERSION}
Section: base
Priority: optional
Architecture: ${ARCH}
Maintainer: libmost packager <noreply@example.com>
Description: libmost runtime package (os8104.ko, os8104/pimost overlays, libmost.so, mostd, mostd-bt-agent)
Recommends: bluez, bluez-alsa-utils, libasound2-plugin-bluez, python3-dbus, python3-gi
EOF

cat > "${PKG_ROOT}/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ -d /boot/overlays ]; then
  install -m 0644 /usr/lib/libmost/overlays/os8104.dtbo /boot/overlays/os8104.dtbo || true
  install -m 0644 /usr/lib/libmost/overlays/pimost.dtbo /boot/overlays/pimost.dtbo || true
fi
if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
fi
depmod -a || true
ldconfig || true
EOF
chmod 0755 "${PKG_ROOT}/DEBIAN/postinst"

rm -f "${OUT_DEB}"
dpkg-deb --root-owner-group --build "${PKG_ROOT}" "${OUT_DEB}"

echo "Created package:"
echo "  ${OUT_DEB}"
