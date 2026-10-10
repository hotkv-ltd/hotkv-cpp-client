# Copyright (c) 2026 HotKV Ltd
# Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

# Use an installed GoogleTest when there is one (offline builds, distro and
# vcpkg packaging); otherwise download the pinned release.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)

    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    FetchContent_MakeAvailable(googletest)
endif()

function(hotkv_cpp_add_test_binary name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE hotkv::hotkv-cpp GTest::gtest_main)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(MINGW)
        # Let gtest_discover_tests run the program at build time without
        # the MinGW runtime DLLs on PATH.
        target_link_options(${name} PRIVATE -static)
    endif()
    include(GoogleTest)
    gtest_discover_tests(${name} DISCOVERY_TIMEOUT 60)
endfunction()

hotkv_cpp_add_test_binary(hotkv_cpp_unit_tests
    unit/test_resp_codec.cpp
    unit/test_options.cpp
    unit/test_crc16.cpp
    unit/test_ai_cache_key.cpp
    unit/test_reply.cpp
    unit/test_scan.cpp
    unit/test_enterprise_usage.cpp
    unit/test_numfmt.cpp
    unit/test_cluster_redirects.cpp
    unit/test_tls_options.cpp
)

hotkv_cpp_add_test_binary(hotkv_cpp_integration_tests
    integration/test_helpers.cpp
    integration/test_core_strings_keys.cpp
    integration/test_core_collections.cpp
    integration/test_core_streams_geo_bitmaps.cpp
    integration/test_core_server_scripting_scan.cpp
    integration/test_core_extended.cpp
    integration/test_remote.cpp
    integration/test_tls_auth.cpp
    integration/test_enterprise_float_args.cpp
    integration/test_pipeline_transaction.cpp
    integration/test_pubsub.cpp
    integration/test_resp_conformance.cpp
    integration/test_cluster.cpp
    integration/test_enterprise_core.cpp
    integration/test_enterprise_ai_cache.cpp
    integration/test_enterprise_agent_rag.cpp
    integration/test_enterprise_admin.cpp
    integration/test_ai_helpers.cpp
)
