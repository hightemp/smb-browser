# TASKS: SMB Browser

Рабочий backlog для реализации SMB Browser. Задачи упорядочены по этапам так, чтобы сначала закрепить архитектуру, каркас и тестовую базу, затем последовательно реализовать storage, credentials, SMB abstraction, UI, browser, операции, tray/theme/logging, import/export и packaging.

Основной язык пользовательского интерфейса - английский. Русский язык должен поддерживаться как переключаемая локализация через настройки приложения.

Приоритеты:

- Must - обязательно для первой версии.
- Should - желательно для первой версии или ближайшего minor release.
- Could - опционально, можно отложить без разрушения основного продукта.

## Этап 0. Документация, решения и границы продукта

### [x] T-001: Зафиксировать продуктовые требования

- Приоритет: Must.
- Зависимости: нет.
- Описание: Поддерживать `PRD.md` как источник продуктовых требований, решений по безопасности, UI, архитектуре, тестам и критериям первой версии.
- Acceptance criteria:
  - В `PRD.md` описаны цели, не-цели, сценарии, функциональные и нефункциональные требования.
  - Зафиксированы решения: SQLite, QtKeychain, encrypted vault fallback, libsmb2, `RemoteFileModel`, async operations, `TransferManager`, `OperationQueue`, `LogSanitizer`.
  - Экспорт с plain-text паролями описан как опасная ручная операция.
- Заметки по тестам:
  - Тестов к документу нет.
  - На code review проверять соответствие реализации PRD.

### [x] T-002: Утвердить технические ограничения первой версии

- Приоритет: Must.
- Зависимости: T-001.
- Описание: Сформировать короткий engineering decision record по ограничениям первой версии: Qt5, Qt Widgets, CMake, отсутствие `QFileSystemModel` для SMB, отсутствие автоматической проверки всех подключений при старте.
- Acceptance criteria:
  - Ограничения видны в проектной документации.
  - Для каждого ограничения есть краткое обоснование.
  - Потенциальные исключения перечислены как open questions.
- Заметки по тестам:
  - Проверяется review checklist.

### [x] T-003: Подготовить архитектурную карту модулей

- Приоритет: Must.
- Зависимости: T-001.
- Описание: Описать ownership и зависимости между модулями: UI, application services, domain/core, infrastructure.
- Acceptance criteria:
  - UI не зависит напрямую от SQLite, QtKeychain и libsmb2.
  - `SmbClient`, `CredentialStore`, repositories и logging имеют четкие границы.
  - `LocalizationManager` отвечает за выбор языка и загрузку переводов; infrastructure layer не содержит user-facing strings.
  - Указаны допустимые направления зависимостей.
- Заметки по тестам:
  - Позже добавить dependency/lint check, если выбран инструмент.

## Этап 1. Каркас проекта и тестовая база

### [x] T-004: Создать CMake/Qt5 каркас проекта

- Приоритет: Must.
- Зависимости: T-002, T-003.
- Описание: Создать минимальную структуру проекта на CMake с Qt5 Widgets, каталогами для app, core, infrastructure, ui и tests.
- Acceptance criteria:
  - Проект конфигурируется через CMake на поддерживаемой dev-платформе.
  - Есть минимальное Qt Widgets приложение без бизнес-логики.
  - Структура каталогов отражает архитектурные слои.
  - Никакие реальные SMB-операции пока не реализованы.
- Заметки по тестам:
  - Smoke build test в CI или локальном script.

### [x] T-005: Подключить тестовый фреймворк

- Приоритет: Must.
- Зависимости: T-004.
- Описание: Выбрать и подключить тестовый фреймворк, совместимый с Qt5/CMake. Базовый вариант - Qt Test, если нет причины выбрать другое.
- Acceptance criteria:
  - Есть отдельная цель сборки unit tests.
  - Есть пример passing test без зависимости от SMB-сервера.
  - Тесты запускаются одной командой через CTest или аналогичный стандартный механизм.
- Заметки по тестам:
  - Добавить smoke test, который проверяет работу тестовой инфраструктуры.

### [ ] T-006: Настроить базовую CI-проверку

- Приоритет: Should.
- Зависимости: T-004, T-005.
- Описание: Настроить CI для сборки и unit tests минимум на Linux. Windows/macOS можно добавить отдельными задачами packaging этапа.
- Acceptance criteria:
  - CI собирает проект.
  - CI запускает unit tests.
  - Интеграционные Docker Samba tests не запускаются по умолчанию.
- Заметки по тестам:
  - CI должен падать при failing unit tests.

### [x] T-007: Ввести общие правила ошибок и результатов операций

- Приоритет: Must.
- Зависимости: T-004.
- Описание: Определить общий подход к результатам операций, typed errors и user-facing messages.
- Acceptance criteria:
  - Есть единая модель ошибки с code, sanitized technical details и user-facing message.
  - Модель применима к storage, credentials, SMB и transfer operations.
  - В модели нет полей, в которые предполагается писать пароль или credential string.
- Заметки по тестам:
  - Unit tests на mapping ошибок появятся в T-014 и T-030.

## Этап 2. Domain/core модели и нормализация

### [x] T-008: Реализовать доменную модель подключения

- Приоритет: Must.
- Зависимости: T-007.
- Описание: Создать domain representation для SMB-подключения: metadata, normalized path, auth type, timestamps, status fields.
- Acceptance criteria:
  - Модель содержит поля из PRD: name, input path, normalized URI, server, share, domain, username, auth type, credential ref, group, favorite, timestamps, last error.
  - Пароль не является частью metadata model.
  - Модель не зависит от Qt Widgets.
- Заметки по тестам:
  - Unit tests на default values и password absence.

### [x] T-009: Реализовать PathNormalizer

