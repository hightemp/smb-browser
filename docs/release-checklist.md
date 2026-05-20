# Release checklist for v1

This checklist is the release gate for the first usable desktop build.

## Scope gate

- [ ] PRD first-version criteria are reviewed against `PRD.md`.
- [ ] `TASKS.md` has no open `Must` tasks for v1.
- [ ] Known non-v1 items are explicitly accepted:
  - SMB-to-desktop drag-and-drop remains a platform-specific follow-up.
  - Recursive SMB search remains optional/experimental.
  - Docker Samba integration tests are opt-in and not part of default local test runs.
  - Native DFS/current-user/Kerberos/encryption limitations are documented if still open.
  - Platform packaging smoke must be completed on each target OS before publishing packages.

## Build gate

- [ ] Configure from a clean build directory:

```bash
cmake -S . -B tmp/build
```

- [ ] Build succeeds:

```bash
cmake --build tmp/build
```

- [ ] Native SMB backend is enabled and legacy libsmb2 backend is disabled:

```bash
cmake -LA -N tmp/build | grep -E 'SMB_BROWSER_WITH_(NATIVE_SMB|LIBSMB2)'
```

- [ ] No generated files outside `tmp/` or intended build output directories are committed.
- [ ] Release build uses hardening flags where supported:

```bash
cmake -S . -B tmp/package-linux -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/package-linux --target package
```

- [ ] Generate release dependency manifest:

```bash
make sbom
```

## Default test gate

- [ ] Full default test suite passes:

```bash
ctest --test-dir tmp/build --output-on-failure
```

- [ ] Core/security targets pass as part of the suite:
  - `path_normalizer`
  - `credential_store_contract`
  - `encrypted_vault_credential_store`
  - `qtkeychain_credential_store`
  - `log_sanitizer`
  - `file_logger`
  - `import_export_service`
  - `connection_import_export_service`

- [ ] UI smoke targets pass as part of the suite:
  - `main_window`
  - `connections_panel`
  - `connection_dialog`
  - `remote_file_model`
  - `remote_browser_widget`
  - `settings_dialog`
  - `log_viewer`
  - `import_export_controller`
  - `ui_smoke`

- [ ] Native SMB unit/protocol suite passes without `libsmb2`:

```bash
make native-test
```

## Optional Docker Samba profile

- [ ] Start the local Samba fixture:

```bash
make samba-up
```

- [ ] Configure a separate integration build:

```bash
cmake -S . -B tmp/build-samba -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=ON
cmake --build tmp/build-samba
```

- [ ] Run only the integration profile:

```bash
ctest --test-dir tmp/build-samba -L docker-samba --output-on-failure
```

- [ ] Stop the fixture:

```bash
make samba-down
```

## Security gate

- [ ] `security_regression` passes in the default `ctest` suite.

- [ ] Default export does not contain:
  - passwords;
  - `credentialRef`;
  - token-like values;
  - master password values;
  - credential-store implementation data.

- [ ] Plain-text password export is available only through the explicit dangerous flow:
  - default option is without passwords;
  - strong warning is shown;
  - separate confirmation is required;
  - operation fails if secrets cannot be read from `CredentialStore`.

- [ ] Logs are sanitized:
  - no passwords;
  - no tokens;
  - no full credential strings;
  - no master password values.

- [ ] SQLite database contains metadata only and does not contain plain-text passwords.

- [ ] Test data uses synthetic credentials only.

- [ ] Package dependency audit rejects legacy SMB runtime dependencies:
  - no `libsmb2`;
  - no `smbclient`;
  - no Samba client helper binaries.

- [ ] Native SMB backend does not write credentials to stdout/stderr.

- [ ] License/compliance review confirms Samba is not copied, linked or
  distributed; local Samba checkouts under `tmp/` are reference-only.
- [ ] Project license metadata is present:
  - `LICENSE` exists and declares `GPL-3.0-or-later`;
  - `NOTICE` exists;
  - package metadata declares `GPL-3.0-or-later`;
  - release package includes `LICENSE` and `NOTICE`.

## Localization gate

- [ ] English is the primary source language for user-facing UI strings.
- [ ] Russian translation is available through Qt translation files.
- [ ] Language mode supports `System`, `English`, and `Russian`.
- [ ] Unsupported system locale falls back to English.
- [ ] `ui_smoke` and `localization_manager` pass.

## Manual platform smoke

Run on each target platform before publishing binaries.
Use `docs/cross-platform-smoke.md` as the detailed command/manual checklist.

- [ ] Windows:
  - package smoke script passes:
    `powershell -ExecutionPolicy Bypass -File scripts\package-smoke-windows.ps1`;
  - app starts;
  - `--smoke-close-ms=1000` path exits cleanly without leaving a process;
  - Qt runtime, QtKeychain, SQLite and translations are packaged;
  - package contains no `libsmb2.dll`, `smbclient.exe` or Samba client runtime;
  - add/edit/delete connection works;
  - credential save/load works;
  - connect/list/upload/download/rename/delete works against a test SMB server;
  - closing the main window exits the application process.

- [ ] Linux:
  - package smoke script passes:
    `scripts/package-smoke-linux.sh`;
  - app starts on the target distribution;
  - `--smoke-close-ms=1000` path exits cleanly without timeout;
  - Secret Service/KWallet behavior is understood and documented;
  - translations are packaged and load correctly;
  - DEB metadata and `ldd` contain no `libsmb2`, `smbclient` or Samba client runtime;
  - Docker Samba integration profile can run in CI or on a prepared host.

- [ ] macOS:
  - package smoke script passes:
    `scripts/package-smoke-macos.sh`;
  - app bundle starts;
  - `--smoke-close-ms=1000` path exits cleanly without leaving a process;
  - Keychain prompts are expected and understandable;
  - translations are packaged and load correctly;
  - bundle and `otool -L` contain no `libsmb2`, `smbclient` or Samba client runtime;
  - opening a downloaded file through the system application works;
  - closing the main window exits the application process.

## Release notes

- [ ] Known limitations are included.
- [ ] Plain-text password export warning is mentioned.
- [ ] Docker Samba integration profile is documented as optional.
- [ ] First-run storage locations for database, logs, cache, and settings are documented.
