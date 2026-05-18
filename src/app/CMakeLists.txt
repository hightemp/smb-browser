add_executable(smb-browser
    main.cpp
)

target_link_libraries(smb-browser
    PRIVATE
        smb_browser_core
        smb_browser_ui
        Qt5::Widgets
)

if(TARGET smb_browser_translations)
    add_dependencies(smb-browser smb_browser_translations)
endif()

set_target_properties(smb-browser PROPERTIES
    OUTPUT_NAME "smb-browser"
)

if(APPLE)
    set_target_properties(smb-browser PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "SMB Browser"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.smb_browser.SmbBrowser"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    )

    install(TARGETS smb-browser
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    install(TARGETS smb-browser
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
