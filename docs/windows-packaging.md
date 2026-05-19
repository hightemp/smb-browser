# Windows packaging

This document describes the first Windows packaging plan. It is not a release
approval by itself; a Windows smoke test is still required before marking the
packaging task complete.

## Target

- Package formats: ZIP for portable builds, NSIS installer for interactive
  installation.
- Runtime deployment: `windeployqt` for Qt5 runtime files.
- Secret storage runtime: QtKeychain backed by Windows Credential Manager.
- SMB backend: built-in clean-room native SMB2/SMB3 engine.

## Build outline

Use a Windows build environment with Qt5, CMake, Ninja, QtKeychain, libsodium,
Git, and a C/C++ compiler available. The default build must keep
`SMB_BROWSER_WITH_LIBSMB2=OFF` and `SMB_BROWSER_WITH_NATIVE_SMB=ON`.

```powershell
cmake -S . -B tmp\package-windows -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DSMB_BROWSER_WITH_LIBSMB2=OFF `
  -DSMB_BROWSER_WITH_NATIVE_SMB=ON `
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
- SQLite Qt SQL driver.
- Russian translation file under an installed `i18n` or `share/smb-browser/i18n`
  directory.

## Dependency audit

The portable package must not contain `libsmb2.dll`, `smbclient.exe` or Samba
client binaries. The smoke script should inspect executable dependencies with
`dumpbin`, `llvm-objdump` or an equivalent scanner and fail the package if a
legacy SMB runtime dependency is present.

DFS namespace support must be implemented by the native SMB engine. Until the
native DFS task is complete, DFS limitations should be documented as product
limitations rather than solved by bundling `smbclient.exe`.

## Smoke test

Before marking Windows packaging complete:

1. Install on a clean supported Windows system.
2. Start the application from Start Menu and from `smb-browser.exe`.
3. Switch UI language to Russian.
4. Add/edit/delete a synthetic SMB connection.
5. Save a synthetic password and verify it persists after restart.
6. Connect to a test SMB server and list files through the native backend.
7. Confirm dependency audit finds no `libsmb2.dll`, `smbclient.exe` or Samba
   client runtime dependency.
8. Download, upload, rename, delete, and open a test file.
9. Confirm closing the main window exits the application process.
10. Confirm logs and SQLite database are created under Windows application data
   locations and do not contain secrets.
