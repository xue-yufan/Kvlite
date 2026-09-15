cmake_minimum_required(VERSION 3.24)

project(KvLite VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 源码含中文注释且为 UTF-8（无 BOM）：若按系统代码页(936)解析，
# MSVC 会把注释结尾的中文字节与下一行"粘"在一起，吞掉一行代码并报 C4819。
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Threads REQUIRED)
find_package(asio REQUIRED)

option(KVLITE_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src)
add_subdirectory(apps)

if (KVLITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
