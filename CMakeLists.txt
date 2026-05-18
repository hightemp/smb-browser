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

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
