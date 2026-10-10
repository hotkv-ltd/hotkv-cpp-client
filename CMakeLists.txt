# Copyright (c) 2026 HotKV Ltd
# Licensed under the Apache License, Version 2.0. See LICENSE in the project root.
#
# Author: HotKV Ltd <https://hotkv.com>
# Repository: https://github.com/hotkv-ltd/hotkv-cpp-client
# License: Apache-2.0 (see LICENSE in the project root)

cmake_minimum_required(VERSION 3.20)

project(hotkv-cpp
    VERSION 1.0.0
    DESCRIPTION "A high-performance C++ client for HotKV, a RESP2/RESP3-compatible in-memory data store with built-in AI and LLM-serving features."
    HOMEPAGE_URL "https://hotkv.com"
    LANGUAGES CXX
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(HOTKV_CPP_IS_TOP_LEVEL ON)
else()
    set(HOTKV_CPP_IS_TOP_LEVEL OFF)
endif()

if(HOTKV_CPP_IS_TOP_LEVEL AND CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "hotkv-cpp: in-source builds are not supported; configure into a separate build directory")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

option(HOTKV_CPP_WITH_TLS "Build with TLS support via OpenSSL" ON)
option(HOTKV_CPP_BUILD_TESTS "Build unit and integration tests" ${HOTKV_CPP_IS_TOP_LEVEL})

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)

if(HOTKV_CPP_WITH_TLS)
    find_package(OpenSSL)
    if(NOT OpenSSL_FOUND)
        message(WARNING "hotkv-cpp: HOTKV_CPP_WITH_TLS is ON but OpenSSL was not found; building without TLS support")
        set(HOTKV_CPP_WITH_TLS OFF)
    endif()
endif()

if(WIN32 AND HOTKV_CPP_IS_TOP_LEVEL)
    # Keep a shared build's DLL next to the test executables so ctest finds it.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

set(HOTKV_CPP_SOURCES
    src/ai.cpp
    src/client_bitmaps.cpp
    src/client_core.cpp
    src/client_enterprise_accessors.cpp
    src/client_generic.cpp
    src/client_geo.cpp
    src/client_hashes.cpp
    src/client_hyperloglog.cpp
    src/client_impl.cpp
    src/client_lists.cpp
    src/client_more.cpp
    src/client_scan.cpp
    src/client_scripting.cpp
    src/client_server.cpp
    src/client_sets.cpp
    src/client_streams.cpp
    src/client_strings.cpp
    src/client_zsets.cpp
    src/cluster.cpp
    src/connection.cpp
    src/connection_pool.cpp
    src/crc16.cpp
    src/enterprise_admin.cpp
    src/enterprise_amem.cpp
    src/enterprise_keyed_cache.cpp
    src/enterprise_llm.cpp
    src/enterprise_rag.cpp
    src/enterprise_ratelimit_ts_feature.cpp
    src/enterprise_scache_pcache.cpp
    src/enterprise_tag_hotkeys_gcra.cpp
    src/enterprise_vsim.cpp
    src/error.cpp
    src/options.cpp
    src/pairs_util.cpp
    src/pipeline.cpp
    src/reply.cpp
    src/resp_codec.cpp
    src/scan.cpp
    src/sha1.cpp
    src/sha256.cpp
    src/socket_io.cpp
    src/subscriber.cpp
    src/transaction.cpp
    src/usage_util.cpp
)

add_library(hotkv-cpp ${HOTKV_CPP_SOURCES})
add_library(hotkv::hotkv-cpp ALIAS hotkv-cpp)

configure_file(include/hotkv/version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/hotkv/version.hpp @ONLY)

target_include_directories(hotkv-cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(hotkv-cpp PUBLIC cxx_std_20)

set_target_properties(hotkv-cpp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME hotkv-cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hotkv-cpp PRIVATE -Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    # Kept compile-plausible: this SDK is not built or tested on MSVC in
    # this environment, but the Winsock branch in src/socket_io.cpp is
    # written to compile there.
    target_compile_options(hotkv-cpp PRIVATE /W4)
endif()

target_link_libraries(hotkv-cpp PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(hotkv-cpp PRIVATE ws2_32)
    if(BUILD_SHARED_LIBS)
        # The public headers carry no export macros, so a Windows DLL
        # exports every symbol (and gets its import library). Untested on
        # MSVC in this repository's CI environment.
        set_target_properties(hotkv-cpp PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
endif()

if(HOTKV_CPP_WITH_TLS)
    # OpenSSL is an implementation detail: no public header includes it.
    # PRIVATE still propagates to consumers of a *static* library (as a
    # link-only dependency, which hotkv-cpp-config.cmake resolves with
    # find_dependency(OpenSSL)) and stays hidden for a shared one.
    target_link_libraries(hotkv-cpp PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(hotkv-cpp PRIVATE HOTKV_CPP_WITH_TLS)
    set(HOTKV_CPP_PC_REQUIRES_LINE "Requires.private: openssl")
else()
    set(HOTKV_CPP_PC_REQUIRES_LINE "")
endif()
if(HOTKV_CPP_WITH_TLS AND NOT BUILD_SHARED_LIBS)
    set(HOTKV_CPP_CONFIG_NEEDS_OPENSSL TRUE)
else()
    set(HOTKV_CPP_CONFIG_NEEDS_OPENSSL FALSE)
endif()

# pkg-config: the installed .pc locates the install tree relative to
# itself (prefix=${pcfiledir}/../..), so a moved or staged tree (an
# --install --prefix package unpacked elsewhere) keeps working. The number
# of ".." comes from CMAKE_INSTALL_LIBDIR (lib, lib64, lib/x86_64-linux-gnu).
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(HOTKV_CPP_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
else()
    # One ".." per path component of <libdir>/pkgconfig.
    string(REGEX MATCHALL "[^/]+" HOTKV_CPP_PC_LIBDIR_PARTS "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
    set(HOTKV_CPP_PC_REL_PREFIX "")
    foreach(part IN LISTS HOTKV_CPP_PC_LIBDIR_PARTS)
        string(APPEND HOTKV_CPP_PC_REL_PREFIX "/..")
    endforeach()
    string(SUBSTRING "${HOTKV_CPP_PC_REL_PREFIX}" 1 -1 HOTKV_CPP_PC_REL_PREFIX)
    set(HOTKV_CPP_PC_PREFIX "\${pcfiledir}/${HOTKV_CPP_PC_REL_PREFIX}")
endif()
if(WIN32)
    set(HOTKV_CPP_PC_LIBS_PRIVATE "-lws2_32")
else()
    set(HOTKV_CPP_PC_LIBS_PRIVATE "-pthread")
endif()

message(STATUS "hotkv-cpp: TLS support (HOTKV_CPP_WITH_TLS) = ${HOTKV_CPP_WITH_TLS}")

# --- install ---

install(TARGETS hotkv-cpp
    EXPORT hotkv-cpp-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/hotkv DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/hotkv/version.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hotkv)

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/hotkv-cpp)

install(EXPORT hotkv-cpp-targets
    FILE hotkv-cpp-targets.cmake
    NAMESPACE hotkv::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hotkv-cpp
)

configure_package_config_file(
    cmake/hotkv-cpp-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/hotkv-cpp-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hotkv-cpp
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/hotkv-cpp-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/hotkv-cpp-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/hotkv-cpp-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hotkv-cpp
)

configure_file(cmake/hotkv-cpp.pc.in ${CMAKE_CURRENT_BINARY_DIR}/hotkv-cpp.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hotkv-cpp.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(HOTKV_CPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
