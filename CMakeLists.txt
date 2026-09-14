cmake_minimum_required(VERSION 3.24)

project(KvLite VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(asio REQUIRED)

option(KVLITE_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src)
add_subdirectory(apps)

if (KVLITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
