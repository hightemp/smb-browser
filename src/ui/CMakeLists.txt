add_library(smb_browser_ui STATIC
    ConnectionActionPrompter.h
    ConnectionClipboardController.cpp
    ConnectionClipboardController.h
    ConnectionListModel.cpp
    ConnectionListModel.h
    ConnectionDialog.cpp
    ConnectionDialog.h
    ConnectionManagementController.cpp
    ConnectionManagementController.h
    ConnectionOpenController.cpp
    ConnectionOpenController.h
    ConnectionsPanel.cpp
    ConnectionsPanel.h
    ConnectivityCheckController.cpp
    ConnectivityCheckController.h
    DialogImportExportActionPrompter.cpp
    DialogImportExportActionPrompter.h
    DialogRemoteFileActionPrompter.cpp
    DialogRemoteFileActionPrompter.h
    ImportExportActionPrompter.h
    ImportExportController.cpp
    ImportExportController.h
    LocalizationManager.cpp
    LocalizationManager.h
    LogViewer.cpp
    LogViewer.h
    MainWindow.cpp
    MainWindow.h
    MessageBoxConnectionActionPrompter.cpp
    MessageBoxConnectionActionPrompter.h
    RemoteFileActionPrompter.h
    RemoteBrowserWidget.cpp
    RemoteBrowserWidget.h
    RemoteFileModel.cpp
    RemoteFileModel.h
    SettingsDialog.cpp
    SettingsDialog.h
    StatusPanel.cpp
    StatusPanel.h
    ThemeManager.cpp
    ThemeManager.h
    TrayController.cpp
    TrayController.h
)

target_include_directories(smb_browser_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(smb_browser_ui
    PUBLIC
        smb_browser_application
        smb_browser_core
        Qt5::Widgets
)

set(SMB_BROWSER_TRANSLATION_OUTPUT_DIR "${CMAKE_BINARY_DIR}/i18n")
target_compile_definitions(smb_browser_ui
    PUBLIC
        SMB_BROWSER_TRANSLATION_DIR="${SMB_BROWSER_TRANSLATION_OUTPUT_DIR}"
)

find_program(QT_LRELEASE_EXECUTABLE NAMES lrelease-qt5 lrelease)
if(QT_LRELEASE_EXECUTABLE)
    set(SMB_BROWSER_RU_TS "${CMAKE_SOURCE_DIR}/src/i18n/smb-browser_ru.ts")
    set(SMB_BROWSER_RU_QM
        "${SMB_BROWSER_TRANSLATION_OUTPUT_DIR}/smb-browser_ru.qm"
    )

    add_custom_command(
        OUTPUT "${SMB_BROWSER_RU_QM}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${SMB_BROWSER_TRANSLATION_OUTPUT_DIR}"
        COMMAND "${QT_LRELEASE_EXECUTABLE}" "${SMB_BROWSER_RU_TS}"
                -qm "${SMB_BROWSER_RU_QM}"
        DEPENDS "${SMB_BROWSER_RU_TS}"
        VERBATIM
    )

    add_custom_target(smb_browser_translations ALL
        DEPENDS "${SMB_BROWSER_RU_QM}"
    )

    install(FILES "${SMB_BROWSER_RU_QM}"
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/smb-browser/i18n)
    if(APPLE)
        install(FILES "${SMB_BROWSER_RU_QM}"
                DESTINATION "smb-browser.app/Contents/Resources/i18n")
    endif()
endif()
