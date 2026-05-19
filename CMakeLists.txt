cmake_minimum_required(VERSION 3.16)

project(SmbBrowser
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Qt Widgets SMB browser"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(FetchContent)
include(GNUInstallDirs)
include(CTest)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Sql Svg Widgets Test)
find_package(Qt5Keychain REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

option(SMB_BROWSER_WITH_LIBSMB2 "Build the legacy libsmb2 SMB backend" OFF)
option(SMB_BROWSER_USE_SYSTEM_LIBSMB2
    "Use a system/pkg-config libsmb2 instead of fetching the pinned source"
    OFF
)
set(SMB_BROWSER_LIBSMB2_REPOSITORY
    "https://github.com/sahlberg/libsmb2.git"
    CACHE STRING "libsmb2 git repository"
)
set(SMB_BROWSER_LIBSMB2_TAG
    "libsmb2-6.2"
    CACHE STRING "libsmb2 git tag or commit"
)
set(SMB_BROWSER_LIBSMB2_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/tmp/libsmb2-src"
    CACHE PATH "Local libsmb2 source checkout used by FetchContent"
)
set(SMB_BROWSER_LIBSMB2_TARGET "")

if(SMB_BROWSER_WITH_LIBSMB2)
    if(SMB_BROWSER_USE_SYSTEM_LIBSMB2)
        set(SMB_BROWSER_LOCAL_LIBSMB2_PKGCONFIG
            "${CMAKE_SOURCE_DIR}/tmp/libsmb2-prefix/lib/pkgconfig"
        )
        if(EXISTS "${SMB_BROWSER_LOCAL_LIBSMB2_PKGCONFIG}/libsmb2.pc")
            set(ENV{PKG_CONFIG_PATH}
                "${SMB_BROWSER_LOCAL_LIBSMB2_PKGCONFIG}:$ENV{PKG_CONFIG_PATH}"
            )
        endif()

        pkg_check_modules(LIBSMB2 REQUIRED IMPORTED_TARGET libsmb2)
        set(SMB_BROWSER_LIBSMB2_TARGET PkgConfig::LIBSMB2)
    else()
        find_package(Git REQUIRED)
        set(ENABLE_EXAMPLES OFF CACHE BOOL "Build libsmb2 examples" FORCE)
        set(ENABLE_LIBKRB5 OFF CACHE BOOL "Enable libsmb2 libkrb5 support" FORCE)
        set(ENABLE_GSSAPI OFF CACHE BOOL "Enable libsmb2 GSSAPI support" FORCE)
        set(INSTALL_LIB_DIR "${CMAKE_INSTALL_LIBDIR}"
            CACHE PATH "libsmb2 library install directory" FORCE
        )
        set(INSTALL_INC_DIR "${CMAKE_INSTALL_INCLUDEDIR}"
            CACHE PATH "libsmb2 include install directory" FORCE
        )
        set(INSTALL_PKGCONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
            CACHE PATH "libsmb2 pkg-config install directory" FORCE
        )
        set(INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/libsmb2"
            CACHE PATH "libsmb2 CMake helper install directory" FORCE
        )

        FetchContent_Declare(libsmb2
            GIT_REPOSITORY "${SMB_BROWSER_LIBSMB2_REPOSITORY}"
            GIT_TAG "${SMB_BROWSER_LIBSMB2_TAG}"
            GIT_SHALLOW TRUE
            SOURCE_DIR "${SMB_BROWSER_LIBSMB2_SOURCE_DIR}"
        )
        FetchContent_MakeAvailable(libsmb2)
        if(NOT TARGET smb2)
            message(FATAL_ERROR
                "Fetched libsmb2 did not define expected target 'smb2'."
            )
        endif()
        set(SMB_BROWSER_LIBSMB2_TARGET smb2)
    endif()
endif()

option(SMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS
    "Enable opt-in Docker Samba integration tests"
    OFF
)
option(SMB_BROWSER_WITH_NATIVE_SMB
    "Build the clean-room native SMB engine scaffold"
    ON
)
option(SMB_BROWSER_ENABLE_HARDENING
    "Enable release compiler/linker hardening flags where supported"
    ON
)

if(SMB_BROWSER_ENABLE_HARDENING)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            "$<$<CONFIG:Release>:-fstack-protector-strong>"
            "$<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>"
        )
        if(UNIX AND NOT APPLE)
            add_link_options(
                "$<$<CONFIG:Release>:-Wl,-z,relro>"
                "$<$<CONFIG:Release>:-Wl,-z,now>"
            )
        endif()
    elseif(MSVC)
        add_compile_options("$<$<CONFIG:Release>:/guard:cf>")
        add_link_options("$<$<CONFIG:Release>:/guard:cf>")
    endif()
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(CPACK_PACKAGE_NAME "smb-browser")
set(CPACK_PACKAGE_VENDOR "SMB Browser")
set(CPACK_PACKAGE_CONTACT "SMB Browser Maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Cross-platform Qt Widgets SMB browser"
)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/PRD.md")

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/smb-browser.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES packaging/linux/io.github.smb_browser.SmbBrowser.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
    install(FILES packaging/icons/smb-browser.svg
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)

    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "SMB Browser Maintainers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt5core5a, libqt5gui5, libqt5widgets5, libqt5sql5, libqt5sql5-sqlite, libqt5svg5, libqt5keychain1, libsodium23"
    )
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "SMB Browser")
    set(CPACK_NSIS_PACKAGE_NAME "SMB Browser")
    set(CPACK_NSIS_MODIFY_PATH ON)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "SMB Browser")
    set(CPACK_BUNDLE_NAME "SMB Browser")
endif()

include(CPack)
