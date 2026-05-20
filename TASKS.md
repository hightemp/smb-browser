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

### [x] T-006: Настроить базовую CI-проверку

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

### [x] T-014: Спроектировать SQLite schema и migrations

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

### [x] T-015: Реализовать ConnectionRepository

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

### [x] T-016: Реализовать ConnectionGroupRepository

- Приоритет: Should.
- Зависимости: T-014.
- Описание: CRUD и сортировка групп/категорий подключений.
- Acceptance criteria:
  - Группы можно создать, переименовать, удалить.
  - Подключения могут ссылаться на группу.
  - Удаление группы не ломает подключения.
- Заметки по тестам:
  - Unit tests на CRUD и orphan handling.

### [x] T-017: Реализовать SettingsRepository

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

### [x] T-018: Добавить storage error mapping

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

### [x] T-019: Определить CredentialStore interface contract

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

### [x] T-020: Реализовать QtKeychainCredentialStore

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

### [x] T-021: Реализовать EncryptedVaultCredentialStore fallback

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

### [x] T-022: Реализовать secret handling policy

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

### [x] T-023: Интегрировать credentials с жизненным циклом Connection

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

### [x] T-024: Определить SmbClient interface

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

### [x] T-025: Реализовать FakeSmbClient

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

### [x] T-026: Провести libsmb2 integration spike

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

### [x] T-027: Реализовать Libsmb2SmbClient - connection и list directory

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

### [x] T-028: Реализовать Libsmb2SmbClient - write operations

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

### [x] T-029: Реализовать copy/move внутри SMB и между SMB-шарами

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

### [x] T-030: Реализовать SMB error mapping для backend

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

### [x] T-031: Реализовать ConnectivityCheckService

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

### [x] T-032: Реализовать OperationQueue

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

### [x] T-033: Реализовать TransferManager

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

### [x] T-034: Реализовать временный кэш файлов

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

### [x] T-035: Реализовать открытие файла через системное приложение

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

### [x] T-036: Реализовать MainWindow layout

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

### [x] T-037: Реализовать ConnectionsPanel

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

### [x] T-038: Реализовать ConnectionDialog

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

### [x] T-039: Реализовать delete connection flow

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

### [x] T-040: Реализовать connect/open flow

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

### [x] T-041: Реализовать RemoteFileModel

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

### [x] T-042: Реализовать RemoteBrowserWidget navigation

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

### [x] T-043: Реализовать поиск файлов в текущей папке

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

### [x] T-044: Реализовать создание папки, удаление и переименование из browser

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

### [x] T-045: Реализовать download/upload из browser

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

### [x] T-046: Реализовать copy/move внутри SMB и между SMB-шарами в UI

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

### [x] T-047: Реализовать drag-and-drop local to SMB

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

### [x] T-048: Реализовать drag-and-drop SMB to desktop

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

### [x] T-049: Реализовать PreviewService для текста и изображений

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

### [x] T-050: Реализовать рекурсивный поиск по SMB-шаре

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

## Этап 9. Logging, theme и settings

### [x] T-051: Реализовать Logger и LogSanitizer

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

### [x] T-052: Реализовать LogViewer

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

### [x] T-053: Реализовать ThemeManager

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

### [x] T-054: Реализовать LocalizationManager

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

### [x] T-055: Реализовать SettingsDialog

- Приоритет: Must.
- Зависимости: T-017, T-053, T-051, T-054.
- Описание: Диалог настроек приложения.
- Acceptance criteria:
  - Настройки темы, языка интерфейса, logging, credential store, cache policy доступны пользователю.
  - Изменения сохраняются через SettingsRepository.
  - Выбор языка содержит System, English, Russian.
  - Неверные настройки не приводят к падению.
- Заметки по тестам:
  - UI smoke test open/save settings.
  - Unit tests на validation logic, включая language mode.

### [x] T-056: Исключить системный трей из первой версии

- Приоритет: Should.
- Зависимости: T-036, T-055.
- Описание: Не включать системный трей в продуктовую область первой версии,
  чтобы не усложнять lifecycle приложения и настройки.
- Acceptance criteria:
  - В приложении нет tray icon, tray menu и close-to-tray behavior.
  - Закрытие главного окна завершает приложение.
  - В SettingsDialog нет настроек tray behavior.
  - Ошибки подключения отображаются внутри main/status/log UI.
- Заметки по тестам:
  - UI/settings tests подтверждают отсутствие tray controls.
  - Smoke: `make run` после закрытия окна возвращает управление консоли.

### [x] T-057: Реализовать status/progress area

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

### [x] T-058: Реализовать формат безопасного экспорта без паролей

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

### [x] T-059: Реализовать импорт подключений

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

### [x] T-060: Реализовать опасный экспорт с plain-text паролями

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

### [x] T-061: Реализовать UI для Import/Export

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

### [x] T-063: Покрыть repositories unit tests

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

### [x] T-064: Покрыть CredentialStore contract tests

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

### [x] T-065: Покрыть ImportExportService tests

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

### [x] T-066: Покрыть LogSanitizer tests

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

### [x] T-067: Покрыть FakeSmbClient scenario tests

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

### [x] T-068: Добавить UI smoke tests

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

### [x] T-069: Добавить optional Docker Samba integration tests

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

### [x] T-070: Добавить thread-safety и cancellation tests

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
  - Qt runtime и QtKeychain dependencies доставлены.
  - Package не содержит `libsmb2.dll`, `smbclient.exe` или Samba client runtime.
  - Translation resources для English/Russian UI доставлены и загружаются.
  - SQLite база и логи создаются в platform-appropriate directories.
  - Keychain integration работает или показывает понятную ошибку.
- Заметки по тестам:
  - Manual smoke: add connection, save credential, reopen app, list SMB via test server.
  - Подготовлен smoke script `scripts/package-smoke-windows.ps1`; выполнить на Windows перед закрытием задачи.
  - Linux workspace note: PowerShell is not installed here, so Windows smoke
    remains manual/on-runner only.
  - Подготовлен manual GitHub Actions workflow
    `.github/workflows/package-smoke.yml` для Windows portable ZIP smoke на
    чистом runner-е.
  - `scripts/package-windows.ps1` теперь собирает portable ZIP staging из
    build-tree после `windeployqt` и запускает smoke именно для созданного ZIP.
  - Workflow syntax проверен через `tmp/bin/actionlint`.
  - Windows smoke теперь проверяет наличие Qt runtime DLLs, platform plugin,
    SQLite driver, QtKeychain runtime и libsodium runtime в ZIP.
  - `scripts/package-windows.ps1` теперь собирает ZIP из чистого staging
    каталога и останавливается при ошибках `cmake`, `ctest` или `windeployqt`.

### [x] T-072: Подготовить Linux packaging

- Приоритет: Should.
- Зависимости: T-004, T-020, T-027, T-069.
- Описание: Подготовить Linux package strategy: AppImage, deb или rpm.
- Acceptance criteria:
  - Приложение запускается на целевом Linux окружении.
  - QtKeychain dependency доступна; `libsmb2` и `smbclient` не являются runtime
    dependencies.
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
  - Qt runtime и QtKeychain packaged.
  - Bundle не содержит `libsmb2`, `smbclient` или Samba client runtime.
  - Translation resources для English/Russian UI packaged и загружаются.
  - Keychain access работает с ожидаемыми prompts.
  - Открытие файла через системное приложение работает.
- Заметки по тестам:
  - Manual smoke на macOS.
  - Проверить tray/menu behavior отдельно.
  - Подготовлен smoke script `scripts/package-smoke-macos.sh`; выполнить на macOS перед закрытием задачи.
  - Linux workspace note: `bash -n scripts/package-macos.sh
    scripts/package-smoke-macos.sh` passes, but host smoke remains macOS-only.
  - Подготовлен manual GitHub Actions workflow
    `.github/workflows/package-smoke.yml` для macOS DMG smoke на чистом
    runner-е.
  - `scripts/package-macos.sh` теперь передает в smoke точный DMG/app artifact,
    созданный текущим запуском, и автоматически добавляет Homebrew `qt@5` /
    `qtkeychain` prefixes в `CMAKE_PREFIX_PATH`.
  - Workflow syntax проверен через `tmp/bin/actionlint`; macOS smoke запускается
    на `macos-15-intel`.
  - macOS smoke теперь проверяет наличие Qt frameworks, Cocoa platform plugin,
    SQLite driver, QtKeychain runtime и libsodium runtime в app bundle.
  - `scripts/package-macos.sh` теперь требует `macdeployqt` и очищает package
    directory перед запуском `cpack`, чтобы smoke проверял свежий artifact.

### [x] T-074: Подготовить release checklist

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

### [x] T-075: Добавить encrypted export format

- Приоритет: Could.
- Зависимости: T-058, T-060, T-021.
- Описание: Альтернатива plain-text export with passwords: encrypted export file с passphrase.
- Acceptance criteria:
  - Plain-text export remains explicit dangerous operation.
  - Encrypted export использует проверенную криптографическую библиотеку.
  - Import encrypted export требует passphrase.
- Заметки по тестам:
  - Unit tests на wrong passphrase и corrupted file.

### [x] T-076: Добавить расширенную диагностику SMB backend

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

### [x] T-077: Добавить расширенное управление кэшем

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

### [x] T-078: Добавить fallback для DFS SMB namespaces

- Приоритет: Should.
- Зависимости: T-027, T-030.
- Описание: Поддержать корпоративные DFS namespace пути, где `libsmb2`
  падает на `smb2_connect_share()` с `STATUS_BAD_NETWORK_NAME`, а Samba
  `smbclient` умеет получить реальный target server/share.
