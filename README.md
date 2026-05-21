# SMB Browser

[![CI](https://github.com/hightemp/smb-browser/actions/workflows/ci.yml/badge.svg)](https://github.com/hightemp/smb-browser/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/hightemp/smb-browser)](https://github.com/hightemp/smb-browser/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/hightemp/smb-browser/total)](https://github.com/hightemp/smb-browser/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Qt](https://img.shields.io/badge/Qt-5-41CD52?logo=qt&logoColor=white)](https://www.qt.io/)
[![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS-lightgrey)](https://github.com/hightemp/smb-browser/releases)
[![SMB](https://img.shields.io/badge/SMB-2%2F3-orange)](docs/native-smb-clean-room.md)

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
- SMB2.0.2/2.1/3.0/3.0.2/3.1.1 dialect negotiation is implemented without
  SMB1/NetBIOS fallback. SMB 3.1.1 NEGOTIATE includes preauth integrity and
  AES-128-CCM encryption capability contexts.
- NTLMv2 password/domain auth, guest/anonymous auth handling, SPNEGO token
  wrapping, SMB2.0.2/2.1 HMAC-SHA256 signing, SMB3.0/3.0.2 AES-CMAC signing
  and SMB3.1.1 preauth-derived AES-CMAC signing are implemented in the
  clean-room engine.
- Basic app operations are wired through `NativeSmbClient`: check, list,
  create folder, delete, rename, download, upload, copy and move.
- Share browsing is implemented through native `IPC$` + DCE/RPC/SRVSVC
  `NetrShareEnum` without SMB1, NetBIOS, `smbclient` or `libsmb2`.
- Advanced metadata APIs cover timestamps/attributes, EA list/set/remove and
  raw security descriptor query/set. POSIX chmod/chown are capability-gated
  until a POSIX extension contract is added.
- Native DFS referral resolution uses `FSCTL_DFS_GET_REFERRALS`, caches
  referrals with TTL and supports nested namespace rebase plus multiple target
  failover. Real Windows DFS namespace validation remains tracked in
  `TASKS.md`.
- SMB3 AES-128-CCM transform encryption is implemented for SMB 3.0/3.0.2,
  including encrypted-share retry and Docker Samba encrypted-share smoke.
  Kerberos/current-user auth is still tracked in `TASKS.md`; current-user auth
  is reported as unsupported by the native backend instead of falling through to
  password auth.

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
operating system. A manual GitHub Actions workflow is also available as
`Package Smoke` in `.github/workflows/package-smoke.yml`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1
```

```bash
scripts/package-macos.sh
```

## Release

Release version is stored in `VERSION`. To prepare and publish a release from
the current branch:

```bash
make release
```

The command synchronizes versioned files, commits version changes when needed,
creates or replaces tag `v<VERSION>`, then force-pushes the current branch and
tag. The tag push triggers `.github/workflows/release.yml`, which builds Linux,
Windows and macOS packages and publishes a GitHub Release.

For a local validation without pushing:

```bash
RELEASE_DRY_RUN=1 make release
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
