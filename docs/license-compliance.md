# License and compliance

## Project license

SMB Browser source code is licensed under `GPL-3.0-or-later`.

The license choice is intentional for the open-source/GPL project model while
keeping the SMB engine clean-room. It does not permit copying Samba
implementation code into this repository.

## Clean-room SMB boundary

Allowed:

- Use public Microsoft protocol specifications.
- Use black-box behavior observations against test SMB servers.
- Keep local research checkouts under `tmp/`.
- Write original implementation and tests in this repository.

Not allowed:

- Copy Samba functions, comments, command tables, structs or internal state
  machines.
- Link Samba libraries into the native SMB backend.
- Bundle `smbclient` as a runtime helper.
- Treat `libsmb2` as a default runtime dependency.

## Distribution notes

Source and binary distributions must include:

- `LICENSE`;
- `NOTICE`;
- package metadata with `GPL-3.0-or-later`;
- dependency/release notes for Qt5, QtKeychain, SQLite/Qt SQL, libsodium and
  OS APIs.

The default package profile must pass dependency audit proving that `libsmb2`,
`smbclient` and Samba client tools are not required at runtime.

## Qt and runtime dependencies

Qt is distributed under its own licenses. Release owners must verify the chosen
Qt distribution model before publishing binaries. The current project strategy
allows Qt runtime libraries and plugins to be shipped next to the application
or used from the target system; fully static Qt is not required by the
one-binary SMB-engine decision.

QtKeychain, SQLite/Qt SQL and libsodium must be reviewed according to their own
licenses and package policies before release.

## Release gate

Before publishing:

- run the package smoke for the target platform;
- generate the dependency manifest with `make sbom`;
- confirm package metadata says `GPL-3.0-or-later`;
- confirm `LICENSE` and `NOTICE` are included;
- confirm no local `tmp/` research checkout enters the package;
- confirm no secrets appear in logs, package metadata or release artifacts.
