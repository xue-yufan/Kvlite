add_executable(kvlite_server
  kvlite_server/main.cpp
)

target_link_libraries(kvlite_server
    PRIVATE
        kvlite_core
)

add_executable(kvlite_client
    kvlite_client/main.cpp
)

target_link_libraries(kvlite_client
    PRIVATE
        kvlite_core
)