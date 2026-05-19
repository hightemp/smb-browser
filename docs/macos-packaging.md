# macOS packaging

This document describes the first macOS packaging plan. It is not a release
approval by itself; a macOS smoke test is still required before marking the
packaging task complete.

## Target

- Package format: `.dmg` generated through CPack DragNDrop.
- Application format: `smb-browser.app`.
- Runtime deployment: `macdeployqt` for Qt5 frameworks and plugins.
- Secret storage runtime: QtKeychain backed by macOS Keychain.
- SMB backend: by default CMake fetches the pinned libsmb2 source into
  `tmp/libsmb2-src` and builds it with the project.

## Build outline

Use a macOS build environment with Qt5, CMake, Ninja, QtKeychain, libsodium,
Git, and Xcode command line tools available.

```bash
cmake -S . -B tmp/package-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF
cmake --build tmp/package-macos
ctest --test-dir tmp/package-macos --output-on-failure
cmake --build tmp/package-macos --target package
scripts/package-smoke-macos.sh
```

Run `macdeployqt` against `smb-browser.app` before publishing the DMG. The app
bundle must include:

- Qt5 frameworks and platform plugins.
- QtKeychain runtime dependency.
- libsodium runtime dependency.
- libsmb2 runtime dependency when the backend is enabled.
- Optional Samba `smbclient` helper for DFS namespace resolution.
- Russian translation file under `Contents/Resources/i18n`.

`LocalizationManager` searches `../Resources/i18n` relative to the app bundle
executable, so packaged language switching can load the `.qm` file.

## DFS namespace support

Direct SMB shares work through `libsmb2`. Corporate DFS namespace paths may need
the optional `smbclient` helper because `libsmb2` can fail Tree Connect with
`STATUS_BAD_NETWORK_NAME` before the real target share is known.

The resolver looks for `smbclient` in `PATH`, next to the app executable, and
under relative `bin` paths including `Contents/Resources/bin/smbclient`. Finder
launches usually have a limited `PATH`, so a release DMG that claims DFS
namespace support should bundle the helper in the app resources. If the helper
is absent, direct shares still work, but DFS namespace paths fall back to a
`ShareUnavailable` error with a DFS hint.

## Smoke test

Before marking macOS packaging complete:

1. Open the app bundle from Finder and Terminal.
2. Confirm expected Keychain prompts when saving credentials.
3. Switch UI language to Russian.
4. Add/edit/delete a synthetic SMB connection.
5. Save a synthetic password and verify it persists after restart.
6. Connect to a test SMB server and list files.
7. If `smbclient` is packaged, connect to a DFS namespace path and confirm it
   resolves to a target share.
8. Download, upload, rename, delete, and open a test file through the system
   application.
9. Confirm tray/menu behavior.
10. Confirm logs and SQLite database are created under macOS application support
   locations and do not contain secrets.
