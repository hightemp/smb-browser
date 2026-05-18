add_library(smb_browser_ui STATIC
    ConnectionActionPrompter.h
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
    DialogRemoteFileActionPrompter.cpp
    DialogRemoteFileActionPrompter.h
    MainWindow.cpp
    MainWindow.h
    MessageBoxConnectionActionPrompter.cpp
    MessageBoxConnectionActionPrompter.h
    RemoteFileActionPrompter.h
    RemoteBrowserWidget.cpp
    RemoteBrowserWidget.h
    RemoteFileModel.cpp
    RemoteFileModel.h
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
