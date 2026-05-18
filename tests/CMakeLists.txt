add_executable(test_core_smoke
    test_core_smoke.cpp
)

target_link_libraries(test_core_smoke
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME core_smoke COMMAND test_core_smoke)

add_executable(test_error_model
    test_error_model.cpp
)

target_link_libraries(test_error_model
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME error_model COMMAND test_error_model)

add_executable(test_connection_model
    test_connection_model.cpp
)

target_link_libraries(test_connection_model
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME connection_model COMMAND test_connection_model)

add_executable(test_path_normalizer
    test_path_normalizer.cpp
)

target_link_libraries(test_path_normalizer
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME path_normalizer COMMAND test_path_normalizer)

add_executable(test_remote_file_entry
    test_remote_file_entry.cpp
)

target_link_libraries(test_remote_file_entry
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME remote_file_entry COMMAND test_remote_file_entry)

add_executable(test_settings_model
    test_settings_model.cpp
)

target_link_libraries(test_settings_model
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME settings_model COMMAND test_settings_model)

add_executable(test_smb_error_mapping
    test_smb_error_mapping.cpp
)

target_link_libraries(test_smb_error_mapping
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME smb_error_mapping COMMAND test_smb_error_mapping)
