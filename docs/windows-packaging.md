# Windows packaging

This document describes the first Windows packaging plan. It is not a release
approval by itself; a Windows smoke test is still required before marking the
packaging task complete.

## Target

- Package formats: ZIP for portable builds, NSIS installer for interactive
  installation.
- Runtime deployment: `windeployqt` for Qt5 runtime files.
- Secret storage runtime: QtKeychain backed by Windows Credential Manager.
- SMB backend: libsmb2 built from source or provided by the chosen dependency
  manager.

## Build outline

Use a Windows build environment with Qt5, CMake, Ninja, QtKeychain, libsodium,
and libsmb2 available.

```powershell
cmake -S . -B tmp\package-windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DSMB_BROWSER_WITH_LIBSMB2=ON `
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF
cmake --build tmp\package-windows
ctest --test-dir tmp\package-windows --output-on-failure
cmake --build tmp\package-windows --target package
```

Run `windeployqt` against the installed or staged `smb-browser.exe` before
publishing the package. The package must include:

- Qt5 Core/Gui/Widgets/Sql runtime DLLs and platform plugins.
- QtKeychain runtime DLLs.
- libsodium runtime DLL.
- libsmb2 runtime DLL when the backend is enabled.
- SQLite Qt SQL driver.
- Russian translation file under an installed `i18n` or `share/smb-browser/i18n`
  directory.

## Smoke test

Before marking Windows packaging complete:

1. Install on a clean supported Windows system.
2. Start the application from Start Menu and from `smb-browser.exe`.
3. Switch UI language to Russian.
4. Add/edit/delete a synthetic SMB connection.
5. Save a synthetic password and verify it persists after restart.
6. Connect to a test SMB server and list files.
7. Download, upload, rename, delete, and open a test file.
8. Confirm tray show/exit behavior.
9. Confirm logs and SQLite database are created under Windows application data
   locations and do not contain secrets.