- Acceptance criteria:
  - `STATUS_BAD_NETWORK_NAME` при Tree Connect мапится в `ShareUnavailable`, а
    не в `DnsError`.
  - Есть backend-agnostic `DfsReferralResolver` interface.
  - Есть `smbclient`-based resolver, который не передает пароль через argv и
    удаляет временный credentials file.
  - Есть `DfsResolvingSmbClient`, который кэширует resolved target и повторяет
    операции через основной `libsmb2` backend.
  - Если `smbclient` отсутствует, пользователь получает actionable error с
    DFS hint.
- Заметки по тестам:
  - Unit tests error mapping для `STATUS_BAD_NETWORK_NAME`.
  - Unit tests parser для `smbclient -c showconnect`.
  - Unit tests fallback/retry/cache wrapper.
  - Manual safe check на `tmp/mylist.json` без вывода пароля.

## Этап 14. Regression fixes после ручного прогона

### [x] T-079: Завершать приложение при закрытии главного окна по умолчанию

- Приоритет: Must.
- Зависимости: T-055.
- Описание: Убрать неожиданное зависание `make run` после закрытия главного
  окна: clean/default конфигурация должна завершать приложение.
- Acceptance criteria:
  - `QApplication` завершает процесс при закрытии последнего окна.
  - `make run` после закрытия главного окна возвращает управление консоли.
  - Закрытие окна не перехватывается дополнительным lifecycle controller.
- Заметки по тестам:
  - Unit/UI smoke tests main window lifecycle.
  - Smoke: `make run-offscreen`.

### [x] T-080: Синхронизировать изменения открытого файла обратно на SMB

- Приоритет: Must.
- Зависимости: T-033, T-034, T-035, T-045.
- Описание: После открытия файла через системное приложение отслеживать
  локальный cache-файл и при сохранении заливать измененную версию обратно в
  исходный remote path.
- Acceptance criteria:
  - После успешного download/open создается open-file session для пары
    `local cache path -> original SMB path`.
  - Изменения cache-файла отслеживаются через file watcher с debounce.
  - Upload выполняется асинхронно и не блокирует UI.
  - Для приложений, временно блокирующих файл при сохранении, upload
    повторяется ограниченное число раз.
  - Ошибки upload отображаются через общий operation/status flow и не логируют
    секреты.
  - Поведение явно основано на событии сохранения/изменения файла, а не на
    отслеживании процесса внешнего редактора.
- Заметки по тестам:
  - UI/widget test с fake file opener: открыть remote file, изменить cache-файл,
    проверить запуск upload в исходный SMB path.
  - Regression test на отсутствие настоящих credentials в путях cache/logs
    остается частью security suite.

### [x] T-081: Удалить системный трей из кода и документации

- Приоритет: Must.
- Зависимости: T-055, T-056, T-079.
- Описание: Полностью убрать остатки tray-функциональности из runtime,
  настроек, тестов, CMake и продуктовых документов.
- Acceptance criteria:
  - `TrayController` удален из UI target.
  - `test_tray_controller` удален из test target list.
  - `ApplicationSettings` и `SettingsRepository` больше не содержат
    `closeToTray` и `showTrayNotifications`.
  - `SettingsDialog` не показывает Behavior/tray controls.
  - `main.cpp` не создает tray icon и не использует tray lifecycle wiring.
  - PRD/TASKS не требуют tray как функциональность первой версии.
- Заметки по тестам:
  - Full CTest suite.
  - `make no-smb`.
  - `make run-offscreen`.
  - Linux package smoke.

## Этап 15. Native SMB library: discovery, license и scope gate

Цель этапа: подготовить миграцию с `libsmb2`/external `smbclient` на
внутреннюю SMB-библиотеку, которая в runtime не требует `libsmb2` и не
запускает `smbclient`. Важное ограничение: Samba `source3/client/client.c` и
`source3/include/libsmbclient.h` в проверенном master snapshot имеют GPL-3-or-
later notice, поэтому любое копирование/статическое связывание Samba code path
требует отдельного license/compliance решения до реализации.

Предварительный анализ выполнен 2026-05-19 в `tmp/samba-src` на Samba commit
`66fec3d`. Файл `source3/client/client.c` содержит 6823 строки и command table
`commands[]`; `source3/wscript_build` собирает `client/smbclient` из
`client/client.c`, `client/clitar.c`, `client/dnsbrowse.c` с зависимостями
`talloc`, `CMDLINE_S3`, `smbconf`, `ndr-standard`, `SMBREADLINE`, `libsmb`,
`msrpc3`, `RPC_NDR_SRVSVC`, `cli_smb_common`, `archive`.

Архитектурное решение: выбран путь `clean-room only`. Samba можно использовать
только как reference для поведения и совместимости в `tmp`; копировать код,
структуры, таблицы команд, комментарии или переносить внутренние реализации
Samba в проект запрещено.

Подробный gate-документ: `docs/native-smb-clean-room.md`.

Продуктовые решения для native SMB migration:

- Проект переводится в open-source/GPL-compatible модель распространения.
- Требование portable binary означает: SMB engine должен быть внутри бинарника
  приложения; Qt/runtime зависимости могут оставаться в portable package или
  platform bundle.
- Поддерживаются только SMB2/SMB3. SMB1 и NetBIOS browsing не реализуются.
- Password/domain/guest/anonymous обязательны; current user/Kerberos/SSO нужен
  для проверки на Windows Server и должен быть спроектирован отдельно.
- DFS namespace support обязателен полностью: referrals, nested DFS,
  multiple targets, failover и TTL cache.
- SMB signing и SMB encryption обязательны.
- ACL, EA, chmod, chown, symlink, hardlink и notify являются Must на уровне
  библиотеки; UI может открывать эти возможности поэтапно.
- Реализация идет staged: сначала Must parity текущего приложения, затем
  расширенные library capabilities.
- Тестирование включает Docker Samba fixtures, synthetic credentials и manual
  validation на корпоративном Windows SMB/DFS server без сохранения реальных
  credentials в репозитории.

### [x] T-082: Зафиксировать результаты анализа Samba source в TASKS/notes

- Приоритет: Must.
- Зависимости: нет.
- Описание: Использовать локальный checkout `tmp/samba-src` для фиксации
  технической картины по `smbclient`: commit, лицензия, command surface,
  build dependencies, reusable internal APIs.
- Acceptance criteria:
  - Зафиксирован проверенный Samba commit и дата анализа.
  - Отдельно отмечено, что `source3/client/client.c` содержит command table
    `commands[]` и не является готовой embeddable library.
  - Зафиксировано, что `smbclient` в Samba build зависит минимум от `talloc`,
    `CMDLINE_S3`, `smbconf`, `ndr-standard`, `SMBREADLINE`, `libsmb`,
    `msrpc3`, `RPC_NDR_SRVSVC`, `cli_smb_common`, `archive`.
  - Зафиксировано, что текущий `smbclient` flow использует global state и CLI
    stdout/stderr, которые нельзя напрямую переносить в Qt service layer.
  - Сформирован список feature groups: session/auth, DFS, directory, file IO,
    metadata, copy/move, POSIX extensions, ACL/EA, notify, share browsing.
- Заметки по тестам:
  - Тестов кода нет; это documentation/research task.
  - Проверить, что notes не содержат credentials из `tmp/mylist.json`.

### [x] T-083: Принять license/compliance решение для Samba-derived code

- Приоритет: Must.
- Зависимости: T-082.
- Описание: До любого переноса Samba кода определить допустимую юридическую
  модель: GPL-compatible application distribution, commercial/legal exception
  отсутствует, либо clean-room implementation без копирования Samba code.
- Acceptance criteria:
  - Решение принято: `clean-room only`.
  - Samba-derived путь не используется; GPL-3-or-later code из Samba не
    переносится в проект и не линкуется с приложением.
  - Samba используется только как behavioral reference в `tmp`, без
    копирования кода, структур, таблиц команд, комментариев или внутренних
    реализаций.
  - Qt static linking obligations для one-binary build остаются отдельным
    packaging/license вопросом в T-084/T-111.
  - Release checklist по source offer для Samba-derived code не нужен, пока
    clean-room boundary соблюдается.
- Заметки по тестам:
  - Manual legal/compliance review gate.
  - CI может проверять наличие license files, notices и source bundle metadata.

### [x] T-084: Уточнить определение “один portable binary”

- Приоритет: Must.
- Зависимости: T-083.
- Описание: Зафиксировать, что именно означает one-binary для Windows, Linux и
  macOS: только SMB-библиотека статически внутри приложения или весь Qt/runtime
  тоже статически связан.
- Acceptance criteria:
  - Решение зафиксировано: SMB engine находится внутри бинарника приложения.
  - Qt/runtime зависимости допустимы в portable folder/app bundle/installer и
    не считаются нарушением требования.
  - Явно перечислены допустимые runtime dependencies: Qt runtime, OS
    networking, system keychain APIs, system CA/crypto facilities, platform
    plugins.
  - Полностью статический Qt не является обязательной целью этого этапа.
  - macOS app bundle считается допустимым форматом поставки.
  - Определены dependency audit commands: `ldd`, `otool -L`, `dumpbin` или
    аналогичные platform tools.
- Заметки по тестам:
  - Packaging smoke должен проверять отсутствие `libsmb2` и `smbclient`.
  - Dependency audit добавляется в CI для release profiles.

### [x] T-085: Составить feature parity matrix для внутренней SMB-библиотеки

- Приоритет: Must.
- Зависимости: T-082.
- Описание: Сопоставить текущие возможности приложения, команды `smbclient`
  и API `libsmbclient`/Samba client stack с будущим `SmbNative` API.
