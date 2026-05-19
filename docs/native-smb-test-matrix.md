# Native SMB library test matrix

Документ фиксирует, как проверять весь функционал внутренней clean-room SMB
library. Возможность считается готовой только когда для нее есть подходящий
набор unit, fake-transport, integration и manual tests.

## Test layers

1. Protocol unit tests

   Проверяют binary layout, endianess, offsets, constants, request builders,
   response parsers, state transitions and error mapping. Эти тесты быстрые,
   deterministic и не используют сеть.

2. Fake transport tests

   Проверяют session state machine поверх scripted request/response transport:
   negotiation, auth flow, tree connect, read/write loops, cancellation,
   timeout, reconnect and DFS rebase logic. Эти тесты не требуют SMB-сервера.

3. Fake SMB client contract tests

   Проверяют application-level `SmbClient` contract. Они должны проходить для
   `FakeSmbClient` всегда и для `NativeSmbClient` в integration profile.

4. Docker Samba integration tests

   Проверяют реальный wire interaction с Samba fixture на synthetic
   credentials. Профиль opt-in и не запускается в default local unit run.

5. Windows Server manual/runner tests

   Проверяют корпоративные сценарии, которые плохо моделируются Docker Samba:
   DFS namespace, current user/Kerberos/SSO, signing/encryption policies,
   Windows ACL/reparse behavior.

6. Security regression tests

   Проверяют, что пароли не попадают в logs, SQLite, default export,
   operation names, stdout/stderr, crash/error details and package metadata.

7. Package dependency audit

   Проверяет, что release package не требует `libsmb2`, `smbclient` или Samba
   CLI tools at runtime.

## Capability coverage matrix

| Capability | Unit | Fake transport | Docker Samba | Windows Server/manual | Notes |
| --- | --- | --- | --- | --- | --- |
| Direct TCP framing | Must | Could | Covered indirectly | Covered indirectly | Length field and invalid marker cases |
| SMB2 SYNC header | Must | Must | Covered indirectly | Covered indirectly | Command, flags, ids, signature bytes |
| NEGOTIATE request/response | Must | Must | Must | Must | SMB2/SMB3 only; no SMB1 dialects |
| Dialect policy | Must | Must | Must | Must | SMB1 excluded; SMB 3.x capabilities explicit |
| Session setup | Must | Must | Must | Must | Password, guest, anonymous, current-user availability |
| Password/domain auth | Must | Must | Must | Should | Synthetic secrets only |
| Guest/anonymous auth | Must | Must | Must | Should | Fixture must cover success and failure |
| Current user/Kerberos/SSO | Must | Should | Could | Must | Platform-specific, feature-gated |
| Signing | Must | Must | Should | Must | Include required/preferred/policy mismatch |
| Encryption | Must | Must | Should | Must | Include required/preferred/policy mismatch |
| Tree connect/disconnect | Must | Must | Must | Must | Share unavailable vs permission denied |
| DFS referrals | Must | Must | Should | Must | Nested referrals, multiple targets, failover, TTL |
| Directory listing | Must | Must | Must | Must | Large directory and cancellation cases |
| Stat/all-info | Must | Must | Must | Must | Timestamps, size, attributes, reparse flags |
| Download/read | Must | Must | Must | Must | Progress, cancellation, resume, cache safety |
| Upload/write | Must | Must | Must | Must | Progress, cancellation, overwrite/resume |
| Open file through cache | Should | Must | Must | Should | Edit/save/upload sync regression |
| Create directory | Must | Must | Must | Must | Already exists and permission denied |
| Delete/rmdir/deltree | Must | Must | Must | Must | Partial failure and cancellation |
| Rename/move | Must | Must | Must | Must | Same-share and cross-share fallback |
| Copy | Must | Must | Must | Must | Server-side path and stream fallback |
| Symlink/reparse navigation | Must | Must | Should | Must | Directory links and file links |
| Symlink creation | Must | Must | Could | Must | Capability-gated |
| Hardlink creation | Must | Must | Could | Must | Capability-gated |
| ACL | Must | Must | Should | Must | Read/write and unsupported-server behavior |
| Extended attributes | Must | Must | Should | Should | Read/set/remove where supported |
| chmod/chown/utimes/setmode | Must | Must | Should | Should | Capability-gated |
| Notify/watch | Must | Must | Could | Must | Cancellation and fallback to manual refresh |
| Share browsing | Should | Should | Should | Should | IPC/RPC only; no SMB1/NetBIOS |
| Timeouts | Must | Must | Must | Should | Connect/list/read/write/metadata |
| Cancellation | Must | Must | Must | Should | Bounded cancellation for long operations |
| Error mapping | Must | Must | Must | Must | Backend-neutral `SmbErrorCode` |
| Secret sanitization | Must | Must | Must | Must | Logs, errors, export, stdout/stderr |
| Dependency audit | Could | Could | Must | Must | No `libsmb2`, no `smbclient` runtime |

