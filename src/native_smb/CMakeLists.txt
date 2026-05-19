add_library(smb_browser_native_smb STATIC
    CloseExchanger.cpp
    CloseExchanger.h
    DirectTcpTransport.cpp
    DirectTcpTransport.h
    DirectoryLister.cpp
    DirectoryLister.h
    FileReader.cpp
    FileReader.h
    FileWriter.cpp
    FileWriter.h
    Negotiator.cpp
    Negotiator.h
    NativeSmbConnection.cpp
    NativeSmbConnection.h
    NativeSmbConnector.cpp
    NativeSmbConnector.h
    NativeSmbSession.cpp
    NativeSmbSession.h
    NtlmCrypto.cpp
    NtlmCrypto.h
    NtlmMessages.cpp
    NtlmMessages.h
    NtlmV2TokenProvider.cpp
    NtlmV2TokenProvider.h
    Protocol.cpp
    Protocol.h
    QueryInfoExchanger.cpp
    QueryInfoExchanger.h
    ReadExchanger.cpp
    ReadExchanger.h
    RemoteObjectOperator.cpp
    RemoteObjectOperator.h
    RemoteStatReader.cpp
    RemoteStatReader.h
    SetInfoExchanger.cpp
    SetInfoExchanger.h
    SessionSetupExchanger.cpp
    SessionSetupExchanger.h
    Smb2Signing.cpp
    Smb2Signing.h
    SmbNative.cpp
    SmbNative.h
    SpnegoToken.cpp
    SpnegoToken.h
    Transport.h
    TreeConnector.cpp
    TreeConnector.h
    WriteExchanger.cpp
    WriteExchanger.h
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

if(WIN32)
    target_link_libraries(smb_browser_native_smb
        PUBLIC
            ws2_32
    )
endif()