- Acceptance criteria:
  - Для каждой возможности указан статус: Must, Should, Could, Not planned.
  - Must покрывает текущие product features: check, list, stat, open folder,
    download, upload, mkdir, delete, rename, copy, move, DFS, symlink/reparse,
    progress, cancellation, signing, encryption.
  - Отдельно классифицированы расширенные возможности `smbclient`: ACL, EA,
    chmod/chown, hardlink/symlink, notify, server-side copy, share browsing,
    POSIX extensions, print queue, tar, message, shell command.
  - ACL, EA, chmod/chown, hardlink/symlink и notify помечены как Must для
    library API.
  - Print, tar, local shell command и message либо исключены из GUI product
    scope, либо требуют отдельного обоснования.
  - Matrix фиксирует, какие функции реализуются в library, а какие получают
    UI позже.
- Заметки по тестам:
  - Matrix становится источником test coverage checklist.
  - Для каждого Must feature должен быть запланирован unit/integration test.

## Этап 16. Native SMB library architecture и build integration

### [x] T-086: Спроектировать модуль `SmbNative` как internal library

- Приоритет: Must.
- Зависимости: T-083, T-085.
- Описание: Ввести архитектуру внутренней SMB-библиотеки, которая не зависит
  от Qt Widgets и реализует stable API для application layer.
- Acceptance criteria:
  - `SmbNative` имеет четкий public API: connection config, credentials,
    session, file handle, directory iterator, transfer callbacks, errors.
  - API не использует глобальное состояние `smbclient` и не пишет в stdout/stderr.
  - API поддерживает progress, cancellation, timeout и structured logging hooks.
  - API безопасен для использования из worker threads и явно описывает
    ownership/lifetime объектов.
  - Секреты передаются через short-lived buffers и не попадают в QString/logs.
- Заметки по тестам:
  - Compile tests на header/API.
  - Unit tests на lifecycle без реального network через fake transport layer,
    если выбран clean-room/адаптерный дизайн.

### [x] T-087: Спроектировать clean-room implementation path

- Приоритет: Must.
- Зависимости: T-083, T-086.
- Описание: На основе принятого решения `clean-room only` описать технический
  путь реализации внутренней библиотеки без переноса Samba code.
- Acceptance criteria:
  - Реализация проектируется как clean-room SMB2/3 client по открытым
    спецификациям и собственному дизайну.
  - SMB1 и NetBIOS browsing явно исключены.
  - Samba используется только как behavioral reference; копирование Samba кода,
    структур, таблиц команд, комментариев и internal implementation details
    запрещено.
  - Для выбранного варианта описаны риски: SMB dialects, DFS, auth, signing,
    encryption, Kerberos, cross-platform sockets, maintenance.
  - Зафиксирован fallback plan, если clean-room путь блокируется на Windows,
    macOS или по срокам.
  - Решение отражено в README/architecture docs до начала реализации.
- Заметки по тестам:
  - No runtime tests.
  - Review gate: нельзя начинать T-088/T-091 без утвержденного path.

### [x] T-088: Настроить reproducible source acquisition для SMB engine

- Приоритет: Must.
- Зависимости: T-087.
- Описание: Сделать воспроизводимый способ получения source для внутреннего
  SMB engine без runtime dependencies.
- Acceptance criteria:
  - Для clean-room native engine внешний source acquisition step отсутствует:
    исходники живут в `src/native_smb`.
  - CMake не скачивает Samba, `libsmb2` или другой SMB client source для
    native engine.
  - CMake не требует установленного `libsmb2` или `smbclient` для сборки
    native engine target.
  - Offline build path native engine описан для release.
  - Никакие временные checkout-ы из `tmp/` не коммитятся и не входят в source
    distribution.
  - Если позже появится внешний dependency для native engine, он должен иметь
    pinned version/checksum и отдельный license/security review.
- Заметки по тестам:
  - CI clean-clone configure test.
  - Test, что build не обращается к system `libsmb2`/`smbclient`.

### [x] T-089: Реализовать минимальный static clean-room SMB2/3 core

- Приоритет: Must.
- Зависимости: T-087, T-088.
- Описание: Реализовать минимальный статический clean-room SMB2/3 protocol
  core внутри `src/native_smb` без Samba-derived source.
- Acceptance criteria:
  - Не используется `source3/client/client.c`, Samba headers или Samba
    libraries.
  - Нет readline/history/shell command/stdin processing.
  - В build входят только clean-room protocol/session/file/DFS/auth components.
  - Static library собирается на Linux в отдельном CMake target.
  - Build artifact может линковаться в `smb-browser` без `libsmb2` и external
    `smbclient`.
  - Стартовый target `smb_browser_native_smb` расширен от scaffold до
    минимального protocol core.
- Заметки по тестам:
  - Linux build target smoke.
  - Link test проверяет отсутствие `-lsmb2`.
  - Dependency audit проверяет отсутствие `smbclient` process dependency.
  - Covered by `make native-test`, default `make build` and Linux package
    dependency smoke.

### [x] T-125: Добавить стартовый native SMB protocol test baseline

- Приоритет: Must.
- Зависимости: T-088, T-124.
- Описание: Добавить первые clean-room protocol primitives и unit tests,
  которые фиксируют бинарную основу SMB2/SMB3 engine до реализации сетевой
  session/auth логики.
- Acceptance criteria:
  - Есть static target `smb_browser_native_smb`.
  - Есть CTest label `native-unit`.
  - Есть CTest label `native-protocol`.
  - Unit tests покрывают SMB2 SYNC header encode/decode.
  - Unit tests покрывают SMB2 NEGOTIATE request builder.
  - Unit tests покрывают Direct TCP frame length encoding/decoding.
  - Unit tests проверяют signing security mode mapping.
  - Unit tests проверяют, что initial dialect policy не содержит SMB1.
  - `make native-test` запускает native tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-unit
    --output-on-failure`.

### [x] T-126: Добавить fake-transport negotiation baseline

- Приоритет: Must.
- Зависимости: T-125.
- Описание: Добавить первый state-machine слой native SMB library, который
  работает поверх scripted transport и позволяет тестировать negotiate flow без
  реального SMB-сервера.
- Acceptance criteria:
  - Есть `Transport` interface внутри `src/native_smb`.
  - Есть `Negotiator`, который строит SMB2 NEGOTIATE request, оборачивает его
    в Direct TCP frame, читает response frame и парсит NEGOTIATE response.
  - Negotiator возвращает выбранный dialect, signing requirement, encryption
    capability, max read/write и security buffer.
  - Есть CTest label `native-fake-transport`.
  - Tests покрывают successful negotiation, invalid frame, short frame и
    cancellation before transport IO.
  - Реальный network и SMB-сервер не нужны.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-fake-transport
    --output-on-failure`.

### [x] T-127: Добавить fake-transport tree connect baseline

- Приоритет: Must.
- Зависимости: T-125, T-126.
- Описание: Добавить request/response primitives и fake-transport flow для
  SMB2 TREE_CONNECT, чтобы проверять подключение к share, DFS flags и
  encryption-required flags без реального SMB-сервера.
- Acceptance criteria:
  - `Protocol` умеет строить SMB2 TREE_CONNECT request.
  - TREE_CONNECT path кодируется как UTF-16LE UNC path `\\server\share`.
  - `Protocol` умеет парсить SMB2 TREE_CONNECT response.
  - Response parser извлекает share type, share flags, capabilities,
    maximal access, DFS flags и encryption-required flag.
  - Есть `TreeConnector` поверх `Transport`.
  - Tests покрывают successful tree connect, unexpected response command и
    cancellation before transport IO.
  - `make native-test` запускает эти tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-fake-transport
    --output-on-failure`.

### [x] T-128: Добавить fake-transport session setup baseline

- Приоритет: Must.
- Зависимости: T-125, T-126.
- Описание: Добавить request/response primitives и fake-transport flow для
  SMB2 SESSION_SETUP token exchange до реализации реальной NTLM/Kerberos
  логики.
- Acceptance criteria:
  - `Protocol` умеет парсить SMB2 SESSION_SETUP response.
  - Parser извлекает response status, session id, session flags,
    security buffer и `STATUS_MORE_PROCESSING_REQUIRED`.
  - Parser определяет guest/null/encrypt-data session flags.
  - Есть `SessionSetupExchanger` поверх `Transport`.
  - Tests покрывают successful token exchange, unexpected response command и
    cancellation before transport IO.
  - `make native-test` запускает эти tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-fake-transport
    --output-on-failure`.

### [x] T-129: Добавить fake-transport directory listing baseline

- Приоритет: Must.
- Зависимости: T-125, T-127, T-128.
- Описание: Добавить минимальные protocol primitives и fake-transport flow
  для будущего `listDirectory`: `CREATE` directory handle и
  `QUERY_DIRECTORY` с `FileIdBothDirectoryInformation`.
