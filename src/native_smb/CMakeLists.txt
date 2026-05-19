add_library(smb_browser_native_smb STATIC
    Negotiator.cpp
    Negotiator.h
    Protocol.cpp
    Protocol.h
    SessionSetupExchanger.cpp
    SessionSetupExchanger.h
    SmbNative.cpp
    SmbNative.h
    Transport.h
    TreeConnector.cpp
    TreeConnector.h
)

set_target_properties(smb_browser_native_smb
    PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
)

target_include_directories(smb_browser_native_smb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(smb_browser_native_smb
    PUBLIC
        cxx_std_17
)

target_compile_definitions(smb_browser_native_smb
    PUBLIC
        SMB_BROWSER_NATIVE_SMB_CLEAN_ROOM=1
)
