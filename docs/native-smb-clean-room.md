# Native SMB clean-room migration

Документ фиксирует gate-решения для перехода с `libsmb2` и внешней утилиты
`smbclient` на внутренний SMB2/SMB3 engine, встроенный в бинарник приложения.

Цель миграции: приложение должно собираться из clean clone и работать без
runtime-зависимостей от `libsmb2`, `smbclient` или других внешних SMB-клиентов.
Qt/runtime зависимости могут поставляться рядом с приложением как часть
portable package или platform bundle.

## Принятые решения

- Реализация SMB engine: `clean-room only`.
- Samba не входит в source distribution и binary distribution проекта.
- Samba можно использовать только как поведенческий reference в локальной
  папке `tmp/`.
- Копирование, перевод или адаптация кода Samba запрещены.
- Проект переходит в open-source/GPL-compatible модель распространения, но это
  продуктово-лицензионное решение проекта, а не разрешение копировать Samba.
- Поддерживаются только SMB2/SMB3.
- SMB1 и NetBIOS browsing исключены.
- SMB engine должен быть внутри бинарника приложения.
- Полностью статический Qt не является обязательной целью.
- Windows app folder, Linux portable package и macOS app bundle считаются
  допустимыми форматами поставки.
- Проверка доступности выполняется только по действию пользователя, а не
  автоматически при старте.
- Реализация идет staged: сначала parity обязательных возможностей текущего
  приложения, затем расширенные возможности библиотеки.

## Допустимые runtime dependencies

Допустимы:

- Qt runtime libraries и Qt platform plugins, если они поставляются рядом с
  приложением или доступны системно согласно выбранному packaging profile.
- SQLite и Qt SQL driver.
- QtKeychain или системные keychain/secret-service API.
- OS sockets, DNS resolver, filesystem APIs, system dialogs and shell-open API.
- System crypto/Kerberos/GSSAPI libraries, если они нужны для current-user/SSO
  и явно отражены в packaging/license documentation.

Недопустимы как runtime requirements:

- `libsmb2`;
- `smbclient`;
- Samba CLI tools;
- установленный Samba client package только ради DFS, browse или diagnostics.

Dependency audit для release profiles должен использовать:

- Linux: `ldd`, package metadata inspection, package smoke script.
- macOS: `otool -L`, bundle inspection, package smoke script.
- Windows: `dumpbin` или эквивалентный dependency scanner, package smoke script.

## Samba reference snapshot

Локальный reference checkout:

- Путь: `tmp/samba-src`.
- Upstream: `https://github.com/samba-team/samba`.
- Проверенный commit: `66fec3d`.
- Дата анализа: 2026-05-19.
- Основной файл для поведенческого анализа: `source3/client/client.c`.
- Размер `source3/client/client.c`: 6823 строки.
- License notice в `client.c`: GNU GPL version 3 or later.

Наблюдения:

- `source3/client/client.c` является CLI client implementation, а не
  embeddable library API.
- В файле есть command table `commands[]`, покрывающая интерактивные команды
  `smbclient`.
- `source3/wscript_build` собирает `client/smbclient` из
  `client/client.c`, `client/clitar.c`, `client/dnsbrowse.c`.
- Проверенные build dependencies для CLI path включают `talloc`,
  `CMDLINE_S3`, `smbconf`, `ndr-standard`, `SMBREADLINE`, `libsmb`,
  `msrpc3`, `RPC_NDR_SRVSVC`, `cli_smb_common`, `archive`.
- CLI flow завязан на interactive state, stdout/stderr, command parsing,
  readline/history, process lifetime и global state. Такой flow нельзя
  напрямую переносить в Qt service layer.

Вывод: Samba полезна как reference для набора возможностей и совместимости, но
не является готовой внутренней библиотекой для этого приложения.

## Clean-room правила

Разрешено:

- Изучать наблюдаемое поведение `smbclient` на тестовых серверах.
- Фиксировать high-level behavior notes без копирования кода и внутренних
  структур.
- Использовать публичные спецификации протоколов SMB2/SMB3, DFS, security,
  NTSTATUS and MS-RPC.
- Писать собственные тесты совместимости против Samba Docker fixture и Windows
  Server.
- Сравнивать результаты команд на уровне observable behavior: какие операции
  успешны, какие ошибки возвращаются, какие поля видны пользователю.

Запрещено:

- Копировать или переводить функции Samba.
- Копировать структуры, enum/table definitions, command tables, comments,
  internal state machines или layout внутренних объектов.
- Использовать заголовки Samba как public/private API приложения.
- Линковать Samba libraries как часть этого clean-room path.
- Переносить snippets из `source3/client/client.c`,
  `source3/include/libsmbclient.h` или соседних internal modules.

Review checklist перед merge protocol code:

- Новый код не содержит дословных фрагментов Samba.
- Имена internal helpers не повторяют Samba-specific implementation naming без
  причины.
- Поведение обосновано публичной спецификацией, собственным дизайном или
  black-box тестом.
- Ошибки и логи не содержат credentials.
- Commit не добавляет содержимое `tmp/samba-src`.