- Acceptance criteria:
  - `Protocol` умеет строить SMB2 CREATE request для открытия каталога.
  - `Protocol` умеет парсить SMB2 CREATE response и извлекать FileId.
  - `Protocol` умеет строить SMB2 QUERY_DIRECTORY request.
  - `Protocol` умеет парсить SMB2 QUERY_DIRECTORY response с
    `FileIdBothDirectoryInformation` entries.
  - Parser извлекает name, timestamps, size, attributes, EA/reparse field,
    file id, directory flag и reparse flag.
  - Есть `DirectoryLister` поверх `Transport`, который выполняет
    `CREATE -> QUERY_DIRECTORY`.
  - Tests покрывают successful listing, invalid create response и cancellation
    before transport IO.
  - `make native-test` запускает эти tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-contract
    --output-on-failure`.

### [x] T-130: Добавить CLOSE lifecycle baseline для native handles

- Приоритет: Must.
- Зависимости: T-129.
- Описание: Добавить SMB2 CLOSE primitives и fake-transport flow, чтобы
  directory/file handles закрывались тестируемо и не оставались скрытым
  lifecycle debt.
- Acceptance criteria:
  - `Protocol` умеет строить SMB2 CLOSE request.
  - `Protocol` умеет парсить SMB2 CLOSE response.
  - Parser извлекает status, flags, timestamps, allocation size, end-of-file,
    attributes и признак post-query attributes.
  - Есть `CloseExchanger` поверх `Transport`.
  - `DirectoryLister` выполняет `CREATE -> QUERY_DIRECTORY -> CLOSE` на
    successful listing.
  - Tests покрывают successful close, unexpected response command и
    cancellation before transport IO.
  - `make native-test` запускает эти tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-fake-transport
    --output-on-failure`.

### [x] T-131: Добавить fake-transport file read baseline

- Приоритет: Must.
- Зависимости: T-130.
- Описание: Добавить SMB2 READ primitives и read-once fake-transport flow,
  чтобы будущие download/open-file операции имели тестируемую основу без
  реального SMB-сервера.
- Acceptance criteria:
  - `Protocol` умеет строить SMB2 READ request.
  - `Protocol` умеет парсить SMB2 READ response и извлекать data buffer,
    data offset, data remaining и flags.
  - `ReadExchanger` выполняет READ поверх `Transport`.
  - `FileReader` выполняет `CREATE file -> READ -> CLOSE`.
  - Tests покрывают successful read, unexpected response command, invalid read
    response in composed flow и cancellation before transport IO.
  - `make native-test` запускает эти tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-contract
    --output-on-failure`.

### [x] T-132: Добавить fake-transport file write baseline

- Приоритет: Must.
- Зависимости: T-130.
- Описание: Добавить SMB2 WRITE primitives и write-once fake-transport flow,
  чтобы будущие upload/open-file-save операции имели тестируемую основу без
  реального SMB-сервера.
- Acceptance criteria:
  - `Protocol` умеет строить SMB2 WRITE request.
  - `Protocol` умеет парсить SMB2 WRITE response и извлекать count, remaining
    и channel info fields.
  - `WriteExchanger` выполняет WRITE поверх `Transport`.
  - `FileWriter` выполняет `CREATE file -> WRITE -> CLOSE`.
  - Tests покрывают successful write, unexpected response command, invalid
    write response in composed flow и cancellation before transport IO.
  - `make native-test` запускает эти tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-contract
    --output-on-failure`.

### [x] T-133: Добавить fake-transport mkdir/delete/rename baseline

- Приоритет: Must.
- Зависимости: T-130.
- Описание: Добавить SMB2 SET_INFO primitives и composed object operation flow,
  чтобы будущие операции create folder, delete и rename имели тестируемую
  основу без реального SMB-сервера.
- Acceptance criteria:
  - `Protocol` умеет строить SMB2 SET_INFO request.
  - `Protocol` умеет парсить SMB2 SET_INFO response.
  - `Protocol` умеет строить `FileDispositionInformation` для delete-on-close.
  - `Protocol` умеет строить `FileRenameInformation for SMB2` для rename.
  - `SetInfoExchanger` выполняет SET_INFO поверх `Transport`.
  - `RemoteObjectOperator` выполняет `CREATE directory -> CLOSE`,
    `CREATE object -> SET_INFO disposition -> CLOSE` и
    `CREATE object -> SET_INFO rename -> CLOSE`.
  - Tests покрывают successful mkdir/delete/rename, unexpected response command,
    invalid set-info response in composed flow и cancellation before transport
    IO.
  - `make native-test` запускает эти tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -L native-contract
    --output-on-failure`.

### [x] T-134: Добавить baseline C++ facade поверх native SMB primitives

- Приоритет: Must.
- Зависимости: T-129, T-131, T-132, T-133.
- Описание: Добавить первый facade layer для native SMB library, который
  скрывает `treeId`, `sessionId`, message id allocation и composed
  protocol/exchanger details от будущего application backend.
- Acceptance criteria:
  - Есть `NativeSmbSession` поверх injectable `Transport`.
  - Facade предоставляет методы `listDirectory`, `readFileOnce`,
    `writeFileOnce`, `createDirectory`, `deleteObject`, `renameObject`.
  - Facade возвращает native-level result structs без raw packet buffers и без
    exposing internal `FileId` в public operation results.
  - Message id allocation инкапсулирована внутри facade.
  - Tests покрывают routing composed operations, message id advancement,
    result mapping и error propagation.
  - `make native-test` запускает facade tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R native_smb_session
    --output-on-failure`.

### [x] T-135: Добавить fake-transport stat/query-info baseline

- Приоритет: Must.
- Зависимости: T-130, T-134.
- Описание: Добавить SMB2 QUERY_INFO primitives и composed stat flow, чтобы
  future metadata mapping мог получать timestamps, attributes, size,
  allocation size, link count, delete-pending и directory flag без реального
  SMB-сервера.
- Acceptance criteria:
  - `Protocol` умеет строить SMB2 QUERY_INFO request.
  - `Protocol` умеет парсить SMB2 QUERY_INFO response.
  - `Protocol` умеет парсить `FileBasicInformation`.
  - `Protocol` умеет парсить `FileStandardInformation`.
  - `QueryInfoExchanger` выполняет QUERY_INFO поверх `Transport`.
  - `RemoteStatReader` выполняет
    `CREATE -> QUERY_INFO(FileBasicInformation) ->
    QUERY_INFO(FileStandardInformation) -> CLOSE`.
  - `NativeSmbSession` exposes `statObject`.
  - Tests покрывают successful stat, unexpected response command, invalid
    query-info response in composed flow и cancellation before transport IO.
  - `make native-test` запускает эти tests без `libsmb2`.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R
    'native_smb_(query_info_exchanger|remote_stat_reader|session)'
    --output-on-failure`.

### [x] T-136: Добавить native NTSTATUS mapping baseline

- Приоритет: Must.
- Зависимости: T-125.
- Описание: Централизованно мапить SMB2/NTSTATUS к typed native errors и
  возвращать failure до парсинга success body, если сервер вернул ошибку.
- Acceptance criteria:
  - Есть `errorCodeForNtStatus` и `ntStatusName`.
  - Authentication, permission, share unavailable, file not found, already
    exists, directory not empty, invalid path, timeout and unsupported protocol
    statuses мапятся отдельно.
  - Response decoders не пытаются парсить fixed success body при error status.
  - `SESSION_SETUP` допускает `STATUS_MORE_PROCESSING_REQUIRED`.
  - `QUERY_DIRECTORY` treats `STATUS_NO_MORE_FILES` as empty successful result.
  - Tests покрывают mapping и early-failure behavior.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R native_smb_protocol
    --output-on-failure`.

### [x] T-137: Добавить Direct TCP transport baseline

- Приоритет: Must.
- Зависимости: T-125, T-136.
- Описание: Добавить cross-platform socket transport для SMB Direct TCP framing
  без `libsmb2` и без external `smbclient`.
- Acceptance criteria:
  - `DirectTcpTransport` implements `Transport`.
  - Transport resolves host, opens TCP connection, sends complete Direct TCP
    frame and reads complete response frame.
  - Timeout and cancellation are checked around connect/send/receive loops.
  - Socket errors map to native typed errors.
  - Windows build links `ws2_32`; POSIX build uses system sockets.
  - Loopback test verifies split response frame and cancellation before socket
    open.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R
    native_smb_direct_tcp_transport --output-on-failure`.

### [x] T-138: Добавить native connection lifecycle skeleton

- Приоритет: Must.
- Зависимости: T-126, T-128, T-127, T-134, T-137.
- Описание: Добавить connection owner и connector flow, который выполняет
  `NEGOTIATE -> SESSION_SETUP token exchange -> TREE_CONNECT` поверх
  injectable `Transport`.
- Acceptance criteria:
  - Есть `NativeSmbConnection`, владеющий `Transport` и `NativeSmbSession`.
  - Есть `NativeSmbConnector`.
  - Auth token generation вынесен в `SessionSetupTokenProvider` interface.
  - Connector поддерживает multi-round `SESSION_SETUP` до success.
  - После `TREE_CONNECT` facade получает tree id, session id и следующий
    message id.
  - Tests покрывают happy path, token provider failure и request sequencing.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R native_smb_connector
    --output-on-failure`.

### [x] T-139: Добавить clean-room NTLMSSP message baseline

- Приоритет: Must.
- Зависимости: T-094, T-138.
- Описание: Реализовать собственные NTLMSSP NEGOTIATE, CHALLENGE decode и
  AUTHENTICATE message builders без Samba/libsmb2 code.
- Acceptance criteria:
  - NEGOTIATE_MESSAGE строится с domain/workstation security buffers.
  - CHALLENGE_MESSAGE декодируется с flags, server challenge and target info.
  - AUTHENTICATE_MESSAGE строится с LM/NT responses, domain, user,
    workstation, flags and version fields.
  - Некорректная signature/type rejected as typed protocol error.
  - Tests покрывают offsets/security buffers and invalid input.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R native_smb_ntlm_messages
    --output-on-failure`.

### [x] T-140: Добавить NTLMv2 crypto и token provider baseline

- Приоритет: Must.
- Зависимости: T-139.
- Описание: Реализовать clean-room MD4/MD5/HMAC-MD5, NT hash, NTOWFv2,
  NTLMv2 response generation and `SessionSetupTokenProvider` for password auth.
- Acceptance criteria:
  - MD4/MD5/HMAC-MD5 проходят known vectors.
  - NT hash, NTOWFv2, NTLMv2 proof, LMv2 response and session base key
    проходят deterministic vectors.
  - `NtlmV2TokenProvider` генерирует initial/next tokens and exposes session
    base key for signing.
  - Password bytes не попадают в generated token tests or logs.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R
    'native_smb_ntlm_(crypto|v2_token_provider)' --output-on-failure`.

