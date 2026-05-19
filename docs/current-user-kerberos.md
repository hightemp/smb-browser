# Current user / Kerberos design

This document defines the planned strategy for `AuthType::CurrentUser` in the
clean-room native SMB backend. It is a design gate, not an implementation claim.

## Product behavior

Current-user authentication must be feature-gated. The UI must not present it
as available unless the active backend reports support for the current platform
and session.

Until implementation is complete, the native backend returns a typed
unsupported-capability error for `CurrentUser`, and the connection dialog should
show a clear diagnostic instead of silently falling back to password auth.

Password, guest and anonymous auth continue to use the existing native NTLMv2
path and must not depend on Kerberos libraries.

## Windows

Planned mechanism:

- Use SSPI through the OS security APIs.
- Prefer Kerberos when the server/SPN/domain policy allow it.
- Allow SSPI to negotiate NTLM only when policy permits.
- Do not ask the user for a password in current-user mode.
- Do not export or log SSPI tokens.

Runtime dependency decision:

- SSPI is an OS API and is allowed by the one-binary decision.
- No Samba, `smbclient`, `libsmb2` or MIT Kerberos runtime is required for the
  Windows current-user path.

Validation:

- Manual Windows domain test with a domain-joined workstation.
- Manual Windows Server share requiring signing.
- Failure tests for non-domain machine, missing SPN and denied share access.

## Linux

Planned mechanism:

- Use GSSAPI through MIT Kerberos or Heimdal when available.
- Use the default credential cache, for example a user TGT from `kinit` or a
  desktop login flow.
- Build and package this as an optional capability, because not all Linux
  desktop installs have a usable Kerberos environment.

Runtime dependency decision:

- System GSSAPI/Kerberos libraries are allowed if explicitly documented and
  audited in package metadata.
- The native SMB engine must still build and run without GSSAPI when the option
  is disabled.
- No Samba tools are allowed as a fallback.

Validation:

- Unit tests for capability reporting.
- Manual or CI runner test with synthetic realm if practical.
- Negative tests for missing ticket cache and expired tickets.

## macOS

Planned mechanism:

- Use the platform GSS framework / Kerberos integration.
- Use the user's existing ticket cache or platform login integration.
- Keep prompts and failures explicit; do not open password dialogs from the
  SMB backend.

Runtime dependency decision:

- macOS system GSS/Kerberos frameworks are allowed OS dependencies.
- Bundled Samba helpers remain prohibited.

Validation:

- Manual test on a managed macOS account with access to a Windows/Samba test
  share.
- Negative tests for no ticket and access denied.

## Backend API requirements

The native SMB facade should expose capability reporting before the UI enables
current-user mode:

- `supportsCurrentUserAuth`;
- supported mechanism names, for diagnostics only;
- reason when unavailable;
- whether signing/encryption policy can still be satisfied.

The auth implementation must produce security tokens through an interface,
separate from NTLMv2 password token generation. The existing `TokenProvider`
pattern can be extended, but SSPI/GSSAPI code must remain behind platform
abstractions.

## Secret handling

- Never log GSSAPI/SSPI security tokens.
- Never include SPNEGO blobs in user-visible errors.
- Never serialize current-user auth material to SQLite or export files.
- Sanitized diagnostics may include platform and mechanism names, but not token
  bytes or credential cache paths if those reveal usernames or realms.

## Test plan

Unit tests:

- availability reporting per platform/build option;
- unsupported-capability mapping while implementation is disabled;
- no fallback from current-user to password auth;
- token/error sanitization.

Manual/integration tests:

- Windows domain current-user connection to test share;
- Linux Kerberos ccache connection if GSSAPI profile is enabled;
- macOS GSS current-user connection if runner/environment is available;
- signing/encryption policy interactions;
- wrong realm/no ticket/access denied diagnostics.