## Required test labels

- `native-unit`: deterministic native library unit tests.
- `native-protocol`: binary protocol encode/decode tests.
- `native-fake-transport`: scripted transport state-machine tests.
- `native-contract`: backend contract tests.
- `docker-samba`: opt-in Docker Samba integration.
- `windows-smb`: manual or runner-backed Windows Server validation.
- `package-smoke`: platform package and dependency audit.
- `security`: secret/log/export regression tests.

## Definition of done for native library features

A `Must` native library feature is not complete until:

- protocol-level behavior has unit tests where wire format is involved;
- state-machine behavior has fake-transport tests;
- application-facing behavior is covered by contract tests;
- at least one real-server validation path exists: Docker Samba, Windows
  Server, or an explicit documented reason why manual-only coverage is needed;
- errors are mapped to typed application errors;
- cancellation and timeout behavior is covered for long operations;
- secret handling regression tests cover new logs/errors/exports;
- limitations and unsupported server capabilities are documented.

## Credentials and fixtures policy

- No real passwords in tests.
- No corporate server credentials in repository.
- Docker Samba uses generated synthetic users and passwords.
- Manual Windows Server test notes must redact server-specific sensitive data.
- `tmp/mylist.json`, screenshots and local logs are not committed.

## Current implemented native tests

- `native_smb_scaffold`: clean-room build policy, public API boundary,
  move-only secret buffer and cancellation token.
- `native_smb_protocol`: SMB2 SYNC header, SMB2 NEGOTIATE request, Direct TCP
  framing, SMB2 NEGOTIATE response, SMB2 SESSION_SETUP request, signing
  security mode mapping, NTSTATUS typed error mapping, response early-failure
  behavior and no-SMB1 initial dialect policy.
- `native_smb_direct_tcp_transport`: loopback Direct TCP transport exchange,
  split response frame handling, timeout/cancellation-aware socket path and
  cancellation before socket open.
- `native_smb_ntlm_messages`: NTLMSSP negotiate/challenge/authenticate
  message encode/decode, security buffer offsets and invalid token rejection.
- `native_smb_ntlm_crypto`: clean-room MD4, MD5, HMAC-MD5, NT hash, NTOWFv2,
  NTLMv2 proof/session key and LMv2 response vectors.
- `native_smb_spnego_token`: GSS-SPNEGO NegTokenInit/NegTokenResp wrapping,
  raw NTLM passthrough and malformed DER rejection.
- `native_smb_signing`: clean-room SHA-256, HMAC-SHA256, AES-128,
  AES-128-CMAC, SMB2.0.2/2.1 HMAC signing, SMB3.0/3.0.2 AES-CMAC signing-key
  derivation and signed response verification over Direct TCP frames.
- `native_smb_error_mapper`: backend-neutral native error to `AppError`
  mapping, retryable classification and sanitized technical details.
