# Copyright (c) 2026 HotKV Ltd
# Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

# A minimal downstream project: finds an *installed* hotkv-cpp with
# find_package and links the imported target. Driven by
# scripts/verify-install.sh; not part of the normal build.
cmake_minimum_required(VERSION 3.20)
project(hotkv_cpp_consumer LANGUAGES CXX)

find_package(hotkv-cpp CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE hotkv::hotkv-cpp)
