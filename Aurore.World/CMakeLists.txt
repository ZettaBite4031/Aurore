add_library(
    aurore_world
    STATIC

    src/Dimension.cpp
    src/WorldManager.cpp

    include/Aurore/World/Dimension.hpp
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
    PRIVATE
    Aurore::BuildOptions
)

set_target_properties(
    aurore_world
    PROPERTIES
    OUTPUT_NAME "Aurore.World"
    FOLDER "Aurore"
)
