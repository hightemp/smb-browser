add_library(smb_browser_application STATIC
    ConnectivityCheckService.cpp
    ConnectivityCheckService.h
    ConnectionOpenService.cpp
    ConnectionOpenService.h
    ConnectionService.cpp
    ConnectionService.h
    DesktopServicesFileOpener.cpp
    DesktopServicesFileOpener.h
    LocalFileOpener.h
    OpenFileService.cpp
    OpenFileService.h
    OperationQueue.cpp
    OperationQueue.h
    TempFileCache.cpp
    TempFileCache.h
    TransferManager.cpp
    TransferManager.h
)

target_include_directories(smb_browser_application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(smb_browser_application
    PUBLIC
        smb_browser_core
        smb_browser_infrastructure
        Qt5::Gui
)
