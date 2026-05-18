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

include(GNUInstallDirs)
include(CTest)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Sql Widgets Test)
find_package(Qt5Keychain REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

set(SMB_BROWSER_LOCAL_LIBSMB2_PKGCONFIG
    "${CMAKE_SOURCE_DIR}/tmp/libsmb2-prefix/lib/pkgconfig"
)
if(EXISTS "${SMB_BROWSER_LOCAL_LIBSMB2_PKGCONFIG}/libsmb2.pc")
    set(ENV{PKG_CONFIG_PATH}
        "${SMB_BROWSER_LOCAL_LIBSMB2_PKGCONFIG}:$ENV{PKG_CONFIG_PATH}"
    )
endif()

pkg_check_modules(LIBSMB2 QUIET IMPORTED_TARGET libsmb2)
option(SMB_BROWSER_WITH_LIBSMB2 "Build the libsmb2 SMB backend" ${LIBSMB2_FOUND})
if(SMB_BROWSER_WITH_LIBSMB2 AND NOT LIBSMB2_FOUND)
    message(FATAL_ERROR
        "SMB_BROWSER_WITH_LIBSMB2 is ON, but pkg-config could not find libsmb2. "
        "Set PKG_CONFIG_PATH to the libsmb2 pkgconfig directory."
    )
endif()

option(SMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS
    "Enable opt-in Docker Samba integration tests"
    OFF
)

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
        "libqt5core5a, libqt5gui5, libqt5widgets5, libqt5sql5, libqt5sql5-sqlite, libqt5keychain1, libsodium23"
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
