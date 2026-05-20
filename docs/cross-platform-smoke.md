# Cross-platform smoke profile

This document defines the release smoke profile for packages built with the
clean-room native SMB engine. It uses synthetic credentials only.

## Automated checks

Run on every platform package before publishing:

- Linux: `make smoke-linux`
- Windows: `pwsh -File scripts/package-smoke-windows.ps1 <package.zip>`
- macOS: `scripts/package-smoke-macos.sh <package.dmg|smb-browser.app>`

Windows and macOS package smoke can also be started through the manual GitHub
Actions workflow `.github/workflows/package-smoke.yml`.

Each package smoke must verify:

- app binary exists and starts;
- app closes itself through `--smoke-close-ms=1000`, proving the packaged app
  does not hang after the main window closes;
- Russian translation is packaged;
- Qt runtime/frameworks, platform plugin, SQLite driver, QtKeychain runtime and
  libsodium runtime are packaged;
- no `libsmb2`, `smbclient` or Samba client helper binary is bundled;
- executable dependency scan does not report `libsmb2`, `smbclient` or Samba
  client runtime linkage.

If `SMB_BROWSER_SMOKE_SERVER` and `SMB_BROWSER_SMOKE_SHARE` are set, the package
smoke also runs the packaged app with `--smoke-smb-list` and requires a
successful directory listing through the built-in native backend.

The default `ctest` suite covers non-network smoke flows that must remain fast:

- `ui_smoke`, `main_window`, `connections_panel`,
  `connection_management_controller`: window creation and add/edit/delete
  connection workflow;
- `credential_store_contract`, `qtkeychain_credential_store`,
  `encrypted_vault_credential_store`: credential save/load/update/delete
  contracts;
- `smb_client_contract`: check/list/upload/download/mkdir/delete/rename/copy/
  move/open-file-adjacent transfer behavior through the `SmbClient` contract;
- `open_file_service`, `transfer_manager`, `remote_browser_widget`:
  download/open/upload-sync-adjacent browser workflows;
- `security_regression`: no secrets in default export, SQLite metadata, logs,
  operation names or native diagnostics.

## Native SMB real-server smoke

For runner-backed or manual real-server validation, run `smb_client_contract`
with a synthetic test share:

```bash
SMB_BROWSER_NATIVE_CONTRACT_SERVER=server.example.test \
SMB_BROWSER_NATIVE_CONTRACT_SHARE=share \
SMB_BROWSER_NATIVE_CONTRACT_USER=smbtest \
SMB_BROWSER_NATIVE_CONTRACT_PASSWORD=synthetic-password \
ctest --test-dir tmp/build -R smb_client_contract --output-on-failure
```

Optional fixture paths:

- `SMB_BROWSER_NATIVE_CONTRACT_SYMLINK_PARENT`
- `SMB_BROWSER_NATIVE_CONTRACT_SYMLINK_NAME`
- `SMB_BROWSER_NATIVE_CONTRACT_DFS_PATH`

Do not use corporate credentials in committed configs, CI variables visible to
logs, screenshots or issue attachments.

The packaged app can also perform a headless list smoke:

```bash
SMB_BROWSER_SMOKE_SERVER=server.example.test \
SMB_BROWSER_SMOKE_SHARE=share \
SMB_BROWSER_SMOKE_USER=smbtest \
SMB_BROWSER_SMOKE_PASSWORD=synthetic-password \
SMB_BROWSER_SMOKE_PATH=/ \
./smb-browser --smoke-smb-list
```

## Manual Windows Server release smoke

Before release, validate against a Windows Server or compatible corporate SMB
test environment:

- password/domain auth, guest/anonymous if available;
- connect/list/upload/download/open file through the in-app browser;
- edit an opened file locally, save and verify upload-sync result on server;
- DFS namespace navigation, including nested referral if available;
- required signing policy and encryption-required share;
- symlink/reparse directory and file navigation;
- ACL/EA/hardlink/notify scenarios where the server exposes them;
- closing the main window exits the process, with no tray/background process.

Record pass/fail notes in the release checklist. Redact server names if they
are sensitive.
