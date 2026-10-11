add_library(
    aurore_world
    STATIC

    src/Chunk.cpp
    src/Dimension.cpp
    src/Player.cpp
    src/WorldManager.cpp

    include/Aurore/World/Chunk.hpp
    include/Aurore/World/Dimension.hpp
    include/Aurore/World/Player.hpp
    include/Aurore/World/WorldManager.hpp
)

add_library(Aurore::World ALIAS aurore_world)

target_include_directories(
    aurore_world
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
    aurore_world

    PUBLIC
    Aurore::Util

    PRIVATE
    Aurore::BuildOptions
)

set_target_properties(
    aurore_world
    PROPERTIES
    OUTPUT_NAME "Aurore.World"
    FOLDER "Aurore"
)