### [x] T-141: Добавить SPNEGO wrapper/unwrapper для NTLMSSP

- Приоритет: Must.
- Зависимости: T-139, T-140.
- Описание: SMB servers обычно передают NTLMSSP через GSS-SPNEGO; добавить
  minimal DER encoder/decoder для NegTokenInit/NegTokenResp без external
  GSS/Samba dependency.
- Acceptance criteria:
  - Client initial token wraps NTLMSSP negotiate in SPNEGO NegTokenInit.
  - Client authenticate wraps NTLMSSP authenticate in SPNEGO NegTokenResp.
  - Server challenge NTLMSSP token extracts from SPNEGO response.
  - Raw NTLM mode remains available for unit/fake harnesses.
  - Malformed DER returns typed protocol error.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R
    'native_smb_spnego_token|native_smb_ntlm_v2_token_provider'
    --output-on-failure`.

### [x] T-142: Добавить SMB2.0.2/2.1 signing baseline

- Приоритет: Must.
- Зависимости: T-122, T-140.
- Описание: Реализовать HMAC-SHA256 signing for SMB2.0.2/2.1 messages и
  подключить его в connector после successful SESSION_SETUP.
- Acceptance criteria:
  - SHA-256 and HMAC-SHA256 проходят known vectors.
  - SMB2 direct TCP frame signing sets `SMB2_FLAGS_SIGNED`, clears signature
    before digest and writes first 16 digest bytes.
  - Signed response verification detects valid/invalid signature.
  - `NativeSmbConnector` signs `TREE_CONNECT` when signing is required and
    session key is available.
  - SMB3 AES-CMAC/key-derivation is tracked separately in T-147.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R
    'native_smb_signing|native_smb_connector' --output-on-failure`.

### [x] T-143: Подключить native backend к `SmbClient` interface

- Приоритет: Must.
- Зависимости: T-107, T-138, T-140, T-142.
- Описание: Добавить `NativeSmbClient`, который использует internal native SMB
  library from the existing application/service layer.
- Acceptance criteria:
  - `NativeSmbClient` implements all current `SmbClient` methods.
  - Check/list/create/delete/rename/download/upload/copy/move route through
    native connection/session facade.
  - UI/services continue to depend only on `core::SmbClient`.
  - Native errors map to `AppError`.
  - Default app build links native backend.
- Заметки по тестам:
  - `make build`.
  - `ctest --test-dir tmp/build --output-on-failure`.

### [x] T-144: Переключить default build/package на native SMB backend

- Приоритет: Must.
- Зависимости: T-108, T-109, T-143.
- Описание: Сделать native backend default path and remove `libsmb2`/`smbclient`
  from default developer/package dependency path.
- Acceptance criteria:
  - `SMB_BROWSER_WITH_LIBSMB2` defaults to OFF.
  - `SMB_BROWSER_WITH_NATIVE_SMB` defaults to ON.
  - `make configure`, `make build`, `make test` use native backend.
  - Linux package deps no longer include `smbclient`.
  - `make setup` no longer installs `smbclient`.
- Заметки по тестам:
  - `make build`.
  - `ctest --test-dir tmp/build --output-on-failure`.

### [x] T-145: Обновить README под clean-clone native build

- Приоритет: Must.
- Зависимости: T-121, T-144.
- Описание: Обновить короткие clean-clone инструкции так, чтобы они не
  требовали `libsmb2-dev`, `libsmb2` source checkout или `smbclient`.
- Acceptance criteria:
  - README documents native backend as default.
  - README lists current native backend status and known open items.
  - Legacy libsmb2 instructions are explicitly marked as legacy.
  - Docs list includes native clean-room and test-matrix documents.
- Заметки по тестам:
  - Manual doc review.
  - Default build command validated after README update.

### [x] T-146: Обновить Linux package smoke на native dependency audit

- Приоритет: Must.
- Зависимости: T-112, T-144.
- Описание: Обновить Linux smoke script так, чтобы он проверял отсутствие
  legacy SMB runtime dependencies вместо ожидания bundled `libsmb2`.
- Acceptance criteria:
  - Smoke fails if package contains `libsmb2.so*`.
  - Smoke fails if Debian `Depends` contains `libsmb2`, `smbclient` or Samba.
  - Smoke fails if `ldd` reports legacy SMB runtime dependency.
  - Extracted package starts in offscreen smoke.
  - `make smoke-linux` passes in native default packaging profile.
- Заметки по тестам:
  - `make smoke-linux`.

### [x] T-147: Добавить SMB3.0/3.0.2 signing baseline

- Приоритет: Must.
- Зависимости: T-122, T-142.
- Описание: Реализовать clean-room AES-128-CMAC signing path для SMB3.0/3.0.2,
  включая derivation signing key from NTLM session base key и подключение в
  `NativeSmbConnector`.
- Acceptance criteria:
  - AES-128 single-block encryption проходит known vector.
  - AES-128-CMAC проходит RFC 4493 empty/one-block vectors.
  - SMB3.0/3.0.2 signing key derivation реализован для label/context,
    требуемых SMB3 signing.
  - `signSmb2DirectTcpFrame` и response verification поддерживают
    SMB3.0/3.0.2 через AES-CMAC.
  - `NativeSmbConnector` derives SMB3 signing key when signing is required.
  - `NativeSmbClient` снова предлагает SMB3.0/3.0.2 dialects in default
    native backend.
- Заметки по тестам:
  - `make native-test`.
  - `ctest --test-dir tmp/build-native-no-legacy -R
    'native_smb_signing|native_smb_connector' --output-on-failure`.

### [x] T-148: Добавить paged native directory listing

- Приоритет: Must.
- Зависимости: T-129, T-134.
- Описание: Расширить `DirectoryLister`, чтобы listing не ограничивался одним
  SMB2 `QUERY_DIRECTORY` response и читал страницы до `STATUS_NO_MORE_FILES`.
- Acceptance criteria:
  - Первый `QUERY_DIRECTORY` отправляется с `RestartScans`.
  - Последующие `QUERY_DIRECTORY` requests используют тот же directory handle
    без `RestartScans`.
  - Entries из нескольких responses агрегируются в один result.
  - Listing закрывает directory handle после получения `STATUS_NO_MORE_FILES`.
  - `NativeSmbSession` корректно учитывает variable message id consumption.
- Заметки по тестам:
  - `make native-test`.
  - `native_smb_directory_lister` covers multi-page listing and request flags.
  - `native_smb_session` covers message id advancement after paged listing.

### [x] T-090: Спроектировать C++ facade поверх native SMB core

- Приоритет: Must.
- Зависимости: T-086, T-089.
- Описание: Изолировать protocol details за small C++ facade, чтобы
  application layer не зависел от внутренних заголовков SMB engine.
- Acceptance criteria:
  - Есть `NativeSmbSession`, `NativeSmbDirectory`, `NativeSmbFile` или
    эквивалентные RAII wrappers.
  - Все errors конвертируются в internal `NativeSmbError`, затем в `AppError`.
  - Facade не экспортирует raw protocol packets/status values или internal
    pointers в UI.
  - Facade имеет injectable logging/progress/cancellation callbacks.
  - Memory ownership покрыт tests/review.
- Заметки по тестам:
  - Unit tests RAII close/free on success and failure.
  - Sanitizer tests, если доступны ASan/UBSan profile.
  - Covered by `native_smb_session`, `native_smb_connector` and composed
    exchanger tests.

### [x] T-091: Поддержать event loop, cancellation и timeouts в native core

- Приоритет: Must.
- Зависимости: T-090.
- Описание: Все сетевые операции должны оставаться async для UI и корректно
  отменяться, несмотря на blocking OS/socket paths внутри native core.
- Acceptance criteria:
  - Каждая long-running operation принимает `OperationContext`.
  - Cancellation прерывает transfer/list/connect настолько быстро, насколько
    позволяет backend.
  - Timeout применяется на connect, list, read, write, metadata operations.
  - Нет busy waiting в worker threads.
  - UI thread не блокируется.
- Заметки по тестам:
  - Fake/loopback tests на cancellation.
  - Integration tests на timeout against unreachable host.
  - `core::OperationContext` cancellation is bridged into native
    `OperationContext` through a callback, so `OperationQueue` cancellation
    reaches Direct TCP/protocol code.

### [x] T-092: Обновить typed error mapping для native SMB engine

- Приоритет: Must.
- Зависимости: T-090.
- Описание: Заменить libsmb2-specific mapping на backend-neutral mapping из
  native SMB statuses/system errors в `SmbErrorCode`/`AppError`.
- Acceptance criteria:
  - Wrong credentials, no rights, DNS, timeout, share unavailable, DFS errors,
    protocol unsupported и local IO различаются.
  - Raw technical details sanitized.
  - Error mapping не зависит от локализованных строк backend-а или OS.
  - Все existing UI error flows продолжают работать.
- Заметки по тестам:
  - Unit tests mapping для NTSTATUS/system errors.
  - Regression tests на отсутствие secret values в error details/logs.
  - Covered by `native_smb_protocol` and `native_smb_error_mapper`.

## Этап 17. Native SMB sessions, auth и DFS

### [x] T-093: Реализовать session lifecycle и connection pooling

- Приоритет: Must.
- Зависимости: T-090, T-091, T-092.
- Описание: Реализовать подключение к server/share, reuse session и
  корректное закрытие соединений.
