#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_PATH="${1:-}"

if [ -z "$PACKAGE_PATH" ]; then
  PACKAGE_PATH="$(find "$ROOT_DIR/tmp" -path '*/packages/smb-browser_*_amd64.deb' \
    -type f -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -1 | cut -d' ' -f2-)"
fi

if [ -z "$PACKAGE_PATH" ] || [ ! -f "$PACKAGE_PATH" ]; then
  echo "Package not found. Build it first with:" >&2
  echo "  cmake --build tmp/package-linux --target package" >&2
  exit 1
fi

SMOKE_DIR="$ROOT_DIR/tmp/package-smoke-linux"
ROOTFS="$SMOKE_DIR/rootfs"
rm -rf "$SMOKE_DIR"
mkdir -p "$ROOTFS"

dpkg-deb -x "$PACKAGE_PATH" "$ROOTFS"

test -x "$ROOTFS/usr/bin/smb-browser"
test -f "$ROOTFS/usr/share/smb-browser/i18n/smb-browser_ru.qm"
test -f "$ROOTFS/usr/share/applications/smb-browser.desktop"
test -f "$ROOTFS/usr/share/metainfo/io.github.smb_browser.SmbBrowser.metainfo.xml"
test -f "$ROOTFS/usr/share/icons/hicolor/scalable/apps/smb-browser.svg"
test -f "$ROOTFS/usr/share/doc/smb-browser/LICENSE"
test -f "$ROOTFS/usr/share/doc/smb-browser/NOTICE"

if ! grep -q 'GPL-3.0-or-later' \
  "$ROOTFS/usr/share/metainfo/io.github.smb_browser.SmbBrowser.metainfo.xml"; then
  echo "AppStream metadata does not declare GPL-3.0-or-later." >&2
  exit 1
fi

if ! grep -q 'SPDX-License-Identifier: GPL-3.0-or-later' \
  "$ROOTFS/usr/share/doc/smb-browser/LICENSE"; then
  echo "Installed LICENSE does not declare GPL-3.0-or-later SPDX id." >&2
  exit 1
fi

if find "$ROOTFS" \( -name 'libsmb2.so*' -o -name 'smbclient' -o -name 'samba*' \) \
  -print -quit 2>/dev/null | grep -q .; then
  echo "Unexpected legacy SMB runtime found in package." >&2
  find "$ROOTFS" \( -name 'libsmb2.so*' -o -name 'smbclient' -o -name 'samba*' \) >&2
  exit 1
fi

if dpkg-deb -f "$PACKAGE_PATH" Depends | grep -Eiq 'libsmb2|smbclient|samba'; then
  echo "Package metadata contains legacy SMB runtime dependency:" >&2
  dpkg-deb -f "$PACKAGE_PATH" Depends >&2
  exit 1
fi

if LD_LIBRARY_PATH="$ROOTFS/usr/lib:$ROOTFS/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
  ldd "$ROOTFS/usr/bin/smb-browser" | grep -Eiq 'libsmb2|smbclient|samba'; then
  echo "Executable links a legacy SMB runtime dependency:" >&2
  LD_LIBRARY_PATH="$ROOTFS/usr/lib:$ROOTFS/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
    ldd "$ROOTFS/usr/bin/smb-browser" >&2
  exit 1
fi

if LD_LIBRARY_PATH="$ROOTFS/usr/lib:$ROOTFS/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
  ldd "$ROOTFS/usr/bin/smb-browser" | grep -q 'not found'; then
  LD_LIBRARY_PATH="$ROOTFS/usr/lib:$ROOTFS/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
    ldd "$ROOTFS/usr/bin/smb-browser" >&2
  exit 1
fi

if command -v readelf >/dev/null 2>&1; then
  if ! readelf -W -l "$ROOTFS/usr/bin/smb-browser" | grep -q 'GNU_RELRO'; then
    echo "Executable is missing GNU_RELRO." >&2
    exit 1
  fi
  if ! readelf -d "$ROOTFS/usr/bin/smb-browser" | grep -q 'BIND_NOW'; then
    echo "Executable is missing BIND_NOW." >&2
    exit 1
  fi
  if readelf -W -l "$ROOTFS/usr/bin/smb-browser" |
    awk '/GNU_STACK/ && $7 ~ /E/ { found=1 } END { exit found ? 0 : 1 }'; then
    echo "Executable stack is marked executable." >&2
    exit 1
  fi
fi

set +e
QT_QPA_PLATFORM=offscreen \
LD_LIBRARY_PATH="$ROOTFS/usr/lib:$ROOTFS/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
timeout 6 "$ROOTFS/usr/bin/smb-browser" --smoke-close-ms=1000
code=$?
set -e

if [ "$code" -ne 0 ]; then
  echo "Application failed to start and close cleanly from extracted package, exit code $code." >&2
  exit "$code"
fi

if [ -n "${SMB_BROWSER_SMOKE_SERVER:-}" ] && [ -n "${SMB_BROWSER_SMOKE_SHARE:-}" ]; then
  set +e
  QT_QPA_PLATFORM=offscreen \
  LD_LIBRARY_PATH="$ROOTFS/usr/lib:$ROOTFS/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
  timeout "${SMB_BROWSER_SMOKE_TIMEOUT_SECONDS:-15}" \
    "$ROOTFS/usr/bin/smb-browser" --smoke-smb-list
  code=$?
  set -e
  if [ "$code" -ne 0 ]; then
    echo "Packaged app failed SMB list smoke, exit code $code." >&2
    exit "$code"
  fi
fi

echo "Linux package smoke passed for $PACKAGE_PATH"
