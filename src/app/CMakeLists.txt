add_executable(smb-browser
    main.cpp
)

target_link_libraries(smb-browser
    PRIVATE
        smb_browser_core
        smb_browser_ui
        Qt5::Widgets
)

set_target_properties(smb-browser PROPERTIES
    OUTPUT_NAME "smb-browser"
)
