# Findasio.cmake - locate standalone (header-only) Asio.
#
# On success this defines the imported interface target `asio::asio`
# (plus a plain `asio` alias), matching what vcpkg's asio-config.cmake
# provides, so `find_package(asio REQUIRED)` + `target_link_libraries(...
# asio::asio)` work unchanged.
#
# The bundled copy lives in dependencies/asio/include. Set ASIO_ROOT to
# point at another installation root if you prefer.

find_path(asio_INCLUDE_DIR
    NAMES asio.hpp
    HINTS
        "${ASIO_ROOT}"
        "${CMAKE_CURRENT_LIST_DIR}/../dependencies/asio"
    PATH_SUFFIXES include
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(asio
    REQUIRED_VARS asio_INCLUDE_DIR
)

if(asio_FOUND AND NOT TARGET asio::asio)
    add_library(asio::asio INTERFACE IMPORTED)

    set_target_properties(asio::asio PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${asio_INCLUDE_DIR}"
        INTERFACE_COMPILE_DEFINITIONS "ASIO_STANDALONE"
    )

    if(WIN32)
        # Asio wants _WIN32_WINNT set explicitly; otherwise MSVC falls back
        # to the Windows 7 default and emits a warning on every compile.
        set_property(TARGET asio::asio APPEND PROPERTY
            INTERFACE_COMPILE_DEFINITIONS "_WIN32_WINNT=0x0601"
        )
    endif()

    if(NOT Threads_FOUND)
        find_package(Threads QUIET)
    endif()

    if(Threads_FOUND)
        set_property(TARGET asio::asio APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES Threads::Threads
        )
    endif()

    if(WIN32)
        set_property(TARGET asio::asio APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES ws2_32 mswsock
        )
    endif()

    if(NOT TARGET asio)
        add_library(asio ALIAS asio::asio)
    endif()
endif()

mark_as_advanced(asio_INCLUDE_DIR)