- Приоритет: Must.
- Зависимости: T-008.
- Описание: Реализовать нормализацию SMB-путей и учетных данных.
- Acceptance criteria:
  - `\\server\share` преобразуется в `smb://server/share`.
  - `smb://server/share` остается canonical URI.
  - `server/share` преобразуется в `smb://server/share`.
  - Server и share выделяются отдельно.
  - Подпуть внутри шары сохраняется отдельно, если поддержан.
  - Некорректный ввод возвращает typed validation error.
- Заметки по тестам:
  - Unit tests: `\\server\share`, `smb://server/share`, `server/share`.
  - Unit tests: пустой путь, путь без share, invalid URI.
  - Не использовать реальные секреты в тестовых данных.

### [x] T-010: Реализовать разбор username/domain

- Приоритет: Must.
- Зависимости: T-009.
- Описание: Поддержать форматы `DOMAIN\user`, `user@domain`, `user`, guest, anonymous и current user.
- Acceptance criteria:
  - `DOMAIN\user` дает domain = `DOMAIN`, username = `user`.
  - `user@domain` дает username = `user`, domain = `domain`.
  - `user` сохраняется как username без domain.
  - Guest/anonymous/current user представлены через auth type.
  - Current user может быть помечен как unsupported, если backend/platform не поддерживает.
- Заметки по тестам:
  - Unit tests на каждый формат.
  - Unit tests на conflict между explicit domain field и domain в username input.

### [x] T-011: Реализовать модель RemoteFileEntry

- Приоритет: Must.
- Зависимости: T-007.
- Описание: Определить domain DTO для удаленного файла или папки.
- Acceptance criteria:
  - Entry содержит name, remote path, type, size, modified date, attributes, permissions при наличии.
  - Entry не зависит от конкретного SMB-backend.
  - Entry подходит для отображения в `RemoteFileModel`.
- Заметки по тестам:
  - Unit tests на сортируемые/отображаемые поля.

### [x] T-012: Реализовать модель настроек

- Приоритет: Must.
- Зависимости: T-007.
- Описание: Определить настройки приложения: theme mode, language mode, tray behavior, logging, credential store mode, cache policy, timeouts.
- Acceptance criteria:
  - Есть domain representation для настроек.
  - Default theme mode - System.
  - Default language mode - English; System/Russian доступны как значения настройки.
  - Credential store mode по умолчанию - QtKeychain primary with fallback policy.
- Заметки по тестам:
  - Unit tests на default settings, theme mode validation и language mode validation.

### [x] T-013: Описать и реализовать SmbErrorCode mapping

- Приоритет: Must.
- Зависимости: T-007.
- Описание: Завести типизированные ошибки SMB и mapping к user-facing статусам.
- Acceptance criteria:
  - Есть коды: InvalidPath, DnsError, ServerUnavailable, ShareUnavailable, AuthenticationFailed, PermissionDenied, Timeout, ProtocolUnsupported, NetworkError, FileNotFound, AlreadyExists, DirectoryNotEmpty, OperationCancelled, Unknown.
  - Каждый код мапится на понятное сообщение пользователя.
  - Technical details проходят через sanitization перед логированием.
- Заметки по тестам:
  - Unit tests на mapping каждого error code.
  - Unit tests на отсутствие credential string в user-facing message.

## Этап 3. SQLite storage и repositories

### [ ] T-014: Спроектировать SQLite schema и migrations

- Приоритет: Must.
- Зависимости: T-008, T-012.
- Описание: Определить schema для connections, groups, settings, schema version и metadata.
- Acceptance criteria:
  - Schema хранит connection metadata без паролей.
  - Есть versioning/migration mechanism.
  - Есть unique/stability constraints для IDs.
  - `credential_ref` хранится как ссылка на секрет, а не секрет.
- Заметки по тестам:
  - Unit tests на создание новой базы.
  - Unit tests на проверку отсутствия password fields в connection table.

### [ ] T-015: Реализовать ConnectionRepository

- Приоритет: Must.
- Зависимости: T-014.
- Описание: CRUD для подключений через SQLite.
- Acceptance criteria:
  - Add, get by ID, list, update, delete работают.
  - Timestamps created/updated корректно обновляются.
  - Last opened, last error, last successful check обновляются отдельными методами или explicit update.
  - Repository не знает парольных значений.
- Заметки по тестам:
  - Unit tests CRUD на temporary SQLite DB.
  - Unit tests на отсутствие plain-text password persistence.

### [ ] T-016: Реализовать ConnectionGroupRepository

- Приоритет: Should.
- Зависимости: T-014.
- Описание: CRUD и сортировка групп/категорий подключений.
- Acceptance criteria:
  - Группы можно создать, переименовать, удалить.
  - Подключения могут ссылаться на группу.
  - Удаление группы не ломает подключения.
- Заметки по тестам:
  - Unit tests на CRUD и orphan handling.

### [ ] T-017: Реализовать SettingsRepository

- Приоритет: Must.
- Зависимости: T-014, T-012.
- Описание: Хранение и загрузка настроек приложения.
- Acceptance criteria:
  - Настройки сохраняются между запусками.
  - Неизвестные значения theme/language/credential mode приводятся к safe default.
  - Repository не зависит от Qt Widgets.
- Заметки по тестам:
  - Unit tests на default settings.
  - Unit tests на сохранение и восстановление.

### [ ] T-018: Добавить storage error mapping

- Приоритет: Must.
- Зависимости: T-015, T-017.
- Описание: Привести ошибки SQLite/repositories к общей модели ошибок.
- Acceptance criteria:
  - Ошибки DB open, migration, constraint violation и not found имеют typed result.
  - Technical details sanitized перед логированием.
  - UI может показать понятную ошибку storage layer.
- Заметки по тестам:
  - Unit tests на типовые storage failures там, где это практично.

## Этап 4. Credentials и безопасность секретов

