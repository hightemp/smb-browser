# SMB Browser

Qt5/C++ desktop application for managing SMB connections and browsing
Windows/Samba shared folders inside the application's own Qt Widgets UI.

## Clean clone build on Ubuntu 22.04

Install build dependencies:

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  git \
  ninja-build \
  pkg-config \
  qtbase5-dev \
  libqt5svg5-dev \
  qttools5-dev-tools \
  qtkeychain-qt5-dev \
  libsodium-dev
```

Configure, build and test:

```bash
make configure
make build
make test
```

By default CMake builds the clean-room native SMB backend
(`SMB_BROWSER_WITH_NATIVE_SMB=ON`, `SMB_BROWSER_WITH_LIBSMB2=OFF`). The default
build does not require `libsmb2-dev`, a checked-out `libsmb2` source tree,
`smbclient`, or Samba CLI tools.

Current native backend status:

- SMB2 Direct TCP transport is built into the application.
- NTLMv2 password/domain auth, guest/anonymous auth handling, SPNEGO token
  wrapping, SMB2.0.2/2.1 HMAC-SHA256 signing and SMB3.0/3.0.2 AES-CMAC signing
  are implemented in the clean-room engine.
- Basic app operations are wired through `NativeSmbClient`: check, list,
  create folder, delete, rename, download, upload, copy and move.
- Advanced metadata APIs cover timestamps/attributes, EA list/set/remove and
  raw security descriptor query/set. POSIX chmod/chown are capability-gated
  until a POSIX extension contract is added.
- SMB3 encryption, native DFS referrals, Kerberos/current-user auth and share
  browsing are still tracked in `TASKS.md`; current-user auth is reported as
  unsupported by the native backend instead of falling through to password auth.

Run the app:

```bash
make run
```

## Build without SMB backend

Use this only for fast UI/core development when real SMB access is not needed:

```bash
make no-smb
```

## Legacy libsmb2 prefix

The default build does not require this. The helper script is kept only for
legacy experiments with the old libsmb2 backend:

```bash
scripts/build-libsmb2.sh
# or
make libsmb2
```

Then configure explicitly with `SMB_BROWSER_WITH_LIBSMB2=ON`,
`SMB_BROWSER_USE_SYSTEM_LIBSMB2=ON` and `PKG_CONFIG_PATH` pointing to
`tmp/libsmb2-prefix/lib/pkgconfig`.

## Linux package smoke

Build a Debian package and smoke-test it without installing into the system:

```bash
make smoke-linux
```

The package is written to `tmp/package-linux/packages`.
If `SMB_BROWSER_SMOKE_SERVER` and `SMB_BROWSER_SMOKE_SHARE` are set, the smoke
script also verifies that the packaged app can list the synthetic test share
through the built-in native backend.

Windows and macOS packaging plans are documented in `docs/windows-packaging.md`
and `docs/macos-packaging.md`. Their smoke scripts must be run on the matching
operating system:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1
```

```bash
scripts/package-macos.sh
```

## Optional Docker Samba integration test

The Docker Samba test profile is disabled by default.

```bash
make samba-up
make samba-test
make samba-down
```

`make samba-up` builds the fixture and waits for the container healthcheck
before returning.

The native SMB perf/stress profile is also opt-in:

```bash
make perf-test
```

## License

SMB Browser source code is licensed under `GPL-3.0-or-later`.

The native SMB engine is clean-room project source. Samba source code and
binaries are not copied, linked, vendored or distributed; local checkouts under
`tmp/` are development-only reference material.

## Project docs

- `PRD.md` - product requirements.
- `TASKS.md` - implementation backlog with completion checkboxes.
- `docs/current-user-kerberos.md` - current-user/Kerberos/SSO design.
- `docs/cross-platform-smoke.md` - package and real-server smoke profile.
- `docs/license-compliance.md` - license and release compliance notes.
- `docs/libsmb2-spike.md` - libsmb2 integration notes.
- `docs/native-smb-clean-room.md` - clean-room native SMB migration plan.
- `docs/native-smb-test-matrix.md` - native SMB capability test matrix.
- `docs/linux-packaging.md` - Linux packaging profile.
- `docs/windows-packaging.md` - Windows packaging plan.
- `docs/macos-packaging.md` - macOS packaging plan.
- `docs/secret-handling-policy.md` - secret handling rules.
- `docs/security-hardening.md` - release hardening and dependency audit.
- `docs/release-checklist.md` - release gate checklist.
