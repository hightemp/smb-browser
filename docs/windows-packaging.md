# Windows packaging

This document describes the first Windows packaging plan. It is not a release
approval by itself; a Windows smoke test is still required before marking the
packaging task complete.

## Target

- Package format: portable ZIP produced by `scripts/package-windows.ps1`.
  NSIS remains a follow-up installer format after the portable smoke is green.
- Runtime deployment: `windeployqt` for Qt5 runtime files.
- Secret storage runtime: QtKeychain backed by Windows Credential Manager.
- SMB backend: built-in clean-room native SMB2/SMB3 engine.

## Build outline

Use a Windows build environment with Qt5, Qt tools (`lrelease`, `windeployqt`),
CMake, Ninja, pkgconf/pkg-config, QtKeychain, libsodium, Git, and a C/C++
compiler available. The default build must keep
`SMB_BROWSER_WITH_LIBSMB2=OFF` and `SMB_BROWSER_WITH_NATIVE_SMB=ON`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1
```

The helper configures the native backend, builds, runs `ctest`, requires
`windeployqt`, creates a clean portable staging directory, stages runtime DLL
candidates, creates a portable ZIP and runs `package-smoke-windows.ps1` against
the exact ZIP it just created.
The package must include:

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

The package smoke script verifies the packaged Qt runtime DLLs, platform plugin,
SQLite driver, QtKeychain runtime and libsodium runtime. It then starts
`smb-browser.exe --smoke-close-ms=1000` and fails if the process does not exit,
so close-without-tray behavior is checked without manual interaction.

If `SMB_BROWSER_SMOKE_SERVER` and `SMB_BROWSER_SMOKE_SHARE` are set, the script
also runs `smb-browser.exe --smoke-smb-list` and requires a successful directory
listing through the packaged native backend.

The manual GitHub Actions workflow `.github/workflows/package-smoke.yml` runs
the same script on a clean Windows runner and uploads the generated ZIP
artifact.

DFS namespace support is implemented by the native SMB engine and must not be
solved by bundling `smbclient.exe`.

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
