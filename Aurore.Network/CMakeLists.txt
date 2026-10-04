set(
    aurore_network_sources

    src/NetworkBackend.cpp
    src/NetworkManager.cpp

    include/Aurore/Network/NetworkManager.hpp
    include/Aurore/Network/NetworkTypes.hpp

    src/NetworkBackend.hpp
    src/NetworkManagerTestAccess.hpp
)

if(AURORE_NETWORK_BACKEND_IOCP)
    list(
        APPEND
        aurore_network_sources

        src/windows/WindowsBackend.cpp
        src/windows/WindowsBackend.hpp
        src/windows/WindowsBackendTest.hpp
        src/windows/WindowsTypes.hpp
        src/windows/WindowsUtils.hpp
    )
endif()

if(AURORE_NETWORK_BACKEND_EPOLL)
    list(
        APPEND
        aurore_network_sources

        src/linux/LinuxBackend.cpp
        src/linux/LinuxBackend.hpp
        src/linux/LinuxTypes.hpp
        src/linux/LinuxUtils.hpp
    )
endif()

add_library(
    aurore_network
    STATIC
    ${aurore_network_sources}
)

add_library(Aurore::Network ALIAS aurore_network)

target_include_directories(
    aurore_network

    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"

    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    aurore_network

    PUBLIC
    Aurore::Util

    PRIVATE
    Aurore::BuildOptions
    Threads::Threads
)

if(AURORE_NETWORK_BACKEND_IOCP)
    target_link_libraries(
        aurore_network
        PRIVATE
        ws2_32
        mswsock
    )
endif()

set_target_properties(
    aurore_network
    PROPERTIES
    OUTPUT_NAME "Aurore.Network"
    FOLDER "Aurore"
)