### [ ] T-019: Определить CredentialStore interface contract

- Приоритет: Must.
- Зависимости: T-007.
- Описание: Ввести интерфейс хранения секретов с операциями save, load, update, delete и availability check.
- Acceptance criteria:
  - Интерфейс не раскрывает детали QtKeychain или vault.
  - Секреты возвращаются только по явному запросу.
  - Metadata APIs не возвращают secret value.
  - Ошибки credential store типизированы.
- Заметки по тестам:
  - Contract tests с in-memory test implementation.
  - Tests на отсутствие secret value в metadata.

### [ ] T-020: Реализовать QtKeychainCredentialStore

- Приоритет: Must.
- Зависимости: T-019.
- Описание: Primary credential store через QtKeychain.
- Acceptance criteria:
  - Save/load/update/delete работают через QtKeychain.
  - Недоступность keychain возвращает typed error.
  - Secret IDs связаны с connection metadata через `credential_ref`.
  - Логи не содержат secret values.
- Заметки по тестам:
  - Unit/contract tests с mock/fake там, где real keychain недоступен.
  - Manual smoke test на каждой целевой платформе.

### [ ] T-021: Реализовать EncryptedVaultCredentialStore fallback

- Приоритет: Must.
- Зависимости: T-019.
- Описание: Fallback credential store на локальном encrypted vault с master password.
- Acceptance criteria:
  - Vault не хранит master password в plain text.
  - Секреты хранятся в authenticated encrypted form.
  - Криптография реализована через проверенную библиотеку, не самописно.
  - Пользователь получает предупреждение при включении fallback.
- Заметки по тестам:
  - Contract tests для save/load/update/delete.
  - Unit tests на wrong master password.
  - Test fixture не содержит настоящие пароли.

### [ ] T-022: Реализовать secret handling policy

- Приоритет: Should.
- Зависимости: T-019, T-020, T-021.
- Описание: Снизить риск утечки секретов в памяти и логах.
- Acceptance criteria:
  - Секреты не передаются в UI models.
  - Временные буферы очищаются насколько разумно в C++/Qt.
  - Ошибки не содержат пароли или полные credential strings.
  - Code review checklist содержит проверку secret lifetime.
- Заметки по тестам:
  - Unit tests на sanitizer и error messages.
  - Manual review для memory lifecycle.

### [ ] T-023: Интегрировать credentials с жизненным циклом Connection

- Приоритет: Must.
- Зависимости: T-015, T-019.
- Описание: При создании/редактировании/удалении подключения сохранять и удалять секреты через `CredentialStore`.
- Acceptance criteria:
  - При password auth пароль сохраняется в credential store.
  - SQLite получает только `credential_ref`.
  - При удалении подключения связанный secret удаляется, если не используется.
  - При guest/anonymous/current user пароль не требуется.
- Заметки по тестам:
  - Unit tests с fake credential store.
  - Tests на отсутствие пароля в SQLite после save.

## Этап 5. SMB abstraction и backend

### [ ] T-024: Определить SmbClient interface

- Приоритет: Must.
- Зависимости: T-011, T-013.
- Описание: Ввести backend-agnostic interface для проверки подключения и операций с SMB.
- Acceptance criteria:
  - Interface покрывает check, list directory, create directory, delete, rename, download, upload, copy, move.
  - Interface поддерживает cancellation и progress callbacks/events там, где операция длительная.
  - Interface не зависит от Qt Widgets.
  - Interface принимает normalized connection/auth data, а не UI form data.
- Заметки по тестам:
  - Compile-level tests или unit tests через fake implementation.

### [ ] T-025: Реализовать FakeSmbClient

- Приоритет: Must.
- Зависимости: T-024.
- Описание: Test backend для сценариев без реального SMB-сервера.
- Acceptance criteria:
  - Поддерживает virtual directory tree.
  - Может симулировать permission denied, wrong credentials, timeout, DNS error, cancellation.
  - Поддерживает upload/download/delete/rename/list для тестов browser и transfer logic.
- Заметки по тестам:
  - Unit tests самого fake backend.
  - Использовать только synthetic secrets.

### [ ] T-026: Провести libsmb2 integration spike

- Приоритет: Must.
- Зависимости: T-024.
- Описание: Проверить сборку, лицензирование, API и кроссплатформенные ограничения libsmb2.
- Acceptance criteria:
  - Понятно, как подключать libsmb2 через CMake на Windows/Linux/macOS.
  - Подтверждены операции list/download/upload/delete/rename/create directory.
  - Открыты issues для неподдержанных или рискованных операций.
  - Зафиксированы ограничения current user auth.
- Заметки по тестам:
  - Manual prototype/spike может быть удален после фиксации выводов.
  - Не добавлять реальные credentials в репозиторий.

### [ ] T-027: Реализовать Libsmb2SmbClient - connection и list directory

- Приоритет: Must.
- Зависимости: T-026.
- Описание: Первая production реализация `SmbClient` на libsmb2 для подключения и чтения директории.
- Acceptance criteria:
  - Check connection различает основные ошибки.
  - List directory возвращает `RemoteFileEntry`.
  - Ошибки libsmb2 мапятся в `SmbErrorCode`.
  - Логи sanitized.
- Заметки по тестам:
  - Unit tests mapping через injectable error adapter.
  - Optional Docker Samba integration для happy path list.

### [ ] T-028: Реализовать Libsmb2SmbClient - write operations

- Приоритет: Must.
- Зависимости: T-027.
- Описание: Реализовать create directory, delete, rename, upload, download для libsmb2 backend.
- Acceptance criteria:
  - Операции возвращают typed result.
  - Permission denied, not found и already exists мапятся корректно.
  - Upload/download поддерживают progress.
  - Cancellation поддержана там, где backend позволяет, или явно эмулируется на уровне operation wrapper.
