#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-$ROOT_DIR/tmp/sbom}"
OUT_FILE="$OUT_DIR/smb-browser-sbom.json"

mkdir -p "$OUT_DIR"

version="$(grep -m1 'project(SmbBrowser' -A2 "$ROOT_DIR/CMakeLists.txt" |
  awk '/VERSION/ {print $2; exit}')"
version="${version:-0.0.0}"

cat >"$OUT_FILE" <<EOF
{
  "name": "smb-browser",
  "version": "$version",
  "generatedBy": "scripts/generate-sbom.sh",
  "cleanRoomNativeSmb": true,
  "legacySmbRuntimeDependencies": {
    "libsmb2": false,
    "smbclient": false,
    "sambaClientTools": false
  },
  "runtimeDependencies": [
    {
      "name": "Qt5",
      "type": "framework",
      "usage": "Widgets UI, SQL, translations and platform plugins"
    },
    {
      "name": "QtKeychain",
      "type": "library",
      "usage": "Primary system credential storage"
    },
    {
      "name": "SQLite",
      "type": "library/plugin",
      "usage": "Connection metadata storage through Qt SQL"
    },
    {
      "name": "libsodium",
      "type": "library",
      "usage": "Encrypted local vault fallback"
    },
    {
      "name": "OS networking APIs",
      "type": "system",
      "usage": "TCP/DNS for native SMB transport"
    }
  ],
  "internalComponents": [
    {
      "name": "smb_browser_native_smb",
      "type": "static-library",
      "origin": "clean-room project source"
    }
  ]
}
EOF

echo "$OUT_FILE"
