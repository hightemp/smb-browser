Читай TASKS.md или PRD.md
TASKS.md - содержит задачи, помечай выполненные задачи чекбоксом
PRD.md - содержит контекст проекта

Документы в docs:
- docs/libsmb2-spike.md - результаты spike по сборке, API, лицензии и интеграции libsmb2
- docs/linux-packaging.md - Linux packaging profile, CPack/DEB commands, runtime dependencies and keychain notes
- docs/macos-packaging.md - macOS app bundle/DMG packaging plan, macdeployqt notes and smoke checklist
- docs/native-smb-clean-room.md - clean-room migration plan для внутреннего SMB2/SMB3 engine без libsmb2/smbclient runtime dependencies
- docs/release-checklist.md - release gate/checklist для первой версии, включая тестовые профили и known limitations
- docs/secret-handling-policy.md - правила работы с секретами, логированием и review checklist
- docs/windows-packaging.md - Windows ZIP/NSIS packaging plan, windeployqt notes and smoke checklist

Ставить временные репозитории и файлы в локальную папку tmp

Данные для проверки SMB есть в test.txt
