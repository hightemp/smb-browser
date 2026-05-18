add_library(smb_browser_core STATIC
    AppInfo.cpp
    AppInfo.h
    Connection.cpp
    Connection.h
    ConnectionGroup.cpp
    ConnectionGroup.h
    CredentialStore.h
    Error.cpp
    Error.h
    LogSanitizer.cpp
    LogSanitizer.h
    PathNormalizer.cpp
    PathNormalizer.h
    RemoteFileEntry.cpp
    RemoteFileEntry.h
    Settings.cpp
    Settings.h
    SmbClient.h
    SmbError.cpp
    SmbError.h
)

target_include_directories(smb_browser_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(smb_browser_core
    PUBLIC
        SMB_BROWSER_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(smb_browser_core
    PUBLIC
        Qt5::Core
)