- Acceptance criteria:
  - Connect/check/open share работают без `libsmb2`.
  - Negotiation поддерживает только SMB2/SMB3 dialects; SMB1 не предлагается.
  - Session закрывается при disconnect, error или destruction.
  - Есть bounded cache/pool, если reuse нужен для performance.
  - Multi-share и cross-share operations не смешивают credentials.
  - Thread ownership session описан и соблюдается.
- Заметки по тестам:
  - Unit tests lifecycle with fake core.
  - Docker Samba integration: connect/list/disconnect/reconnect.
  - `native_smb_connector` covers connect, tree connect, explicit
    `TREE_DISCONNECT` and `LOGOFF` request sequencing.

### [x] T-094: Реализовать password, domain, guest и anonymous auth

- Приоритет: Must.
- Зависимости: T-093.
- Описание: Поддержать текущие auth modes приложения через native backend.
- Acceptance criteria:
  - `DOMAIN\user`, `user@domain`, plain user, guest и anonymous работают через
    normalized connection model.
  - Пароль не попадает в URI, logs, SQLite или command line.
  - Wrong password мапится в `AuthenticationFailed`.
  - Guest/anonymous failures показываются как actionable errors.
  - Existing CredentialStore flow не меняется.
- Заметки по тестам:
  - Unit tests credential conversion without real secrets.
  - Docker Samba tests для password и guest/anonymous fixtures.
  - `native_smb_ntlm_v2_token_provider` covers password/domain, guest,
    anonymous and current-user unsupported behavior without real secrets.

### [x] T-095: Спроектировать current user / Kerberos support

- Приоритет: Must.
- Зависимости: T-083, T-093.
- Описание: Спроектировать current user/Kerberos/SSO для Windows Server
  validation и определить платформенные ограничения.
- Acceptance criteria:
  - Для Windows, Linux, macOS описаны механизмы и зависимости.
  - Если Kerberos/GSSAPI требует system libraries, это отражено в one-binary
    decision.
  - UI не обещает current user там, где backend не поддерживает режим.
  - Есть feature flag/diagnostic message.
- Заметки по тестам:
  - Manual domain environment tests.
  - Unit tests на availability reporting.

### [x] T-096: Реализовать native DFS referral resolution

- Приоритет: Must.
- Зависимости: T-093, T-094, T-122.
- Описание: Убрать external `smbclient` DFS fallback и резолвить DFS
  namespace внутри native SMB engine.
- Acceptance criteria:
  - `STATUS_PATH_NOT_COVERED`/DFS referral обрабатывается без запуска
    `smbclient`.
  - Resolved target кэшируется с TTL и credential isolation.
  - Поддержаны nested DFS referrals, multiple targets и failover strategy.
  - Navigation внутри DFS symlink/namespace не ломает original namespace path.
  - Если DFS не поддержан сервером или backend-ом, пользователь получает
    actionable error.
  - Старый `SmbclientDfsReferralResolver` удален или отключен.
- Заметки по тестам:
  - Unit tests referral cache/rebase logic.
  - Integration tests с Samba DFS fixture или dedicated test server.
  - Regression check на корпоративный пример из `tmp/mylist.json` без логов
    credentials.
  - Частично реализовано: добавлены clean-room `FSCTL_DFS_GET_REFERRALS`
    request builder, DFS referral response parser v2/v3/v4 и SMB2 IOCTL
    fetcher with all-ones FileId для IPC$/DFS referral requests.
  - Частично реализовано: добавлен `NativeDfsReferralResolver`, native app path
    теперь использует `DfsResolvingSmbClient` + native resolver; legacy
    `SmbclientDfsReferralResolver` остаётся только в legacy libsmb2 build path.
  - Реализовано: `DfsReferralResolver` может возвращать несколько targets и
    referral TTL; `DfsResolvingSmbClient` кэширует targets с TTL, пробует
    failover targets по очереди и сохраняет original namespace path при
    nested DFS navigation.
  - Covered by `native_smb_dfs_referral`,
    `native_smb_remote_dfs_referral_fetcher`, `native_dfs_referral_resolver`
    and backend-agnostic `dfs_resolving_smb_client`.
  - Dedicated Windows Server validation для реального DFS namespace остаётся
    release/manual gate; Docker Samba через host port mapping не является
    надежной проверкой DFS target UNC без отдельной сетевой схемы на 445/tcp.

### [x] T-122: Реализовать SMB signing и encryption

- Приоритет: Must.
- Зависимости: T-093, T-094.
- Описание: Реализовать обязательные для корпоративных Windows Server
  окружений механизмы SMB signing и SMB encryption для SMB2/SMB3.
- Acceptance criteria:
  - Dialect negotiation определяет requirements/capabilities signing и
    encryption.
  - Signing поддержан для SMB2/SMB3 sessions and messages.
  - Encryption поддержан для SMB3 там, где сервер требует или разрешает его.
  - Политики `required`, `preferred`, `disabled` описаны на уровне backend
    config, но небезопасное отключение не используется по умолчанию.
  - Ошибки policy mismatch отображаются как actionable `ProtocolUnsupported`
    или security policy error без утечки credentials.
- Заметки по тестам:
  - Unit tests для crypto/signature state machine на synthetic vectors.
  - Docker Samba fixtures с required signing/encryption, если поддерживаются.
  - Manual Windows Server validation с signing/encryption policy.
  - Реализовано: SMB2/2.1 HMAC-SHA256 signing and SMB3 AES-CMAC signing.
  - Реализовано: clean-room SMB3 AES-128-CCM transform header,
    encrypt/decrypt, client/server encryption key derivation and transport
    wrapper for SMB 3.0/3.0.2.
  - Реализовано: connector advertises encryption capability, supports required
    session/share encryption, retries encrypted `TREE_CONNECT` when an
    encryption-capable server denies the unencrypted attempt, and fails closed
    when policy/server requires encryption but negotiated dialect/capability
    cannot support it.
  - Covered by `native_smb_signing`, `native_smb_encryption`,
    `native_smb_connector` and Docker Samba encrypted-share integration.

### [x] T-097: Реализовать share browsing и capability probing

- Приоритет: Should.
- Зависимости: T-093.
- Описание: Поддержать получение списка share на server и диагностику
  capabilities/dialects, где это возможно без SMB1/NetBIOS fallback.
- Acceptance criteria:
  - IPC$/RPC-based share enumeration работает для доступных серверов.
  - SMB1/NetBIOS browsing отсутствует и не используется как fallback.
  - Capability report включает dialect, signing/encryption support,
    DFS support, server/share availability.
  - Ошибки не раскрывают credentials.
- Заметки по тестам:
  - Docker Samba integration для share list.
  - Unit tests capability formatting/sanitization.
  - Реализовано: добавлен `SmbCapabilityReport` contract,
    `NativeSmbClient::probeCapabilities()` returns negotiated dialect,
    signing/encryption flags, DFS support and tree DFS/encryption flags without
    SMB1/NetBIOS.
  - Реализовано: `SmbClient::listShares()` contract, fake backend support,
    native `IPC$` + DCE/RPC bind/request/response + SRVSVC `NetrShareEnum`
    level 1 implementation, no `smbclient`/SMB1/NetBIOS fallback.
  - Covered by `native_smb_dcerpc`, `native_smb_srvs_rpc`,
    `native_smb_remote_share_enumerator`, `smb_client_contract` and Docker
    Samba `shareBrowsingListsConfiguredShares`.

## Этап 18. Native SMB file operation parity

### [x] T-098: Реализовать directory listing, stat и metadata mapping

- Приоритет: Must.
- Зависимости: T-093, T-096.
- Описание: Реализовать list/stat для remote entries с типами, timestamps,
  size, attributes, permissions и reparse/symlink indicators.
- Acceptance criteria:
  - `RemoteFileEntry` заполняется без `libsmb2`.
  - Folder, file, symlink/reparse и unknown types различаются.
  - Dates/sizes/attributes корректны для Windows SMB и Samba.
  - Большие директории не блокируют UI и поддерживают cancellation.
  - Existing browser model tests проходят без изменений public behavior.
- Заметки по тестам:
  - Unit tests metadata conversion.
  - Docker Samba tests: files, folders, symlinks/reparse if fixture supports.
  - Native Docker Samba real-wire проверка покрывает root/nested listing,
    stat-driven file metadata and symlink/reparse indicators на synthetic
    fixture.

### [x] T-099: Реализовать download/read с progress, resume и temp cache

- Приоритет: Must.
- Зависимости: T-098.
- Описание: Реализовать чтение файлов, скачивание локально и open-via-cache
  через native backend.
- Acceptance criteria:
  - Download поддерживает progress и cancellation.
  - Partial download cleanup/retry policy определена.
  - `reget`/resume capability реализована в library, даже если UI включает ее
    позже.
  - Open file + local edit sync flow продолжает работать.
  - Local cache paths не содержат secrets.
- Заметки по тестам:
  - Fake/native unit tests stream read.
  - Docker Samba download large file/cancel/resume tests.
  - Regression test open/edit/upload remains green.
  - Native Docker Samba real-wire проверка скачивает `large.bin`, проверяет
    monotonic progress and final byte count. Low-level native API supports
    offset reads; high-level transfers use 64 KiB chunks until full credit
    window management is added in performance work.

### [x] T-100: Реализовать upload/write с progress, resume и overwrite policy

- Приоритет: Must.
- Зависимости: T-098.
- Описание: Реализовать запись файлов на SMB, включая создание, перезапись и
  возобновление upload.
