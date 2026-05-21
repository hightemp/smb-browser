Читай TASKS.md или PRD.md
TASKS.md - содержит задачи, помечай выполненные задачи чекбоксом
PRD.md - содержит контекст проекта

1. Перед тем как сделать задачу помечай что будешь делать в TASKS.md.
2. После выполнения задачи отмечай в TASKS.md.

Документы в docs:
- docs/cross-platform-smoke.md - automated/manual smoke profile для Linux/Windows/macOS packages и real-server native SMB validation
- docs/current-user-kerberos.md - design strategy для Current User/Kerberos/SSO auth в native SMB backend
- docs/license-compliance.md - license/compliance notes для GPL-3.0-or-later проекта и clean-room SMB boundary
- docs/libsmb2-spike.md - результаты spike по сборке, API, лицензии и интеграции libsmb2
- docs/linux-packaging.md - Linux packaging profile, CPack/DEB commands, runtime dependencies and keychain notes
- docs/macos-packaging.md - macOS app bundle/DMG packaging plan, macdeployqt notes and smoke checklist
- docs/native-smb-clean-room.md - clean-room migration plan для внутреннего SMB2/SMB3 engine без libsmb2/smbclient runtime dependencies
- docs/native-smb-test-matrix.md - test matrix для полного покрытия возможностей внутренней SMB-библиотеки
- docs/release-checklist.md - release gate/checklist для первой версии, включая тестовые профили и known limitations
- docs/secret-handling-policy.md - правила работы с секретами, логированием и review checklist
- docs/security-hardening.md - release hardening, dependency audit, SBOM и advisory tracking для native SMB build
- docs/windows-packaging.md - Windows ZIP/NSIS packaging plan, windeployqt notes and smoke checklist

Ставить временные репозитории и файлы в локальную папку tmp

Данные для проверки SMB есть в test.txt

В ответе пиши сколько сделано и сколько осталось (можно еще в процентах)
