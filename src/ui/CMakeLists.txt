add_library(smb_browser_ui STATIC
    MainWindow.cpp
    MainWindow.h
)

target_include_directories(smb_browser_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(smb_browser_ui
    PUBLIC
        smb_browser_core
        Qt5::Widgets
)