## Source acquisition policy

Для clean-room native engine нет external source acquisition step. Исходники
engine создаются и ревьюятся внутри этого репозитория, начиная с
`src/native_smb`. Локальные checkout-ы в `tmp/` используются только для
исследования и не являются build input.

Текущий стартовый target: `smb_browser_native_smb`. Он собирает только
clean-room scaffold и build policy; protocol implementation добавляется
последующими задачами.

Правила:

- CMake не должен скачивать Samba, `libsmb2` или другой SMB client source для
  native engine.
- `tmp/samba-src` не должен попадать в source distribution.
- Offline build native engine должен работать из clean clone без сети.
- Если когда-либо появится внешний dependency для protocol/security layer, он
  должен получить отдельный license/security review, pinned version и checksum
  до включения в build.
- Legacy `libsmb2` FetchContent остается только временным transition path до
  выполнения задач по замене backend-а и не является частью native engine.

## Feature matrix

| Feature group | Priority | Library scope | UI scope |
| --- | --- | --- | --- |
| SMB2/SMB3 negotiation | Must | Диалекты SMB2/SMB3, no SMB1 offer, capability detection | Диагностика в error/details |
| TCP/DNS connection | Must | Connect, timeout, retry policy, structured errors | Check/connect actions |
| Password/domain auth | Must | `DOMAIN\user`, `user@domain`, plain user, password | Existing connection dialog |
| Guest/anonymous auth | Must | Explicit auth modes, actionable failures | Existing auth type controls |
| Current user/Kerberos/SSO | Must | Platform design and implementation with capability reporting | Show only when supported |
| Signing | Must | SMB2/SMB3 signing policy and validation | Security diagnostics |
| Encryption | Must | SMB3 encryption when required/preferred | Security diagnostics |
| DFS namespace | Must | Referrals, nested DFS, multiple targets, failover, TTL cache | Transparent navigation |
| Check availability | Must | Server/share/auth/permission/protocol classification | Existing Check button |
| Directory list/stat | Must | Entries, size, timestamps, attributes, permissions, reparse flags | Browser table/model |
| Open folder/up/back/refresh | Must | Path operations and directory handles | Existing browser navigation |
| Download/read | Must | Stream read, progress, cancellation, resume capability | Download/open file/cache |
| Upload/write | Must | Stream write, progress, cancellation, resume capability | Upload/drag-and-drop |
| Open file via temp cache | Must | Download to safe local cache, sync hooks | Existing open-file flow |
| Create/delete/recursive delete | Must | `mkdir`, delete file, rmdir, deltree with cancellation | Existing operations |
| Rename/move | Must | Same-share rename, cross-share copy+delete fallback | Existing operations |
| Copy | Must | Server-side copy when available, stream fallback | Existing operations |
| Symlink/reparse navigation | Must | Follow directory links, open file links, read target where supported | Existing symlink behavior |
| Hardlink/symlink creation | Must | Library API with capability detection | UI later |
| ACL | Must | Read/write where server supports it, capability gated | Properties UI later |
| Extended attributes | Must | List/read/set/remove where supported | Properties UI later |
| chmod/chown/utimes/setmode | Must | Capability gated metadata operations | Properties UI later |
| Notify/watch | Must | Watch API with cancellation and fallback | Auto-refresh later |
| Share browsing | Should | IPC/RPC enumeration without SMB1/NetBIOS | Optional connection helper |
| Recursive SMB search | Could | Bounded traversal, cancellation, progress | Optional feature |
| Print queue commands | Not planned | Excluded from library | Excluded |
| Tar/archive commands | Not planned | Excluded from library | Excluded |
| Local shell command | Not planned | Excluded from library | Excluded |
| Message commands | Not planned | Excluded from library | Excluded |

## Native library architecture

`SmbNative` должен быть internal library, не зависящей от Qt Widgets.
Application layer продолжает работать через существующий `SmbClient` interface,
а UI не получает доступа к protocol internals.

Предлагаемые слои:

- Public C++ facade: stable API для application services.
- Protocol engine: SMB2/SMB3 packets, state machines, credits, dialect
  negotiation.
- Transport layer: TCP sockets, timeout, cancellation, read/write framing.
- Auth layer: NTLM/password, guest/anonymous, Kerberos/current-user where
  available.
- Crypto/security layer: signing, encryption, key derivation, secure buffers.
- DFS resolver: referrals, target cache, rebase to original namespace,
  failover.
- File service: list/stat/open/read/write/mkdir/delete/rename/copy/move.
- Metadata service: ACL, EA, timestamps, chmod/chown/setmode, symlink/hardlink,
  notify.
- Error mapper: native status/system errors to backend-neutral `SmbErrorCode`.
- Test harness: fake transport, protocol fixtures, Docker Samba and manual
  Windows Server validation.

Public facade requirements:

- No stdout/stderr diagnostics.
- No global mutable client state.
- No UI strings.
- No raw credential strings in logs or errors.
- Explicit ownership/lifetime for sessions, handles and buffers.
- Progress callbacks for long operations.
- Cancellation token or operation context for network and file operations.
- Thread-safety rules documented per object type.

