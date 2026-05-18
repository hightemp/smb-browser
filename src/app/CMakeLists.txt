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