- Заметки по тестам:
  - FakeSmbClient tests обязательны.
  - Docker Samba integration tests optional profile.

### [ ] T-029: Реализовать copy/move внутри SMB и между SMB-шарами

- Приоритет: Should.
- Зависимости: T-028.
- Описание: Поддержать копирование и перемещение внутри одной SMB-шары и между разными SMB-шарами.
- Acceptance criteria:
  - Copy/move внутри одной шары работает через backend или stream copy fallback.
  - Copy между разными шарами работает с progress и cancellation.
  - Move между разными шарами выполняет copy + delete source только после успешного copy.
  - Ошибки частичного выполнения возвращаются пользователю.
- Заметки по тестам:
  - FakeSmbClient tests для same share и cross share.
  - Tests на partial failure и отсутствие удаления source при failed copy.

### [ ] T-030: Реализовать SMB error mapping для backend

- Приоритет: Must.
- Зависимости: T-027, T-028.
- Описание: Привести ошибки libsmb2 и network layer к `SmbErrorCode`.
- Acceptance criteria:
  - Wrong credentials отображается как AuthenticationFailed.
  - No rights отображается как PermissionDenied.
  - DNS error, timeout, protocol unsupported и unavailable различаются насколько позволяет backend.
  - Unknown errors не содержат секретов.
- Заметки по тестам:
  - Unit tests на mapping.
  - FakeSmbClient scenario tests.

### [ ] T-031: Реализовать ConnectivityCheckService

- Приоритет: Must.
- Зависимости: T-015, T-023, T-024, T-030.
- Описание: Use case для кнопки "Check": загрузить metadata, получить secret при необходимости, вызвать SMB backend, обновить status fields.
- Acceptance criteria:
  - Проверка запускается только по явному действию пользователя.
  - Last successful check обновляется только при успехе.
  - Last error обновляется sanitized ошибкой.
  - UI получает typed status.
- Заметки по тестам:
  - Unit tests с FakeSmbClient: server unavailable, share unavailable, wrong credentials, permission denied, timeout, success.

## Этап 6. Async operations, progress и cancellation

### [ ] T-032: Реализовать OperationQueue

- Приоритет: Must.
- Зависимости: T-024.
- Описание: Общая очередь длительных операций с состояниями, прогрессом и отменой.
- Acceptance criteria:
  - Операции выполняются вне UI thread.
  - Есть состояния queued, running, completed, failed, cancelled.
  - Progress events доставляются в UI-safe manner.
  - Cancellation token передается в backend operations.
- Заметки по тестам:
  - Unit tests на порядок выполнения.
  - Unit tests на cancellation.
  - Unit tests на progress emission.

### [ ] T-033: Реализовать TransferManager

- Приоритет: Must.
- Зависимости: T-024, T-032.
- Описание: Центральный сервис для upload, download, open via temp cache, copy, move.
- Acceptance criteria:
  - Download и upload идут через `OperationQueue`.
  - Copy/move используют SMB backend и/или stream fallback.
  - Каждая операция имеет progress и cancellation.
  - TransferManager не зависит от widgets.
- Заметки по тестам:
  - FakeSmbClient tests: upload/download/copy/move.
  - Tests на cancellation и failed operation cleanup.

### [ ] T-034: Реализовать временный кэш файлов

- Приоритет: Must.
- Зависимости: T-033, T-017.
- Описание: Управление локальным временным кэшем для открытия файлов через системное приложение и preview.
- Acceptance criteria:
  - Файл скачивается в app-specific temp/cache directory.
  - Cache path не содержит секретов.
  - Есть policy очистки.
  - Повторное открытие может переиспользовать актуальный cache entry, если это безопасно.
- Заметки по тестам:
  - Unit tests на cache path generation.
  - Unit tests на cleanup policy.

### [ ] T-035: Реализовать открытие файла через системное приложение

- Приоритет: Must.
- Зависимости: T-033, T-034.
- Описание: Двойной клик скачивает файл во временный кэш и открывает его системным приложением.
- Acceptance criteria:
  - Открытие не блокирует UI.
  - Ошибка скачивания показывает user-facing message.
  - Открытие локального файла из cache идет через platform adapter.
  - В логах нет credentials.
- Заметки по тестам:
  - Unit tests с fake platform opener.
  - UI smoke test на вызов open flow без реального приложения.

## Этап 7. UI shell и управление подключениями

### [ ] T-036: Реализовать MainWindow layout

- Приоритет: Must.
- Зависимости: T-004.
- Описание: Создать основное окно Qt Widgets с областями: connections panel, toolbar/filter, browser area, status/progress area.
- Acceptance criteria:
  - Главное окно создается без зависаний.
  - Layout соответствует PRD.
  - Browser area может показывать placeholder при отсутствии подключения.
  - Status area готова принимать статусы операций.
  - User-facing строки shell UI идут через Qt translation workflow; исходный текст - английский.
- Заметки по тестам:
  - UI smoke test на создание окна.

### [ ] T-037: Реализовать ConnectionsPanel

- Приоритет: Must.
- Зависимости: T-015, T-016, T-036.
- Описание: Панель списка сохраненных SMB-доступов и групп.
- Acceptance criteria:
  - Список подключений загружается из repository через service layer.
  - Есть фильтр по названию, server, share, group, favorite.
  - Доступны действия Add/Edit/Delete/Check/Connect/Copy path.
  - UI не обращается напрямую к SQLite.
- Заметки по тестам:
  - Unit tests для presenter/view-model logic, если выделено.
  - UI smoke test на отображение fake connections.

### [ ] T-038: Реализовать ConnectionDialog

