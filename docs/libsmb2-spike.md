# libsmb2 Integration Spike

## Scope

Spike for `T-026`: verify whether upstream `libsmb2` can be used as the primary SMB backend candidate for this Qt5/C++ project on the current Ubuntu 22.04 development machine.

Temporary repository and build outputs are kept under local `tmp/`:

- `tmp/libsmb2-spike`
- `tmp/libsmb2-prefix`

These paths are local scratch space and are not part of the application source.

## Source

- Upstream repository: `https://github.com/sahlberg/libsmb2.git`
- Checked tag: `libsmb2-6.2`
- Checked commit from tag: `d67e213a5c4e7e4969fd81f0b95e4ca5831fbba1`

Visible stable tags during the spike included:

- `libsmb2-6.0`
- `libsmb2-6.1`
- `libsmb2-6.2`
- `v6.0.0`

Note: the checked `libsmb2-6.2` tag configures the CMake project as version `6.1.0` and installs `libsmb2.so.6.1.0`. Treat tag/version naming as a packaging detail to verify again before release.

## Build Result

The library configured, built, and installed successfully with CMake/Ninja:

```bash
git clone --depth 1 --branch libsmb2-6.2 \
  https://github.com/sahlberg/libsmb2.git \
  tmp/libsmb2-spike

cmake -S tmp/libsmb2-spike -B tmp/libsmb2-spike/build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=$PWD/tmp/libsmb2-prefix \
  -DENABLE_EXAMPLES=OFF

cmake --build tmp/libsmb2-spike/build --target install -j 4
```

Installed artifacts:

- Headers under `tmp/libsmb2-prefix/include/smb2`.
- Shared libraries:
  - `tmp/libsmb2-prefix/lib/libsmb2.so`
  - `tmp/libsmb2-prefix/lib/libsmb2.so.1`
  - `tmp/libsmb2-prefix/lib/libsmb2.so.6.1.0`
- pkg-config file: `tmp/libsmb2-prefix/lib/pkgconfig/libsmb2.pc`.
- CMake helper: `tmp/libsmb2-prefix/lib/cmake/libsmb2/FindSMB2.cmake`.

pkg-config check:

```bash
PKG_CONFIG_PATH=$PWD/tmp/libsmb2-prefix/lib/pkgconfig pkg-config --modversion libsmb2
PKG_CONFIG_PATH=$PWD/tmp/libsmb2-prefix/lib/pkgconfig pkg-config --cflags --libs libsmb2
```

Observed output:

```text
6.1.0
-I.../tmp/libsmb2-prefix/include -L.../tmp/libsmb2-prefix/lib -lsmb2
```

## CMake Integration Options

Recommended for this project:

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSMB2 REQUIRED IMPORTED_TARGET libsmb2)

