set(KVLITE_GOOGLETEST_DIR "${PROJECT_SOURCE_DIR}/dependencies/googletest")

# Windows 下必须与父工程使用同一套 CRT，否则会出现 LNK2038
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

if(EXISTS "${KVLITE_GOOGLETEST_DIR}/CMakeLists.txt")
    # 优先使用随仓库提供的 googletest 源码（与 dependencies/asio 同样的方式），
    # 不依赖网络，也让编辑器的 includePath 有稳定路径可指
    add_subdirectory("${KVLITE_GOOGLETEST_DIR}" "${CMAKE_BINARY_DIR}/_deps/googletest-build")
else()
    # 兜底：本地副本缺失时通过 git 拉取
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(kvlite_test
    test_storage.cpp
    test_protocol.cpp
    test_storage_concurrent.cpp
)

target_link_libraries(kvlite_test
    PRIVATE
        kvlite_core
        GTest::gtest_main
)

# --gtest_fail_if_no_test_linked：用例文件被清空时让测试失败，而不是静默通过
add_test(NAME kvlite_test COMMAND kvlite_test --gtest_fail_if_no_test_linked)
