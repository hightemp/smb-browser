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
  qttools5-dev-tools \
  qtkeychain-qt5-dev \
  libsodium-dev
```

Configure, build and test:

```bash
cmake -S . -B tmp/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF

cmake --build tmp/build --parallel
ctest --test-dir tmp/build --output-on-failure
```

By default CMake builds the SMB backend. If `libsmb2` is not available through
`pkg-config`, CMake clones the pinned upstream source into `tmp/libsmb2-src` and
builds it as part of the project.

Run the app:

```bash
tmp/build/src/app/smb-browser
```

## Build without SMB backend

Use this only for fast UI/core development when real SMB access is not needed:

```bash
cmake -S . -B tmp/build-no-smb -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSMB_BROWSER_WITH_LIBSMB2=OFF
cmake --build tmp/build-no-smb --parallel
ctest --test-dir tmp/build-no-smb --output-on-failure
```

## Manual libsmb2 prefix

The default build does not require this, but the helper script can build
`libsmb2` into `tmp/libsmb2-prefix` for experiments with a pkg-config based
setup:

```bash
scripts/build-libsmb2.sh
```

Then configure with `SMB_BROWSER_USE_SYSTEM_LIBSMB2=ON` and `PKG_CONFIG_PATH`
pointing to `tmp/libsmb2-prefix/lib/pkgconfig`.

## Linux package smoke

Build a Debian package and smoke-test it without installing into the system:

```bash
cmake -S . -B tmp/package-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF
cmake --build tmp/package-linux --target package --parallel
scripts/package-smoke-linux.sh
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
docker compose -f tests/integration/samba/docker-compose.yml up -d --build
cmake -S . -B tmp/build-samba -G Ninja \
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=ON
cmake --build tmp/build-samba --parallel
ctest --test-dir tmp/build-samba -L docker-samba --output-on-failure
docker compose -f tests/integration/samba/docker-compose.yml down -v
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