target_link_libraries(target PRIVATE PkgConfig::LIBSMB2)
```

Developer environment:

```bash
export PKG_CONFIG_PATH="$PWD/tmp/libsmb2-prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$PWD/tmp/libsmb2-prefix/lib:$LD_LIBRARY_PATH"
```

Alternative:

```cmake
list(APPEND CMAKE_MODULE_PATH "/path/to/libsmb2-prefix/lib/cmake/libsmb2")
find_package(SMB2 REQUIRED)
```

The installed CMake file is a `FindSMB2.cmake` module, not a conventional `libsmb2Config.cmake`, so `pkg-config` is the cleaner integration path for now.

## License

The library and headers are LGPL 2.1 or later. The examples directory is BSD 2-Clause.

Implication for this project:

- Dynamic linking is preferred.
- Distribution must include the relevant LGPL notices and license text.
- Packaging must preserve the ability to replace/relink the LGPL library as required by the license.
- The examples license does not apply to linking the library itself.

## API Fit

The checked header `include/smb2/libsmb2.h` provides:

- Context lifecycle: `smb2_init_context`, `smb2_close_context`, `smb2_destroy_context`.
- Authentication setup: `smb2_set_user`, `smb2_set_password`, `smb2_set_domain`, `smb2_set_authentication`.
- Connection: `smb2_connect_share` and `smb2_connect_share_async`.
- Directory listing: `smb2_opendir`, `smb2_readdir`, `smb2_closedir`.
- File IO: `smb2_open`, `smb2_read`, `smb2_write`, `smb2_pread`, `smb2_pwrite`, `smb2_close`.
- File operations: `smb2_unlink`, `smb2_rmdir`, `smb2_mkdir`, `smb2_rename`, `smb2_stat`.
- Async variants for the important blocking operations.
- Event loop integration through `smb2_get_fd`, `smb2_which_events`, `smb2_service`, and fd event callbacks.

This is sufficient for the planned `SmbClient` backend:

- Check connection.
- List directory.
- Create folder.
- Delete file/folder.
- Rename.
- Upload/download.
- Copy/move through application-level stream copy when server-side copy is not available.

## Authentication Notes

The spike build did not find Kerberos/GSSAPI headers:

```text
Could NOT find LibKrb5
gssapi/gssapi.h - not found
krb5/krb5.h - not found
```

For the first implementation this is acceptable because `libsmb2` has built-in NTLMSSP username/password support.

Implications:

- Password authentication is the first supported production path.
- Guest/anonymous should be tested explicitly against Samba/Windows servers.
- Current-user/Kerberos behavior remains a separate spike and should not be promised for v1 until validated.

## Backend Design Decision

Use `libsmb2` behind `SmbClient`, not directly from UI or Qt models.

## Current Project Integration

The project now builds `libsmb2` by default when `SMB_BROWSER_WITH_LIBSMB2=ON`.
If a system/pkg-config installation is not explicitly requested, CMake uses
FetchContent to clone the pinned upstream source into:

- `tmp/libsmb2-src`

Current defaults:

- Repository: `https://github.com/sahlberg/libsmb2.git`
- Tag: `libsmb2-6.2`
- Examples: disabled.
- libkrb5/GSSAPI: disabled for reproducible username/password builds.

System/pkg-config mode remains available:

```bash
scripts/build-libsmb2.sh
PKG_CONFIG_PATH="$PWD/tmp/libsmb2-prefix/lib/pkgconfig:$PKG_CONFIG_PATH" \
cmake -S . -B tmp/build-system-libsmb2 -G Ninja \
  -DSMB_BROWSER_WITH_LIBSMB2=ON \
  -DSMB_BROWSER_USE_SYSTEM_LIBSMB2=ON
```

Initial `Libsmb2SmbClient` implementation should:

- Run in worker threads or through an operation queue, never on the UI thread.
- Prefer async libsmb2 APIs for cancellation/progress where practical.
- Start with sync APIs wrapped outside the UI thread if it materially reduces risk for the first backend version.
- Convert `errno`/NTSTATUS/libsmb2 error strings into `SmbErrorCode`.
- Never log SMB URLs containing credentials.
- Set credentials through `smb2_set_user`, `smb2_set_password`, and `smb2_set_domain`, not by embedding passwords into SMB URLs.

## DFS Namespace Finding

Corporate paths can be DFS namespaces rather than direct shares. One observed
case was `smb://v-tell.com/ru`: `smbclient` successfully followed the referral
to a concrete target, while `libsmb2` `smb2_connect_share()` failed at tree
connect with `STATUS_BAD_NETWORK_NAME`.

Project handling:

- `STATUS_BAD_NETWORK_NAME` during connection is mapped to `ShareUnavailable`,
  not `DnsError`.
- `DfsResolvingSmbClient` wraps the primary `libsmb2` client.
- `SmbclientDfsReferralResolver` optionally runs `smbclient -c showconnect`
  without putting passwords on the command line, parses the concrete
  `//server/share` target, and retries the same operation through `libsmb2`.
- Resolved targets are cached in memory per connection identity to avoid
  repeating the failed DFS-root tree connect on every folder navigation.
- If `smbclient` is absent, behavior falls back to the actionable
  `ShareUnavailable` error and hint.

## Open Follow-Ups

- Validate Windows and macOS build packaging for `libsmb2`.
- Add Docker Samba integration tests before implementing production write operations.
- Investigate Kerberos/current-user support separately.
- Investigate native DFS referral support in `libsmb2` or a maintained library
  alternative so the `smbclient` resolver can remain an optional compatibility
  path rather than a long-term dependency.