- Приоритет: Must.
- Зависимости: T-009, T-010, T-023.
- Описание: Диалог добавления и редактирования SMB-доступа.
- Acceptance criteria:
  - Поддерживает поля из PRD.
  - Показывает preview normalized URI.
  - Валидирует путь и username/domain до сохранения.
  - Password field показывается только для password auth.
  - Guest/anonymous/current user не требуют пароля.
  - Все labels, validation messages и warnings переводимы; основной текст - английский.
- Заметки по тестам:
  - Unit tests на dialog validation logic.
  - UI smoke test add/edit flow.

### [ ] T-039: Реализовать delete connection flow

- Приоритет: Must.
- Зависимости: T-037, T-038, T-023.
- Описание: Удаление подключения из UI с подтверждением и очисткой связанного секрета.
- Acceptance criteria:
  - Delete требует подтверждения.
  - Secret удаляется, если не используется.
  - Ошибка удаления secret/repository показывается пользователю.
  - Удаление обновляет список без перезапуска приложения.
- Заметки по тестам:
  - Unit tests с fake repository и fake credential store.
  - UI smoke test delete flow.

### [ ] T-040: Реализовать connect/open flow

- Приоритет: Must.
- Зависимости: T-031, T-037.
- Описание: По кнопке "Connect" загрузить учетные данные, открыть SMB-шару и передать контекст в browser widget.
- Acceptance criteria:
  - Подключение выполняется асинхронно.
  - Last opened обновляется при успешном открытии.
  - Ошибки отображаются в status area и сохраняются sanitized.
  - UI остается responsive.
- Заметки по тестам:
  - FakeSmbClient tests на success и failure.
  - UI smoke test connect flow.

## Этап 8. Встроенный SMB browser

### [ ] T-041: Реализовать RemoteFileModel

- Приоритет: Must.
- Зависимости: T-011.
- Описание: Собственная Qt model для отображения SMB entries.
- Acceptance criteria:
  - Модель основана на `QAbstractItemModel` или `QAbstractTableModel`.
  - Не используется `QFileSystemModel` для SMB.
  - Колонки минимум: name, type, size, modified date.
  - Модель обновляется из service layer data.
  - Модель поддерживает множественный выбор на уровне view.
- Заметки по тестам:
  - Unit tests на row/column counts и data roles.
  - UI smoke test displays remote entries.

### [ ] T-042: Реализовать RemoteBrowserWidget navigation

- Приоритет: Must.
- Зависимости: T-024, T-032, T-041.
- Описание: Браузер текущей SMB-папки с toolbar: back, forward, up, refresh.
- Acceptance criteria:
  - Открытие папки загружает entries асинхронно.
  - Back/forward/up корректно управляют history stack.
  - Refresh перезагружает текущую папку.
  - Loading/error/empty states отображаются явно.
- Заметки по тестам:
  - FakeSmbClient tests на open folder, back, up, refresh.
  - UI smoke test на basic navigation.

### [ ] T-043: Реализовать поиск файлов в текущей папке

- Приоритет: Must.
- Зависимости: T-041, T-042.
- Описание: Локальный поиск/фильтр по уже загруженным entries текущей папки.
- Acceptance criteria:
  - Поиск фильтрует отображаемый список без нового SMB-запроса.
  - Фильтр сбрасывается при переходе в другую папку или ведет себя предсказуемо.
  - Поиск не блокирует UI.
- Заметки по тестам:
  - Unit tests на filter logic.
  - UI smoke test на отображение filtered entries.

### [ ] T-044: Реализовать создание папки, удаление и переименование из browser

- Приоритет: Must.
- Зависимости: T-028, T-032, T-042.
- Описание: UI и service flow для mkdir, delete и rename.
- Acceptance criteria:
  - Create folder обновляет текущую директорию.
  - Delete требует подтверждения.
  - Rename валидирует новое имя.
  - Permission denied/not found показываются пользователю.
  - Все операции асинхронны.
- Заметки по тестам:
  - FakeSmbClient tests: create, delete, rename, permission denied.
  - UI smoke test на model refresh после операции.

### [ ] T-045: Реализовать download/upload из browser

- Приоритет: Must.
- Зависимости: T-033, T-042.
- Описание: UI flow для скачивания локально и загрузки файла на SMB.
- Acceptance criteria:
  - Download выбирает локальную папку/путь и показывает progress.
  - Upload выбирает локальный файл и показывает progress.
  - Cancellation доступна для длительной операции.
  - Ошибки отображаются и логируются sanitized.
- Заметки по тестам:
  - FakeSmbClient tests: upload/download/timeout/cancellation.
  - Unit tests на progress state.

### [ ] T-046: Реализовать copy/move внутри SMB и между SMB-шарами в UI

- Приоритет: Should.
- Зависимости: T-029, T-033, T-042.
- Описание: UI и service flow для копирования/перемещения выбранных файлов внутри SMB и между подключениями.
- Acceptance criteria:
  - Multiple selection поддерживается.
  - Destination chooser позволяет выбрать папку.
  - Progress aggregate отображается в status area.
  - Partial failures показаны пользователю.
- Заметки по тестам:
  - FakeSmbClient tests на cross-share copy/move.
  - Tests на cancellation и partial failure.

### [ ] T-047: Реализовать drag-and-drop local to SMB

- Приоритет: Should.
- Зависимости: T-033, T-042, T-045.
- Описание: Drag-and-drop файлов из локальной системы в текущую SMB-папку.
- Acceptance criteria:
  - Drop локальных файлов запускает upload operations.
  - Multiple files поддерживаются.
  - Progress и cancellation доступны.
  - Ошибки показываются по каждому failed item или batch summary.
- Заметки по тестам:
  - UI smoke/integration test на drop event с temporary files, если инфраструктура позволяет.
  - FakeSmbClient tests для batch upload.

### [ ] T-048: Реализовать drag-and-drop SMB to desktop

