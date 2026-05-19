# Windows packaging

This document describes the first Windows packaging plan. It is not a release
approval by itself; a Windows smoke test is still required before marking the
packaging task complete.

## Target

- Package formats: ZIP for portable builds, NSIS installer for interactive
  installation.
- Runtime deployment: `windeployqt` for Qt5 runtime files.
- Secret storage runtime: QtKeychain backed by Windows Credential Manager.
- SMB backend: by default CMake fetches the pinned libsmb2 source into
  `tmp/libsmb2-src` and builds it with the project.

## Build outline

Use a Windows build environment with Qt5, CMake, Ninja, QtKeychain, libsodium,
Git, and a C/C++ compiler available.

```powershell
cmake -S . -B tmp\package-windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF
cmake --build tmp\package-windows
ctest --test-dir tmp\package-windows --output-on-failure
cmake --build tmp\package-windows --target package
powershell -ExecutionPolicy Bypass -File scripts\package-smoke-windows.ps1
```

Run `windeployqt` against the installed or staged `smb-browser.exe` before
publishing the package. The package must include:

- Qt5 Core/Gui/Widgets/Sql/Svg runtime DLLs and platform/image plugins.
- QtKeychain runtime DLLs.
- libsodium runtime DLL.
- libsmb2 runtime DLL when the backend is enabled.
- Optional Samba `smbclient.exe` helper for DFS namespace resolution.
- SQLite Qt SQL driver.
- Russian translation file under an installed `i18n` or `share/smb-browser/i18n`
  directory.

## DFS namespace support

Direct SMB shares work through `libsmb2`. Corporate DFS namespace paths may need
the optional `smbclient.exe` helper because `libsmb2` can fail Tree Connect with
`STATUS_BAD_NETWORK_NAME` before the real target share is known.

The resolver looks for `smbclient.exe` in `PATH`, next to `smb-browser.exe`, and
under `bin` directories relative to the executable. If the helper is not
packaged, direct shares still work, but DFS namespace paths fall back to a
`ShareUnavailable` error with a DFS hint.

## Smoke test

Before marking Windows packaging complete:

1. Install on a clean supported Windows system.
2. Start the application from Start Menu and from `smb-browser.exe`.
3. Switch UI language to Russian.
4. Add/edit/delete a synthetic SMB connection.
5. Save a synthetic password and verify it persists after restart.
6. Connect to a test SMB server and list files.
7. If `smbclient.exe` is packaged, connect to a DFS namespace path and confirm
   it resolves to a target share.
8. Download, upload, rename, delete, and open a test file.
9. Confirm closing the main window exits the application process.
10. Confirm logs and SQLite database are created under Windows application data
   locations and do not contain secrets.
