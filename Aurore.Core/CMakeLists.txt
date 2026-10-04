add_library(
    aurore_core
    STATIC

    src/ClientLifecycle.cpp
    src/ClientLogin.cpp
    src/ClientService.cpp
    src/Configuration.cpp
    src/RegistrySnapshotStore.cpp
    src/Server.cpp
    src/SyntheticRegistrySnapshot.cpp
    src/Tick.cpp
    src/logger/Log.cpp

    include/Aurore/Core/ClientLifecycle.hpp
    include/Aurore/Core/ClientLogin.hpp
    include/Aurore/Core/ClientService.hpp
    include/Aurore/Core/Configuration.hpp
    include/Aurore/Core/RegistrySnapshotStore.hpp
    include/Aurore/Core/Server.hpp
    include/Aurore/Core/SyntheticRegistrySnapshot.hpp
    include/Aurore/Core/Tick.hpp
    include/Aurore/Core/logger/Log.hpp

    src/ServerTestAccess.hpp
)

add_library(Aurore::Core ALIAS aurore_core)

target_include_directories(
    aurore_core
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
    aurore_core

    PUBLIC
    Aurore::Network
    Aurore::Protocol
    Aurore::Util
    Aurore::World
    Sonnet::sonnet

    PRIVATE
    Aurore::BuildOptions
    Threads::Threads
)

set_target_properties(
    aurore_core
    PROPERTIES
    OUTPUT_NAME "Aurore.Core"
    FOLDER "Aurore"
)