Public facade concepts:

- Connection config: server, share, normalized URI, domain, username, auth type,
  dialect/security policy, timeout policy.
- Credential input: short-lived password/secret buffer or system-auth token,
  never a credential-bearing URI.
- Session: authenticated connection context with explicit close/disconnect and
  share identity.
- Directory iterator/result page: remote path plus entries and continuation
  state for large directories.
- File handle: read/write/close operations with byte offsets and durable error
  reporting.
- Metadata handle/result: stat/all-info, attributes, timestamps, permissions,
  ACL, EA and reparse/symlink information.
- Transfer callbacks: bytes processed, total size if known, current path,
  operation id and cancellation state.
- Error result: native category/status, backend-neutral `SmbErrorCode`,
  sanitized technical details and retryability/capability hints.

## Threading and operation model

- UI thread never performs network IO.
- Application layer owns `OperationQueue`.
- `TransferManager` owns upload/download/copy/move orchestration.
- `SmbNative` operations accept operation context with timeout, cancellation,
  progress callback and sanitized logging hook.
- Session objects are either single-thread-affine or protected by explicit
  synchronization; the chosen rule must be documented before implementation.
- Cancellation is best-effort but must stop large transfers and directory walks
  in bounded time.

## Security rules

- Passwords are never stored in SQLite.
- Secrets are loaded from `CredentialStore` only for the operation that needs
  them.
- Secret lifetime must be short.
- Where mutable buffers are used for crypto/auth material, clear them after use
  when practical.
- Avoid storing secrets in `QString`, queued UI events, exceptions, logs,
  operation names or diagnostics.
- All backend logs pass through `LogSanitizer`.
- Plain-text password export remains a separate dangerous manual flow and is
  unaffected by SMB backend migration.

## Delivery plan

Phase 1: design and gates

- Freeze clean-room rules.
- Freeze one-binary definition.
- Freeze feature matrix.
- Define public `SmbNative` facade and error model.
- Update release/license documentation.

Phase 2: app Must parity

- Implement connect/check/list/stat.
- Implement password/domain/guest/anonymous auth.
- Implement download/upload/open-file cache flow.
- Implement mkdir/delete/rename/copy/move.
- Implement DFS without `smbclient`.
- Replace `Libsmb2SmbClient` and `SmbclientDfsReferralResolver`.

Phase 3: security and enterprise behavior

- Implement signing and encryption.
- Implement current-user/Kerberos/SSO where supported.
- Validate against Windows Server and Samba.
- Add dependency audit that fails on `libsmb2` or `smbclient`.

Phase 4: advanced library capabilities

- ACL, EA, chmod/chown/utimes/setmode.
- Symlink/hardlink creation and reparse handling.
- Notify/watch API.
- Share browsing if needed.

Phase 5: packaging and release

- Linux portable build/package smoke.
- Windows portable build/package smoke.
- macOS app bundle/package smoke.
- README clean-clone instructions.
- Release checklist and license review.

## Test strategy

Полная матрица покрытия зафиксирована в `docs/native-smb-test-matrix.md`.

Default tests:

- Unit tests for path normalization, credentials conversion, error mapping,
  sanitizer and operation state.
- Fake transport/protocol tests for request/response parsing and state
  transitions.
- `FakeSmbClient` contract tests for UI/application behavior.
- Security regression tests for logs, SQLite and default export.

Opt-in integration tests:

- Docker Samba profile with synthetic credentials.
- Multiple shares, nested directories, permissions, guest/password auth.
- Large file upload/download with cancellation and resume.
- Symlink/reparse fixture where available.
- ACL/EA fixture where available.
- DFS fixture if reliable in Docker; otherwise document manual coverage.

Manual Windows Server validation:

- Corporate SMB/DFS namespace.
- Nested DFS referrals, multiple targets and failover.
- Signing required.
- Encryption required or preferred.
- Current-user/Kerberos/SSO.
- ACL, EA, symlink, hardlink and notify.

Test data rules:

- No real passwords in repository.
- No real corporate server credentials in logs, screenshots or fixtures.
- `tmp/mylist.json` remains local scratch data and is never committed.

## Open risks

- Full clean-room SMB2/SMB3 implementation is significantly larger than
  replacing a backend adapter.
- Kerberos/current-user behavior is platform-specific and may require system
  libraries.
- DFS behavior differs across Windows Server, Samba and corporate namespace
  configurations.
- Signing/encryption mistakes are security-sensitive and require focused
  protocol tests.
- ACL/EA/POSIX metadata support varies by server and server configuration.
- Cross-platform packaging must be honest about Qt and OS-level runtime
  dependencies.

## Fallback plan

If the clean-room implementation cannot reach production quality on schedule:

- Keep the existing backend behind a legacy build flag only for local
  comparison, not as the target runtime.
- Reduce first public release scope to read-only SMB2/SMB3 browsing only if
  write operations or DFS are not safe yet.
- Do not silently reintroduce `smbclient` or `libsmb2` as required runtime
  dependencies.
- Escalate the scope decision in `TASKS.md` and release notes before shipping.
