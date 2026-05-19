add_library(smb_browser_test_support STATIC
    fakes/FakeSmbClient.cpp
    fakes/FakeSmbClient.h
)

target_include_directories(smb_browser_test_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(smb_browser_test_support
    PUBLIC
        smb_browser_core
)

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

add_executable(test_log_sanitizer
    test_log_sanitizer.cpp
)

target_link_libraries(test_log_sanitizer
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME log_sanitizer COMMAND test_log_sanitizer)

add_executable(test_sqlite_storage
    test_sqlite_storage.cpp
)

target_link_libraries(test_sqlite_storage
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME sqlite_storage COMMAND test_sqlite_storage)

add_executable(test_connection_repository
    test_connection_repository.cpp
)

target_link_libraries(test_connection_repository
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME connection_repository COMMAND test_connection_repository)

add_executable(test_connection_group_repository
    test_connection_group_repository.cpp
)

target_link_libraries(test_connection_group_repository
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME connection_group_repository COMMAND test_connection_group_repository)

add_executable(test_settings_repository
    test_settings_repository.cpp
)

target_link_libraries(test_settings_repository
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME settings_repository COMMAND test_settings_repository)

add_executable(test_file_logger
    test_file_logger.cpp
)

target_link_libraries(test_file_logger
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME file_logger COMMAND test_file_logger)

add_executable(test_smbclient_dfs_referral_resolver
    test_smbclient_dfs_referral_resolver.cpp
)

target_link_libraries(test_smbclient_dfs_referral_resolver
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME smbclient_dfs_referral_resolver
         COMMAND test_smbclient_dfs_referral_resolver)

add_executable(test_dfs_resolving_smb_client
    test_dfs_resolving_smb_client.cpp
)

target_link_libraries(test_dfs_resolving_smb_client
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME dfs_resolving_smb_client COMMAND test_dfs_resolving_smb_client)

if(SMB_BROWSER_WITH_LIBSMB2)
    add_executable(test_libsmb2_error_mapper
        test_libsmb2_error_mapper.cpp
    )

    target_link_libraries(test_libsmb2_error_mapper
        PRIVATE
            smb_browser_infrastructure
            Qt5::Test
    )

    add_test(NAME libsmb2_error_mapper COMMAND test_libsmb2_error_mapper)

    add_executable(test_libsmb2_smb_client
        test_libsmb2_smb_client.cpp
    )

    target_link_libraries(test_libsmb2_smb_client
        PRIVATE
            smb_browser_infrastructure
            Qt5::Test
    )

    add_test(NAME libsmb2_smb_client COMMAND test_libsmb2_smb_client)
endif()

if(SMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS AND SMB_BROWSER_WITH_LIBSMB2)
    add_executable(test_docker_samba_integration
        test_docker_samba_integration.cpp
    )

    target_link_libraries(test_docker_samba_integration
        PRIVATE
            smb_browser_infrastructure
            Qt5::Test
    )

    add_test(NAME docker_samba_integration
             COMMAND test_docker_samba_integration)
    set_tests_properties(docker_samba_integration PROPERTIES
        LABELS "docker-samba;integration"
        RUN_SERIAL TRUE
        ENVIRONMENT
            "SMB_BROWSER_SAMBA_SERVER=127.0.0.1:1445;SMB_BROWSER_SAMBA_SHARE=public;SMB_BROWSER_SAMBA_USER=smbtest;SMB_BROWSER_SAMBA_PASSWORD=synthetic-password"
    )
elseif(SMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS)
    message(WARNING
        "SMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS is ON, but libsmb2 backend is disabled."
    )
endif()

add_executable(test_credential_store_contract
    test_credential_store_contract.cpp
)

target_link_libraries(test_credential_store_contract
    PRIVATE
        smb_browser_core
        Qt5::Test
)

add_test(NAME credential_store_contract COMMAND test_credential_store_contract)

add_executable(test_qtkeychain_credential_store
    test_qtkeychain_credential_store.cpp
)

target_link_libraries(test_qtkeychain_credential_store
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME qtkeychain_credential_store COMMAND test_qtkeychain_credential_store)

add_executable(test_encrypted_vault_credential_store
    test_encrypted_vault_credential_store.cpp
)

target_link_libraries(test_encrypted_vault_credential_store
    PRIVATE
        smb_browser_infrastructure
        Qt5::Test
)

add_test(NAME encrypted_vault_credential_store COMMAND test_encrypted_vault_credential_store)

add_executable(test_fake_smb_client
    test_fake_smb_client.cpp
)

target_link_libraries(test_fake_smb_client
    PRIVATE
        smb_browser_test_support
        Qt5::Test
)

add_test(NAME fake_smb_client COMMAND test_fake_smb_client)

add_executable(test_connection_service
    test_connection_service.cpp
)

target_link_libraries(test_connection_service
    PRIVATE
        smb_browser_application
        Qt5::Test
)

add_test(NAME connection_service COMMAND test_connection_service)

add_executable(test_connectivity_check_service
    test_connectivity_check_service.cpp
)

target_link_libraries(test_connectivity_check_service
    PRIVATE
        smb_browser_application
        smb_browser_test_support
        Qt5::Test
)

add_test(NAME connectivity_check_service COMMAND test_connectivity_check_service)

add_executable(test_connection_open_service
    test_connection_open_service.cpp
)

target_link_libraries(test_connection_open_service
    PRIVATE
        smb_browser_application
        smb_browser_test_support
        Qt5::Test
)

add_test(NAME connection_open_service COMMAND test_connection_open_service)

add_executable(test_connection_import_export_service
    test_connection_import_export_service.cpp
)

target_link_libraries(test_connection_import_export_service
    PRIVATE
        smb_browser_application
        Qt5::Test
)

add_test(NAME connection_import_export_service
         COMMAND test_connection_import_export_service)

add_executable(test_operation_queue
    test_operation_queue.cpp
)

target_link_libraries(test_operation_queue
    PRIVATE
        smb_browser_application
        Qt5::Test
)

add_test(NAME operation_queue COMMAND test_operation_queue)

add_executable(test_transfer_manager
    test_transfer_manager.cpp
)

target_link_libraries(test_transfer_manager
    PRIVATE
        smb_browser_application
        smb_browser_test_support
        Qt5::Test
)

add_test(NAME transfer_manager COMMAND test_transfer_manager)

add_executable(test_temp_file_cache
    test_temp_file_cache.cpp
)

target_link_libraries(test_temp_file_cache
    PRIVATE
        smb_browser_application
        Qt5::Test
)

add_test(NAME temp_file_cache COMMAND test_temp_file_cache)

add_executable(test_open_file_service
    test_open_file_service.cpp
)

target_link_libraries(test_open_file_service
    PRIVATE
        smb_browser_application
        smb_browser_test_support
        Qt5::Test
)

add_test(NAME open_file_service COMMAND test_open_file_service)

add_executable(test_preview_service
    test_preview_service.cpp
)

target_link_libraries(test_preview_service
    PRIVATE
        smb_browser_application
        Qt5::Test
)

add_test(NAME preview_service COMMAND test_preview_service)

add_executable(test_recursive_search_service
    test_recursive_search_service.cpp
)

target_link_libraries(test_recursive_search_service
    PRIVATE
        smb_browser_application
        Qt5::Test
)

add_test(NAME recursive_search_service COMMAND test_recursive_search_service)

add_executable(test_import_export_service
    test_import_export_service.cpp
)

target_link_libraries(test_import_export_service
    PRIVATE
        smb_browser_application
        Qt5::Test
)

add_test(NAME import_export_service COMMAND test_import_export_service)

add_executable(test_settings_service
    test_settings_service.cpp
)

target_link_libraries(test_settings_service
    PRIVATE
        smb_browser_application
        Qt5::Test
)

add_test(NAME settings_service COMMAND test_settings_service)

add_executable(test_remote_file_model
    test_remote_file_model.cpp
)

target_link_libraries(test_remote_file_model
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME remote_file_model COMMAND test_remote_file_model)
set_tests_properties(remote_file_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_remote_browser_widget
    test_remote_browser_widget.cpp
)

target_link_libraries(test_remote_browser_widget
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME remote_browser_widget COMMAND test_remote_browser_widget)
set_tests_properties(remote_browser_widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_log_viewer
    test_log_viewer.cpp
)

target_link_libraries(test_log_viewer
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME log_viewer COMMAND test_log_viewer)
set_tests_properties(log_viewer PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_theme_manager
    test_theme_manager.cpp
)

target_link_libraries(test_theme_manager
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME theme_manager COMMAND test_theme_manager)
set_tests_properties(theme_manager PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_localization_manager
    test_localization_manager.cpp
)

target_link_libraries(test_localization_manager
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

if(TARGET smb_browser_translations)
    add_dependencies(test_localization_manager smb_browser_translations)
endif()

add_test(NAME localization_manager COMMAND test_localization_manager)
set_tests_properties(localization_manager PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_main_window
    test_main_window.cpp
)

target_link_libraries(test_main_window
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME main_window COMMAND test_main_window)
set_tests_properties(main_window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_connections_panel
    test_connections_panel.cpp
)

target_link_libraries(test_connections_panel
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME connections_panel COMMAND test_connections_panel)
set_tests_properties(connections_panel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_connection_dialog
    test_connection_dialog.cpp
)

target_link_libraries(test_connection_dialog
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME connection_dialog COMMAND test_connection_dialog)
set_tests_properties(connection_dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_settings_dialog
    test_settings_dialog.cpp
)

target_link_libraries(test_settings_dialog
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME settings_dialog COMMAND test_settings_dialog)
set_tests_properties(settings_dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_status_panel
    test_status_panel.cpp
)

target_link_libraries(test_status_panel
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME status_panel COMMAND test_status_panel)
set_tests_properties(status_panel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_import_export_controller
    test_import_export_controller.cpp
)

target_link_libraries(test_import_export_controller
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME import_export_controller COMMAND test_import_export_controller)
set_tests_properties(import_export_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_ui_smoke
    test_ui_smoke.cpp
)

target_link_libraries(test_ui_smoke
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

if(TARGET smb_browser_translations)
    add_dependencies(test_ui_smoke smb_browser_translations)
endif()

add_test(NAME ui_smoke COMMAND test_ui_smoke)
set_tests_properties(ui_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_connection_management_controller
    test_connection_management_controller.cpp
)

target_link_libraries(test_connection_management_controller
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME connection_management_controller COMMAND test_connection_management_controller)
set_tests_properties(connection_management_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_connectivity_check_controller
    test_connectivity_check_controller.cpp
)

target_link_libraries(test_connectivity_check_controller
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME connectivity_check_controller COMMAND test_connectivity_check_controller)
set_tests_properties(connectivity_check_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_connection_open_controller
    test_connection_open_controller.cpp
)

target_link_libraries(test_connection_open_controller
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME connection_open_controller COMMAND test_connection_open_controller)
set_tests_properties(connection_open_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_connection_clipboard_controller
    test_connection_clipboard_controller.cpp
)

target_link_libraries(test_connection_clipboard_controller
    PRIVATE
        smb_browser_ui
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME connection_clipboard_controller COMMAND test_connection_clipboard_controller)
set_tests_properties(connection_clipboard_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