- Приоритет: Should.
- Зависимости: T-033, T-034, T-042.
- Описание: Drag SMB entries наружу как локальные временные файлы для desktop/file manager.
- Acceptance criteria:
  - Drag запускает download во временный location или delayed transfer strategy.
  - Пользователь видит progress, если нужно скачать большой файл.
  - Cache cleanup policy не удаляет файл раньше завершения drag/drop.
  - Платформенные ограничения описаны.
- Заметки по тестам:
  - Manual tests на Windows/Linux/macOS.
  - Unit tests cache lifetime policy.

### [ ] T-049: Реализовать PreviewService для текста и изображений

- Приоритет: Should.
- Зависимости: T-033, T-034.
- Описание: Встроенный preview для текстовых файлов и изображений как отдельный модуль.
- Acceptance criteria:
  - PreviewService определяет поддерживаемые типы.
  - Файл скачивается в cache и отображается без блокировки UI.
  - Unsupported files предлагают открыть системным приложением.
  - Preview не зависит от конкретного SMB backend.
- Заметки по тестам:
  - Unit tests на type detection.
  - UI smoke test preview text/image через fake downloaded file.

### [ ] T-050: Реализовать рекурсивный поиск по SMB-шаре

- Приоритет: Could.
- Зависимости: T-032, T-042.
- Описание: Опциональный рекурсивный поиск по SMB-шаре с ограничением глубины, progress и cancellation.
- Acceptance criteria:
  - Функция выключена или помечена experimental, если не готова к первой версии.
  - Есть ограничение глубины/количества результатов.
  - Есть cancellation.
  - Сетевой шум контролируем и видим пользователю.
- Заметки по тестам:
  - FakeSmbClient tests на recursion, depth limit, cancellation.

## Этап 9. Logging, theme, settings и tray

### [ ] T-051: Реализовать Logger и LogSanitizer

- Приоритет: Must.
- Зависимости: T-007.
- Описание: Централизованное логирование в файл с обязательной sanitization.
- Acceptance criteria:
  - Все логи проходят через `LogSanitizer`.
  - Файл логов пишется в platform-appropriate directory.
  - Есть уровни debug/info/warn/error и категории.
  - Correlation ID доступен для операций.
  - Пароли, токены, master password и credential strings маскируются.
- Заметки по тестам:
  - Unit tests `LogSanitizer` на known secret values.
  - Tests на отсутствие пароля в логах import/export/check/connect.

### [ ] T-052: Реализовать LogViewer

- Приоритет: Must.
- Зависимости: T-051, T-036.
- Описание: Окно или панель журнала внутри приложения.
- Acceptance criteria:
  - Пользователь может открыть журнал из UI.
  - Журнал показывает sanitized log entries.
  - Есть фильтр по уровню или поиску, если это не задерживает первую версию.
  - Viewer не показывает raw secret values.
- Заметки по тестам:
  - UI smoke test open log viewer.
  - Unit tests на feeding sanitized entries.

### [ ] T-053: Реализовать ThemeManager

- Приоритет: Must.
- Зависимости: T-012, T-017.
- Описание: Управление темой System / Light / Dark.
- Acceptance criteria:
  - Default mode - System.
  - User selection сохраняется.
  - Смена темы применяется без перезапуска там, где практично.
  - Theme logic отделена от конкретных dialogs.
- Заметки по тестам:
  - Unit tests на theme/settings logic.
  - UI smoke test на переключение режима.

### [ ] T-054: Реализовать LocalizationManager

- Приоритет: Must.
- Зависимости: T-012, T-017.
- Описание: Управление языком интерфейса: English как основной язык, Russian как дополнительный перевод, System как режим следования поддержанной системной локали.
- Acceptance criteria:
  - Default language mode - English.
  - Поддержаны значения System, English, Russian.
  - Для неподдержанной системной локали используется English fallback.
  - User-facing строки загружаются через Qt i18n workflow (`tr()`, `.ts`, `.qm` или эквивалент).
  - Переключение языка применяется без перезапуска там, где это практично; ограничения явно показаны пользователю.
  - Infrastructure layer возвращает error codes/sanitized details, а не локализованные строки.
- Заметки по тестам:
  - Unit tests на language fallback и сохранение настройки.
  - UI smoke test на переключение English/Russian для главного окна и SettingsDialog.
  - Static/review check: новые user-facing строки не добавляются как непереводимые literals.

### [ ] T-055: Реализовать SettingsDialog

- Приоритет: Must.
- Зависимости: T-017, T-053, T-051, T-054.
- Описание: Диалог настроек приложения.
- Acceptance criteria:
  - Настройки темы, языка интерфейса, tray behavior, logging, credential store, cache policy доступны пользователю.
  - Изменения сохраняются через SettingsRepository.
  - Выбор языка содержит System, English, Russian.
  - Неверные настройки не приводят к падению.
- Заметки по тестам:
  - UI smoke test open/save settings.
  - Unit tests на validation logic, включая language mode.

### [ ] T-056: Реализовать TrayController

- Приоритет: Should.
- Зависимости: T-036, T-037, T-053.
- Описание: Системный трей, tray menu и lifecycle behavior.
- Acceptance criteria:
  - Закрытие окна сворачивает в трей, если включена настройка.
  - "Exit" реально завершает приложение.
  - Из трея можно открыть главное окно.
  - В tray menu есть быстрый список избранных подключений.
  - Уведомления об ошибках подключения показываются, если поддержаны платформой.
- Заметки по тестам:
  - Manual tests на Windows/Linux/macOS.
  - Unit tests для tray menu model через fake favorites.

### [ ] T-057: Реализовать status/progress area

- Приоритет: Must.
- Зависимости: T-032, T-036, T-051.
- Описание: Нижняя панель статуса подключения, последней ошибки и прогресса операций.
- Acceptance criteria:
  - Показывает статус текущего подключения.
  - Показывает последнюю sanitized ошибку.
  - Показывает active operations и progress.
  - Позволяет отменить cancellable operation.
