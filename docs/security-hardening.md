# Security hardening and dependency audit

This document defines the release hardening baseline for SMB Browser after the
switch to the built-in clean-room native SMB engine.

## Build hardening

`SMB_BROWSER_ENABLE_HARDENING` is enabled by default. Release builds apply the
following flags where the toolchain supports them:

- GCC/Clang: `-fstack-protector-strong` and `_FORTIFY_SOURCE=2`.
- Linux linker: `-Wl,-z,relro` and `-Wl,-z,now`.
- MSVC: `/guard:cf` for compile and link steps.

Debug builds remain optimized for local diagnostics and do not force release
hardening flags.

## Linux release audit

`scripts/package-smoke-linux.sh` checks the generated package for:

- no bundled `libsmb2`, `smbclient` or Samba client helper binaries;
- no Debian runtime dependency on `libsmb2`, `smbclient` or Samba client tools;
- no executable linkage to legacy SMB runtime dependencies;
- all dynamic libraries resolved;
- `GNU_RELRO` present;
- `BIND_NOW` present;
- non-executable stack.

## Windows and macOS release audit

`scripts/package-smoke-windows.ps1` rejects `libsmb2.dll`, `smbclient.exe` and
Samba client DLLs in the package. If `dumpbin.exe` or `llvm-objdump.exe` is
available, it also scans executable dependencies for legacy SMB runtime names.

`scripts/package-smoke-macos.sh` rejects `libsmb2`, `smbclient` and Samba client
helper binaries in the app bundle, and scans `otool -L` output for legacy SMB
runtime dependencies.

## SBOM / dependency manifest

Run this before publishing a release:

```bash
make sbom
```

The command writes `tmp/sbom/smb-browser-sbom.json`. The manifest records the
native SMB engine as clean-room project source and lists the runtime dependency
families that must be reviewed for release: Qt5, QtKeychain, SQLite, libsodium
and OS networking APIs. The manifest declares the project license as
`GPL-3.0-or-later`.

The generated file is a release artifact, not committed source.

## Third-party source policy

The native SMB engine must build from this repository without downloading Samba,
`libsmb2` or other SMB client source. If any third-party source is added later,
the release owner must add:

- pinned version or commit;
- checksum or signature verification;
- license notice;
- update/advisory owner;
- package smoke coverage proving it is not a hidden `libsmb2`/`smbclient`
  runtime replacement.

## Advisory tracking

Before release, review security advisories for:

- Qt5 and Qt platform plugins;
- QtKeychain;
- SQLite / Qt SQL driver packaging;
- libsodium;
- OS credential/keychain APIs;
- compiler/runtime libraries used by the target package;
- native SMB changes in this repository.

Samba advisories are monitored as protocol compatibility/security background
only. Samba source and binaries are not copied, linked or distributed by the
clean-room native backend path.

## Secret diagnostics rule

Crash reports, logs, package smoke output, SBOM output and dependency audit
logs must not include passwords, credential-bearing URIs, master passwords or
raw credential blobs. New native SMB error paths must pass through the existing
sanitization layer before they reach user-visible logs or diagnostics.

The default test suite includes `security_regression`, which verifies that
synthetic secrets do not appear in default export/import metadata, SQLite error
metadata, transfer operation names or native SMB diagnostic errors. It also
keeps the dangerous plain-text password export flow explicit and confirmed.
