add_library(
    aurore_data
    STATIC

    src/DataPreparation.cpp

    include/Aurore/Data/DataPreparation.hpp
)

add_library(Aurore::Data ALIAS aurore_data)

target_include_directories(
    aurore_data
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
    aurore_data

    PRIVATE
    Aurore::BuildOptions
)

set_target_properties(
    aurore_data
    PROPERTIES
    OUTPUT_NAME "Aurore.Data"
    FOLDER "Aurore"
)