- Заметки по тестам:
  - UI smoke test на progress updates.
  - Unit tests на mapping operation state to UI state, если выделено.

## Этап 10. Import/export

### [ ] T-058: Реализовать формат безопасного экспорта без паролей

- Приоритет: Must.
- Зависимости: T-015, T-016, T-051.
- Описание: Экспорт metadata подключений без паролей по умолчанию.
- Acceptance criteria:
  - Формат экспортного файла версионирован.
  - Экспорт включает metadata, groups, comments, favorites и normalized paths.
  - Экспорт не включает password fields, token fields, credential store data или master password.
  - Логи экспортной операции sanitized.
- Заметки по тестам:
  - Unit tests `ImportExportService`: normal export does not contain password fields.
  - Unit tests: exported text does not contain known secret values.
  - Log tests: export logs do not contain secrets.

### [ ] T-059: Реализовать импорт подключений

- Приоритет: Must.
- Зависимости: T-015, T-016, T-023, T-058.
- Описание: Импорт версионированного файла подключений.
- Acceptance criteria:
  - Импорт без паролей создает metadata и требует заполнить credentials позже.
  - Импорт валидирует paths через `PathNormalizer`.
  - Duplicate handling определен: skip, replace или create copy.
  - Ошибки импорта показываются пользователю с указанием записи без секретов.
- Заметки по тестам:
  - Unit tests на valid/invalid import.
  - Unit tests на duplicate handling.
  - Tests на отсутствие записи секретов в логи.

### [ ] T-060: Реализовать опасный экспорт с plain-text паролями

- Приоритет: Must.
- Зависимости: T-019, T-023, T-058.
- Описание: Отдельная ручная операция экспорта с паролями в незашифрованном виде.
- Acceptance criteria:
  - Опция выключена по умолчанию.
  - Пользователь видит сильное предупреждение о plain-text паролях.
  - Требуется отдельное явное подтверждение.
  - Экспорт с паролями доступен только после успешного чтения секретов из `CredentialStore`.
  - Логи не содержат экспортированные пароли.
- Заметки по тестам:
  - Unit tests: passwords included only when explicit option is set.
  - Unit tests: no passwords in default export.
  - UI smoke test на confirmation flow.
  - LogSanitizer tests на secret values from export.

### [ ] T-061: Реализовать UI для Import/Export

- Приоритет: Must.
- Зависимости: T-058, T-059, T-060, T-036.
- Описание: Кнопки и dialogs для импорта и экспорта в основном окне.
- Acceptance criteria:
  - Import и Export доступны из верхней панели.
  - Default export не содержит паролей.
  - Dangerous export требует отдельного подтверждения.
  - Результат операции показывается пользователю.
- Заметки по тестам:
  - UI smoke tests import/export flow.
  - Security smoke test на отсутствие пароля в default exported file.

## Этап 11. Расширенные тесты и качество

### [x] T-062: Покрыть PathNormalizer unit tests

- Приоритет: Must.
- Зависимости: T-009, T-010.
- Описание: Полный набор unit tests для нормализации путей и учетных данных.
- Acceptance criteria:
  - Покрыты `\\server\share`, `smb://server/share`, `server/share`.
  - Покрыты `DOMAIN\user`, `user@domain`, guest, anonymous, current user.
  - Покрыты invalid cases.
- Заметки по тестам:
  - Использовать synthetic values.

### [ ] T-063: Покрыть repositories unit tests

- Приоритет: Must.
- Зависимости: T-015, T-016, T-017.
- Описание: Unit tests для SQLite repositories.
- Acceptance criteria:
  - ConnectionRepository CRUD покрыт.
  - SettingsRepository покрыт.
  - Group repository покрыт, если реализован.
  - Tests подтверждают отсутствие plain-text password в SQLite.
- Заметки по тестам:
  - Temporary DB per test.

### [ ] T-064: Покрыть CredentialStore contract tests

- Приоритет: Must.
- Зависимости: T-019, T-020, T-021.
- Описание: Общий набор тестов для реализаций credential store.
- Acceptance criteria:
  - Save/load/update/delete проходят для fake/in-memory store.
  - QtKeychain store проходит contract tests там, где окружение поддерживает.
  - Vault store проходит contract tests.
  - Wrong master password covered.
- Заметки по тестам:
  - Не использовать настоящие пароли.
  - Synthetic secrets не логируются.

### [ ] T-065: Покрыть ImportExportService tests

- Приоритет: Must.
- Зависимости: T-058, T-059, T-060.
- Описание: Unit tests для import/export, включая security cases.
- Acceptance criteria:
  - Default export excludes passwords.
  - Dangerous export includes passwords only with explicit option.
  - Import validates schema version.
  - Import handles invalid paths and duplicates.
- Заметки по тестам:
  - Known secret values должны отсутствовать в default export и logs.

### [ ] T-066: Покрыть LogSanitizer tests

- Приоритет: Must.
- Зависимости: T-051.
- Описание: Unit tests на sanitization секретов.
- Acceptance criteria:
  - Маскируются known passwords.
  - Маскируются credential-like URI/strings.
  - Маскируются token-like values.
  - Sanitizer не ломает обычные сообщения.
- Заметки по тестам:
  - Добавить regression cases из найденных bugs.

### [ ] T-067: Покрыть FakeSmbClient scenario tests

- Приоритет: Must.
- Зависимости: T-025, T-033, T-042, T-044, T-045.
- Описание: Тесты SMB сценариев без реального сервера.
- Acceptance criteria:
  - List directory.
  - Open folder.
  - Upload/download.
  - Delete.
  - Rename.
  - Permission denied.
  - Wrong credentials.
  - Timeout.
  - Cancellation.
