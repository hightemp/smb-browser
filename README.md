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
  libsodium-dev \
  smbclient
```

Configure, build and test:

```bash
make configure
make build
make test
```

By default CMake builds the SMB backend. If `libsmb2` is not available through
`pkg-config`, CMake clones the pinned upstream source into `tmp/libsmb2-src` and
builds it as part of the project.

`smbclient` is used only as an optional DFS referral resolver. File browsing and
file operations still go through the `libsmb2` backend, but corporate DFS
namespaces such as `smb://domain/share` may need `smbclient` available on
`PATH` so the app can discover the real target server/share first.

Run the app:

```bash
make run
```

## Build without SMB backend

Use this only for fast UI/core development when real SMB access is not needed:

```bash
make no-smb
```

## Manual libsmb2 prefix

The default build does not require this, but the helper script can build
`libsmb2` into `tmp/libsmb2-prefix` for experiments with a pkg-config based
setup:

```bash
scripts/build-libsmb2.sh
# or
make libsmb2
```

Then configure with `SMB_BROWSER_USE_SYSTEM_LIBSMB2=ON` and `PKG_CONFIG_PATH`
pointing to `tmp/libsmb2-prefix/lib/pkgconfig`.

## Linux package smoke

Build a Debian package and smoke-test it without installing into the system:

```bash
make smoke-linux
```

The package is written to `tmp/package-linux/packages`.

Windows and macOS packaging plans are documented in `docs/windows-packaging.md`
and `docs/macos-packaging.md`. Their smoke scripts must be run on the matching
operating system:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-smoke-windows.ps1
```

```bash
scripts/package-smoke-macos.sh
```

## Optional Docker Samba integration test

The Docker Samba test profile is disabled by default.

```bash
make samba-up
make samba-test
make samba-down
```

## Project docs

- `PRD.md` - product requirements.
- `TASKS.md` - implementation backlog with completion checkboxes.
- `docs/libsmb2-spike.md` - libsmb2 integration notes.
- `docs/linux-packaging.md` - Linux packaging profile.
- `docs/windows-packaging.md` - Windows packaging plan.
- `docs/macos-packaging.md` - macOS packaging plan.
- `docs/secret-handling-policy.md` - secret handling rules.
- `docs/release-checklist.md` - release gate checklist.
