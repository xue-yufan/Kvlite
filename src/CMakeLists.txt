add_library(kvlite_core)

target_sources(kvlite_core
    PRIVATE
        storage/storage.cpp
        protocol/protocol.cpp
        net/net.cpp
        thread/thread_pool.cpp
)

target_include_directories(kvlite_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(kvlite_core PUBLIC cxx_std_23)

target_link_libraries(kvlite_core
    PRIVATE
        Threads::Threads
        asio::asio
)