- Заметки по тестам:
  - Все tests используют fake data и synthetic credentials.

### [ ] T-068: Добавить UI smoke tests

- Приоритет: Must.
- Зависимости: T-036, T-037, T-038, T-041, T-042, T-054.
- Описание: Минимальные UI smoke tests для основных экранов и flow.
- Acceptance criteria:
  - Basic window creation.
  - Add/edit/delete connection flow.
  - Browser model displays remote entries.
  - Settings dialog opens.
  - Language can be switched between English and Russian.
  - Log viewer opens.
- Заметки по тестам:
  - Не требовать реального SMB-сервера.
  - Использовать fake services.

### [ ] T-069: Добавить optional Docker Samba integration tests

- Приоритет: Should.
- Зависимости: T-027, T-028, T-030.
- Описание: Интеграционные тесты с Samba в Docker, отключенные по умолчанию.
- Acceptance criteria:
  - Есть отдельный profile/label для запуска.
  - Тесты не запускаются в обычном unit test run.
  - CI Linux может запускать их отдельно.
  - Test credentials generated и не являются настоящими секретами.
- Заметки по тестам:
  - Покрыть connect, list, upload, download, rename, delete.

### [ ] T-070: Добавить thread-safety и cancellation tests

- Приоритет: Should.
- Зависимости: T-032, T-033.
- Описание: Проверить, что операции корректно отменяются и не обновляют UI из worker thread напрямую.
- Acceptance criteria:
  - Cancellation не приводит к use-after-free operation state.
  - Progress после cancellation не ломает UI state.
  - Result delivery происходит через UI-safe mechanism.
- Заметки по тестам:
  - Unit tests OperationQueue.
  - Stress-like tests на несколько операций.

## Этап 12. Packaging и distribution

### [ ] T-071: Подготовить Windows packaging

- Приоритет: Should.
- Зависимости: T-004, T-020, T-027.
- Описание: Собрать Windows package или installer.
- Acceptance criteria:
  - Приложение запускается на чистой поддерживаемой Windows-системе.
  - Qt runtime, QtKeychain и libsmb2 dependencies доставлены.
  - Translation resources для English/Russian UI доставлены и загружаются.
  - SQLite база и логи создаются в platform-appropriate directories.
  - Keychain integration работает или показывает понятную ошибку.
- Заметки по тестам:
  - Manual smoke: add connection, save credential, reopen app, list SMB via test server.

### [ ] T-072: Подготовить Linux packaging

- Приоритет: Should.
- Зависимости: T-004, T-020, T-027, T-069.
- Описание: Подготовить Linux package strategy: AppImage, deb или rpm.
- Acceptance criteria:
  - Приложение запускается на целевом Linux окружении.
  - libsmb2 и QtKeychain dependencies доступны.
  - Translation resources для English/Russian UI доставлены и загружаются.
  - Secret Service/KWallet ограничения описаны.
  - Optional Docker Samba integration tests могут выполняться в Linux CI.
- Заметки по тестам:
  - Manual smoke на окружении с доступным keychain backend.

### [ ] T-073: Подготовить macOS packaging

- Приоритет: Should.
- Зависимости: T-004, T-020, T-027.
- Описание: Подготовить macOS app bundle/dmg.
- Acceptance criteria:
  - Приложение запускается как app bundle.
  - Qt runtime, QtKeychain и libsmb2 packaged.
  - Translation resources для English/Russian UI packaged и загружаются.
  - Keychain access работает с ожидаемыми prompts.
  - Открытие файла через системное приложение работает.
- Заметки по тестам:
  - Manual smoke на macOS.
  - Проверить tray/menu behavior отдельно.

### [ ] T-074: Подготовить release checklist

- Приоритет: Must.
- Зависимости: T-058, T-060, T-062, T-068, T-054.
- Описание: Release checklist для первой версии.
- Acceptance criteria:
  - Проверены criteria из PRD.
  - Пройдены unit tests.
  - Пройдены UI smoke tests.
  - Проверено, что English является основным языком UI, а Russian доступен через настройки.
  - Security tests подтверждают отсутствие паролей в логах и default export.
  - Known limitations задокументированы.
- Заметки по тестам:
  - Checklist должен ссылаться на конкретные test targets/profiles.

## Этап 13. Post-v1 улучшения

### [ ] T-075: Добавить encrypted export format

- Приоритет: Could.
- Зависимости: T-058, T-060, T-021.
- Описание: Альтернатива plain-text export with passwords: encrypted export file с passphrase.
- Acceptance criteria:
  - Plain-text export remains explicit dangerous operation.
  - Encrypted export использует проверенную криптографическую библиотеку.
  - Import encrypted export требует passphrase.
- Заметки по тестам:
  - Unit tests на wrong passphrase и corrupted file.

### [ ] T-076: Добавить расширенную диагностику SMB backend

- Приоритет: Could.
- Зависимости: T-030, T-051.
- Описание: Улучшить диагностические сообщения для протокола, версии SMB и server capabilities.
- Acceptance criteria:
  - Пользователь видит actionable error details без секретов.
  - Log details помогают диагностировать backend issue.
  - Нет утечки credentials.
- Заметки по тестам:
  - Unit tests sanitizer.
  - Integration tests с разными Samba settings, если практично.

### [ ] T-077: Добавить расширенное управление кэшем

- Приоритет: Could.
- Зависимости: T-034, T-055.
- Описание: UI и policies для просмотра, очистки и ограничения временного кэша.
- Acceptance criteria:
  - Пользователь может очистить cache из settings.
  - Можно задать size/age limits.
  - Cache cleanup не ломает открытые файлы.
- Заметки по тестам:
  - Unit tests cleanup policy.
  - Manual tests open file while cleanup pending.