- `native_smb_ntlm_v2_token_provider`: NTLMv2 token provider over raw NTLM and
  SPNEGO, deterministic challenge/timestamp injection, session base key export
  for signing, guest/anonymous auth behavior, current-user unsupported behavior
  and password non-disclosure assertions.
- `native_smb_connector`: baseline connection lifecycle
  `NEGOTIATE -> SESSION_SETUP token exchange -> TREE_CONNECT`, owned
  transport/session facade construction, token provider failure, request
  sequencing, signed `TREE_CONNECT` when signing is required, and explicit
  `TREE_DISCONNECT -> LOGOFF` teardown sequencing.
- `native_smb_directory_lister`: composed `CREATE -> QUERY_DIRECTORY... ->
  CLOSE` flow, paged listing until `STATUS_NO_MORE_FILES`, entry aggregation
  and correct `RestartScans` behavior.
- `native_smb_negotiator`: scripted transport negotiation state machine,
  Direct TCP frame exchange, server signing/encryption capability extraction,
  malformed frame handling and cancellation before transport IO.
- `native_smb_session_setup`: scripted transport SESSION_SETUP token exchange,
  response status/session-id/session-flags parsing, encryption session flag,
  unexpected response handling and cancellation before transport IO.
- `native_smb_tree_connector`: scripted transport tree connect state machine,
  UTF-16LE share path request, tree id extraction, DFS/share encryption flags,
  unexpected response handling and cancellation before transport IO.
- `native_smb_directory_lister`: scripted `CREATE directory` plus
  `QUERY_DIRECTORY` plus `CLOSE` flow, directory handle FileId propagation,
  `FileIdBothDirectoryInformation` entry parsing, invalid create response
  handling and cancellation before transport IO.
- `native_smb_close_exchanger`: scripted handle close flow, close response
  attribute parsing, unexpected response handling and cancellation before
  transport IO.
- `native_smb_read_exchanger`: scripted file read flow, READ request/response
  data parsing, unexpected response handling and cancellation before transport
  IO.
- `native_smb_file_reader`: scripted `CREATE file` plus `READ` plus `CLOSE`
  flow, file handle FileId propagation, data extraction, invalid read response
  handling and cancellation before transport IO.
- `native_smb_write_exchanger`: scripted file write flow, WRITE request/response
  count parsing, unexpected response handling and cancellation before transport
  IO.
- `native_smb_file_writer`: scripted `CREATE file` plus `WRITE` plus `CLOSE`
  flow, file handle FileId propagation, written byte count extraction, invalid
  write response handling and cancellation before transport IO.
- `native_smb_set_info_exchanger`: scripted SET_INFO flow,
  FileDispositionInformation buffer construction, response parsing, unexpected
  response handling and cancellation before transport IO.
- `native_smb_query_info_exchanger`: scripted QUERY_INFO flow,
  FileBasicInformation request construction, output buffer response parsing,
  unexpected response handling and cancellation before transport IO.
- `native_smb_remote_object_operator`: scripted `CREATE -> CLOSE` directory
  creation, `CREATE -> SET_INFO(FileDispositionInformation) -> CLOSE` delete
  flow, `CREATE -> SET_INFO(FileRenameInformation for SMB2) -> CLOSE` rename
  flow, invalid set-info response handling and cancellation before transport IO.
- `native_smb_remote_stat_reader`: scripted
  `CREATE -> QUERY_INFO(FileBasicInformation) ->
  QUERY_INFO(FileStandardInformation) -> CLOSE` stat flow, timestamps,
  attributes, size/allocation, link count, delete-pending, directory/reparse
  mapping, invalid query-info response handling and cancellation before
  transport IO.
- `native_smb_session`: baseline C++ facade over native primitives, operation
  routing for list/stat/read/write/delete/create-directory/rename, recursive
  delete, wildcard delete, message id allocation, result mapping, cancellation
  before recursive delete network IO and error propagation without exposing raw
  packet buffers to callers.