- Acceptance criteria:
  - Upload поддерживает progress и cancellation.
  - Existing overwrite/rename conflict behavior не ломается.
  - `reput`/resume capability реализована в library.
  - Local file lock errors мапятся в `LocalIoError`.
  - Secret values не попадают в operation names/logs.
- Заметки по тестам:
  - Docker Samba upload/overwrite/cancel/resume tests.
  - Unit tests progress monotonicity.
  - Native Docker Samba real-wire проверка загружает файл больше 2 MiB,
    проверяет monotonic progress, повторный upload в тот же path and download
    verification. Low-level native API supports offset writes; high-level
    overwrite policy remains truncate/replace.

### [x] T-101: Реализовать mkdir, rmdir, delete, deltree и wildcard delete

- Приоритет: Must.
- Зависимости: T-098.
- Описание: Поддержать удаление и создание remote objects в native backend.
- Acceptance criteria:
  - Create folder, remove empty folder, delete file работают.
  - Recursive delete (`deltree`) реализован в library с confirmation handled
    above service layer.
  - Wildcard delete/list capability реализована в library, UI может использовать
    ее позже.
  - Permission denied, not found, directory not empty различаются.
  - Cancellation работает для recursive operations.
- Заметки по тестам:
  - Fake/native unit tests recursive delete.
  - Docker Samba permission denied/not found/dir not empty tests.

### [x] T-102: Реализовать rename, move и cross-share move

- Приоритет: Must.
- Зависимости: T-098, T-100, T-101.
- Описание: Реализовать rename/move внутри share и между share/connection.
- Acceptance criteria:
  - Same-share rename использует native rename.
  - Cross-share move выполняет copy + verified delete only after success.
  - Partial failure возвращает structured result.
  - Replace/no-replace policy описана и покрыта tests.
- Заметки по тестам:
  - Unit tests partial failure.
  - Docker Samba same-share and cross-share scenarios.

### [x] T-103: Реализовать copy и server-side copy fallback strategy

- Приоритет: Must.
- Зависимости: T-099, T-100.
- Описание: Поддержать copy inside SMB, включая server-side copy там, где
  backend/server позволяют, и stream-copy fallback.
- Acceptance criteria:
  - Same-server/share copy пытается использовать server-side mechanism.
  - Fallback stream copy работает между разными share/server.
  - Progress отражает фактический copy mode.
  - Cancellation не оставляет поврежденный destination без понятного статуса.
- Заметки по тестам:
  - Docker Samba copy large file.
  - Unit tests fallback decision matrix.
  - `native_smb_protocol` covers SMB2 IOCTL and SRV_COPYCHUNK payloads.
  - `native_smb_session` covers server-side copy sequencing with
    `FSCTL_SRV_REQUEST_RESUME_KEY` and `FSCTL_SRV_COPYCHUNK`.
  - Docker Samba real-wire profile covers same-share copy and cross-share
    stream-copy fallback with synthetic fixtures.

### [x] T-104: Реализовать symlink, hardlink и reparse handling

- Приоритет: Must.
- Зависимости: T-098.
- Описание: Поддержать переход в symlink directories, открытие symlink files,
  чтение target там, где server/backend позволяют, и создание symlink/hardlink
  как library capabilities.
- Acceptance criteria:
  - Existing symlink navigation regression не ломается.
  - File symlink open/download работает.
  - Reparse point/DFS link не путается с обычным file symlink.
  - Create symlink/hardlink available через library API, UI exposure отдельная.
- Заметки по тестам:
  - Unit tests type mapping.
  - Integration tests с Samba UNIX extensions/reparse fixture, если возможно.
  - Реализовано: reparse indicators/listing/stat, symlink file/folder UI
    fallback behavior, native hardlink API through `FileLinkInformation`, and
    native symlink creation API through `FSCTL_SET_REPARSE_POINT` with Windows
    symbolic-link reparse buffer construction.
  - `native_smb_protocol` covers symlink reparse buffer layout.
  - `native_smb_remote_object_operator` and `native_smb_session` cover hardlink
    and symlink creation routing. Real-server symlink/hardlink capability
    validation remains part of optional server-specific integration profiles.

### [x] T-105: Реализовать file attributes, timestamps, ACL и EA operations

- Приоритет: Must.
- Зависимости: T-098.
- Описание: Реализовать advanced metadata capabilities из `smbclient`
  (`allinfo`, `stat`, `chmod`, `chown`, `getfacl`, `geteas`, `setea`,
  `setmode`, `utimes`) на уровне library.
- Acceptance criteria:
  - Library может читать all-info/stat-like metadata.
  - Timestamps can be set where server supports it.
  - EAs can be listed/read/set/remove where supported.
  - ACL/POSIX operations gated by capability detection.
  - UI не показывает недоступные операции как гарантированные.
- Заметки по тестам:
  - Unit tests capability gating.
  - Optional integration tests on Samba with UNIX extensions/ACL support.
  - Реализовано в native library: `FILE_BASIC_INFORMATION` set for timestamps
    and attributes, `FILE_FULL_EA_INFORMATION` list/set/remove, raw security
    descriptor query/set through SMB2 QUERY/SET_INFO with POSIX chmod/chown
    explicitly gated as unsupported capability until a POSIX extension contract
    is added.
  - `native_smb_protocol` covers basic-info and EA buffer wire layout.
  - `native_smb_remote_metadata_operator` covers metadata operation routing.
  - `native_smb_session` covers metadata capability gating.

### [x] T-106: Реализовать change notify/watch capability

- Приоритет: Must.
- Зависимости: T-093, T-098.
- Описание: Поддержать native SMB notify для будущего auto-refresh текущей
  remote folder.
- Acceptance criteria:
  - Library exposes watch/notify API with cancellation.
  - UI integration can remain disabled by default.
  - Network noise and server support limitations documented.
  - Notify failures degrade to manual refresh.
- Заметки по тестам:
  - Unit tests callback lifecycle.
  - Optional Samba integration if reliable in CI.

## Этап 19. Замена текущих SMB backend-ов в приложении

### [x] T-107: Реализовать `NativeSmbClient` для существующего `SmbClient` interface

- Приоритет: Must.
- Зависимости: T-093, T-098, T-099, T-100, T-101, T-102.
- Описание: Подключить новую внутреннюю библиотеку к текущему application
  layer без переписывания UI.
- Acceptance criteria:
  - `NativeSmbClient` покрывает все методы текущего `SmbClient`.
  - Existing `RemoteBrowserWidget`, services и operation queue не знают о
    Samba/native internals.
  - `FakeSmbClient` остается основным unit-test backend.
  - Feature flags позволяют сравнить old/new backend в transition period.
- Заметки по тестам:
  - Existing FakeSmbClient tests unchanged.
  - New native integration tests cover each `SmbClient` method.
  - Default build links `NativeSmbClient`; native contract tests cover the
    underlying facade methods without real secrets.

### [x] T-108: Удалить runtime-зависимости от `libsmb2` и external `smbclient`

- Приоритет: Must.
- Зависимости: T-096, T-107.
- Описание: Убрать `Libsmb2SmbClient`, `SmbclientDfsReferralResolver` и
  packaging/install references на внешние SMB tools.
- Acceptance criteria:
  - CMake default build не ищет и не линкует `libsmb2`.
  - Приложение не запускает `smbclient` process ни для DFS, ни для diagnostics.
  - README/setup/package docs не требуют `libsmb2-dev` или `smbclient` для
    runtime.
  - Old code удален или оставлен только за явно выключенным legacy flag.
  - CI проверяет отсутствие символических/строковых references на старые
    runtime dependencies в package.
- Заметки по тестам:
  - Clean build on machine without `libsmb2-dev` and without `smbclient`.
  - Package smoke dependency audit.
  - `make smoke-linux` verifies default package has no `libsmb2`/`smbclient`
    runtime dependency.

### [x] T-109: Обновить CMake, scripts и Makefile для native backend

- Приоритет: Must.
- Зависимости: T-088, T-089, T-108.
- Описание: Перестроить build profiles под native SMB backend и one-binary
  packaging.
- Acceptance criteria:
  - `make build`, `make test`, `make run`, `make package-linux` работают из
    clean clone.
  - Старые цели `libsmb2` либо удалены, либо переименованы в legacy notes.
  - CMake option names отражают новый backend (`SMB_BROWSER_WITH_NATIVE_SMB`).
  - Build artifacts остаются в `tmp/` или build directories.
  - Нет generated source churn в repo.
- Заметки по тестам:
  - Clean-clone CI script.
  - Configure/build with network disabled after source cache prepared.
  - `make native-test`, `make build` and `make smoke-linux` cover default
    native build profiles.

### [x] T-110: Обновить UI/service capabilities под native feature set

- Приоритет: Should.
- Зависимости: T-105, T-106, T-107.
- Описание: Добавить UI hooks для новых library capabilities без перегрузки
  основного браузера.
- Acceptance criteria:
  - Capabilities влияют на enabled/disabled state операций.
  - Metadata/attributes can be shown in properties/details dialog.
  - Unsupported advanced operations скрыты или показывают понятную причину.
  - Основной browser workflow остается простым.
- Заметки по тестам:
  - UI smoke tests for capabilities state.
  - Unit tests view-model mapping.
  - Добавлен `SmbClientCapabilities` contract, `ConnectionOpenService`
    прокидывает capabilities в `OpenConnectionResult`, `NativeSmbClient`
    сообщает native metadata/watch/share-browsing support and unsupported POSIX
    reason.
  - `RemoteBrowserWidget` uses capabilities for toolbar enabled state and
    exposes a lightweight Properties dialog from current model metadata.
  - `connection_open_service` covers capability propagation.
  - `remote_browser_widget` covers capability-controlled action state.

