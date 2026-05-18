# Secret handling policy

Документ фиксирует минимальные правила работы с секретами для SMB Browser. Он используется как checklist при code review задач credentials, SMB backend, import/export, logging и UI.

## Что считается секретом

- Пароли SMB-подключений.
- Master password для encrypted vault.
- Токены, если они появятся в будущих интеграциях.
- Полные credential strings, включая `smb://user:password@server/share`, `DOMAIN\user:password`, `DOMAIN:user:password`.
- Сырые значения, полученные из `CredentialStore::load`.

## Правила хранения

- SQLite хранит только metadata и `credential_ref`; plain-text пароли в SQLite запрещены.
- Primary storage секретов - QtKeychain / системное хранилище.
- Fallback storage - локальный encrypted vault с master password и проверенной криптографией.
- Master password не сохраняется в plain text и не попадает в settings, SQLite или логи.
- Export без паролей является режимом по умолчанию.
- Export с plain-text паролями допустим только как ручная опасная операция с отдельным подтверждением.

## Правила передачи между слоями

- UI models не содержат secret value. UI может принимать пароль из формы только для сохранения или обновления через application service.
- Repositories не принимают и не возвращают plain-text пароль.
- SMB backend получает секрет только на время конкретной операции.
- Application services отвечают за минимальный lifetime секрета между `CredentialStore` и backend.
- Infrastructure layer возвращает typed errors и sanitized technical details, а не локализованные user-facing strings.

## Правила логирования

- Любая запись в файл или окно журнала проходит через `LogSanitizer`.
- Запрещено логировать пароль, master password, bearer token, полный credential URI или raw connection string с userinfo.
- Ошибки backend должны сохранять диагностическую пользу без секретов: server, share, код ошибки, timeout, operation id.
- Перед добавлением нового логирования проверить, что message и technical details не содержат raw secret value.

## Очистка из памяти

- Для `QString`/Qt-контейнеров невозможно гарантировать полное zeroization из-за implicit sharing и копий внутри Qt.
- Код должен сокращать lifetime секретов: не хранить их в долгоживущих объектах, моделях, queued UI events или exception-like error objects.
- Там, где используются mutable byte buffers для vault/crypto, буферы очищаются после использования, если это поддержано библиотекой.
- Не добавлять секреты в debug UI, telemetry, crash context или assertion messages.

## Review checklist

- Нет новых полей password/secret/token в SQLite schema, domain metadata, UI models или settings.
- Все paths import/export/check/connect покрыты sanitizer или явно не формируют лог с секретом.
- Unit tests подтверждают отсутствие секретов в default export и логах.
- Tests используют synthetic secrets, а не реальные пароли.
- Ошибки, возвращаемые в UI, не содержат raw backend credential string.
- Plain-text password export требует отдельного подтверждения и не включается случайно флагом default export.
