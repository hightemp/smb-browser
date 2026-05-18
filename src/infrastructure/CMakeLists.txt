set(SMB_BROWSER_INFRASTRUCTURE_SOURCES
    credentials/EncryptedVaultCredentialStore.cpp
    credentials/EncryptedVaultCredentialStore.h
    credentials/QtKeychainCredentialStore.cpp
    credentials/QtKeychainCredentialStore.h
    logging/FileLogger.cpp
    logging/FileLogger.h
    storage/ConnectionGroupRepository.cpp
    storage/ConnectionGroupRepository.h
    storage/ConnectionRepository.cpp
    storage/ConnectionRepository.h
    storage/SettingsRepository.cpp
    storage/SettingsRepository.h
    storage/SqliteStorage.cpp
    storage/SqliteStorage.h
)

if(SMB_BROWSER_WITH_LIBSMB2)
    list(APPEND SMB_BROWSER_INFRASTRUCTURE_SOURCES
        smb/Libsmb2ErrorMapper.cpp
        smb/Libsmb2ErrorMapper.h
        smb/Libsmb2SmbClient.cpp
        smb/Libsmb2SmbClient.h
    )
endif()

add_library(smb_browser_infrastructure STATIC
    ${SMB_BROWSER_INFRASTRUCTURE_SOURCES}
)

target_include_directories(smb_browser_infrastructure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(smb_browser_infrastructure
    PUBLIC
        smb_browser_core
        PkgConfig::SODIUM
        Qt5Keychain::Qt5Keychain
        Qt5::Sql
)

if(SMB_BROWSER_WITH_LIBSMB2)
    target_link_libraries(smb_browser_infrastructure
        PUBLIC
            ${SMB_BROWSER_LIBSMB2_TARGET}
    )
    target_compile_definitions(smb_browser_infrastructure
        PUBLIC
            SMB_BROWSER_WITH_LIBSMB2=1
    )
endif()