## Этап 20. One-binary packaging и dependency audit

### [x] T-111: Подготовить static/portable dependency strategy

- Приоритет: Must.
- Зависимости: T-084, T-089.
- Описание: Определить и реализовать стратегию поставки одного portable
  binary/package без `libsmb2` и `smbclient`.
- Acceptance criteria:
  - Linux, Windows, macOS имеют отдельный build plan.
  - Qt, SQLite driver, QtKeychain/vault, crypto, native SMB engine accounted.
  - Native SMB engine linked into app binary; Qt/runtime libraries may be
    shipped рядом в portable package/app bundle.
  - Dependency audit fails build if `libsmb2` or `smbclient` are required.
- Заметки по тестам:
  - `ldd`/`otool -L`/`dumpbin` outputs archived in package smoke logs.
  - Package smoke runs on clean VM/container.

### [x] T-112: Реализовать Linux portable build profile

- Приоритет: Must.
- Зависимости: T-111.
- Описание: Сделать Linux build/package без внешних SMB runtime dependencies.
- Acceptance criteria:
  - DEB/AppImage/portable artifact не зависит от `libsmb2` или `smbclient`.
  - Native SMB engine linked into app.
  - System dependencies documented honestly.
  - Package smoke confirms app starts and can list a test SMB share.
- Заметки по тестам:
  - Linux package smoke.
  - Docker Samba integration in packaged app, если возможно.

### [x] T-113: Реализовать Windows portable build profile

- Приоритет: Must.
- Зависимости: T-111.
- Описание: Сделать Windows portable/installer build без `libsmb2.dll` и
  `smbclient.exe`.
- Acceptance criteria:
  - `smb-browser.exe`/portable package does not require `libsmb2.dll`.
  - No `smbclient.exe` bundled or invoked.
  - Credential store works via Windows Credential Manager or approved fallback.
  - Native SMB engine passes basic integration against Windows/Samba test share.
- Заметки по тестам:
  - Windows package smoke script.
  - Dependency audit via `dumpbin` or equivalent.

### [x] T-114: Реализовать macOS portable/app bundle profile

- Приоритет: Must.
- Зависимости: T-111.
- Описание: Сделать macOS app bundle/dmg без `libsmb2` и `smbclient`.
- Acceptance criteria:
  - App bundle contains native SMB engine inside app binary/static objects.
  - No external `smbclient` helper is required for DFS.
  - Keychain prompts remain understandable.
  - Translation files and cache/log paths still work in app bundle.
- Заметки по тестам:
  - macOS package smoke script.
  - Dependency audit via `otool -L`.

### [x] T-115: Добавить security hardening для static native SMB engine

- Приоритет: Must.
- Зависимости: T-089, T-111.
- Описание: Включить security-relevant compiler/linker options и обработать
  supply-chain risks vendored/native SMB code.
- Acceptance criteria:
  - Release builds use hardened flags where supported.
  - Vendored third-party source checksum/signature verified, если такой source
    появится.
  - SBOM/dependency manifest generated for release.
  - Known CVE/security advisory tracking process documented for native SMB
    code and third-party dependencies.
  - Secrets are not included in crash/log diagnostics.
- Заметки по тестам:
  - CI checks hardening flags where practical.
  - Static analysis/sanitizer profile added for native SMB code.

## Этап 21. Test matrix для native SMB migration

### [x] T-124: Зафиксировать полную test matrix для native SMB library

- Приоритет: Must.
- Зависимости: T-085, T-087.
- Описание: Описать, как проверять весь функционал внутренней SMB-библиотеки
  на разных уровнях: protocol unit, fake transport, contract tests, Docker
  Samba, Windows Server/manual, security regression и package audit.
- Acceptance criteria:
  - Документ `docs/native-smb-test-matrix.md` существует.
  - Для каждой Must/Should/Could capability из native feature matrix указан
    ожидаемый уровень покрытия.
  - Definition of done запрещает считать Must-возможность готовой без
    protocol/fake/contract/integration или documented manual coverage.
  - Отдельно отражены signing, encryption, DFS, current-user/Kerberos,
    ACL/EA/symlink/hardlink/notify, cancellation/timeouts и secret handling.
  - Реальные пароли и корпоративные credentials запрещены в тестах и fixtures.
- Заметки по тестам:
  - Тестов к документационному gate нет.
  - Matrix становится checklist для T-116, T-117, T-118, T-120 и всех native
    implementation задач.

### [x] T-116: Расширить FakeSmbClient/native contract tests

- Приоритет: Must.
- Зависимости: T-107.
- Описание: Сделать общий contract test suite, который проходит и на fake, и
  на native backend там, где есть integration fixture.
- Acceptance criteria:
  - Covered: check, list, stat, upload, download, mkdir, delete, rename,
    copy/move, symlink handling, DFS, timeout, cancellation.
  - Test credentials synthetic only.
  - Contract tests document feature differences.
  - Existing UI tests остаются быстрыми и не требуют network.
- Заметки по тестам:
  - Default unit run uses fake only.
  - Native integration tests opt-in by label/profile.

### [x] T-117: Обновить Docker Samba integration fixtures

- Приоритет: Must.
- Зависимости: T-096, T-116.
- Описание: Расширить Docker Samba профиль под native backend и DFS/metadata
  сценарии.
- Acceptance criteria:
  - Fixtures include multiple shares, nested dirs, files, permissions,
    symlink/reparse where possible, ACL/EA metadata, large file,
    guest/password auth.
  - DFS fixture covers nested referrals, multiple targets, failover and TTL
    cache behavior, or documents exact external manual coverage if Docker
    cannot support all cases.
  - Tests disabled by default in local unit run.
  - CI Linux can run the profile.
- Заметки по тестам:
  - `ctest -L docker-samba`.
  - No real passwords; generated synthetic credentials only.
  - Native real-wire profile runs with
    `SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE=1`; `make samba-up` waits for the
    fixture healthcheck to avoid startup races.

### [x] T-118: Добавить cross-platform manual/automated smoke tests

- Приоритет: Must.
- Зависимости: T-112, T-113, T-114.
- Описание: Обновить package smoke для Windows/Linux/macOS под native SMB
  engine and no external SMB tools.
- Acceptance criteria:
  - Каждый smoke проверяет отсутствие `libsmb2`/`smbclient` dependency.
  - Проверяется start, add/edit connection, credential save/load, connect/list,
    upload/download/open file.
  - Manual Windows Server smoke проверяет DFS, signing/encryption policy,
    current user/Kerberos/SSO where available, ACL/EA/symlink/hardlink/notify.
  - Проверяется закрытие окна без hanging process.
  - Smoke results documented in release checklist.
- Заметки по тестам:
  - Linux automated.
  - Windows/macOS manual or runner-backed before release.

### [x] T-119: Добавить performance и stress tests для native backend

- Приоритет: Should.
- Зависимости: T-099, T-100, T-103.
- Описание: Проверить native backend на больших директориях, больших файлах и
  длительных операциях.
- Acceptance criteria:
  - Large directory list has bounded memory growth.
  - Upload/download throughput measured and not worse than acceptable baseline.
  - Cancellation during large transfer is reliable.
  - UI remains responsive.
- Заметки по тестам:
  - Optional perf profile, not default unit suite.
  - Store metrics as CI artifacts where possible.
  - `make perf-test` enables `SMB_BROWSER_ENABLE_PERF_TESTS=ON` and runs
    `native_smb_perf_stress` with labels `perf;native-stress`.
  - Current coverage includes large directory parsing, chunked server-side copy
    progress and cancellation between copy chunks.

### [x] T-120: Провести security regression suite после удаления старых backend-ов

- Приоритет: Must.
- Зависимости: T-108, T-116.
- Описание: Перепроверить секреты, логи, export/import и error messages после
  перехода на native backend.
- Acceptance criteria:
  - Пароли не попадают в logs, SQLite, export без паролей, operation names,
    crash/error details.
  - Native backend не пишет credentials в stdout/stderr.
  - Plain-text password export behavior не изменился.
  - CredentialStore contract tests remain green.
- Заметки по тестам:
  - LogSanitizer regression cases from native errors.
  - Security suite part of default CI.

### [x] T-121: Обновить документацию и release checklist под native SMB engine

- Приоритет: Must.
- Зависимости: T-108, T-111, T-118, T-120, T-123.
- Описание: Обновить README, PRD/TASKS references, packaging docs,
  release checklist и developer setup.
- Acceptance criteria:
  - README больше не просит устанавливать `libsmb2-dev` или `smbclient`.
  - Описан native SMB backend, supported capabilities и known limitations.
  - License/compliance section соответствует решению T-083.
  - Build instructions work from clean clone.
  - Release checklist включает dependency audit и source/license obligations.
- Заметки по тестам:
  - Manual clean-clone doc test.
  - CI validates key commands from README if practical.

### [x] T-123: Перевести проектные документы и release metadata в open-source/GPL model

- Приоритет: Must.
- Зависимости: T-083, T-084, T-087.
- Описание: Зафиксировать open-source/GPL-compatible распространение проекта
  без Samba-derived code и привести документы, metadata и release process к
  выбранной модели.
- Acceptance criteria:
  - Выбрана конкретная лицензия проекта, совместимая с clean-room native SMB
    implementation и Qt usage.
  - `LICENSE`, copyright notices, README и package metadata обновлены.
  - Отдельно указано, что Samba не входит в source/binary distribution и
    используется только как reference в `tmp`.
  - Для Qt/runtime dependencies описаны соответствующие notices и obligations.
  - Release checklist содержит license review gate.
- Заметки по тестам:
  - CI/package smoke проверяет наличие `LICENSE` и notices.
  - Manual release review перед публикацией.
