set(
    aurore_data_sources

    src/DataPreparation.cpp
    src/MinecraftSource.cpp
    src/Sha1.cpp

    include/Aurore/Data/DataPreparation.hpp
    include/Aurore/Data/MinecraftSource.hpp

    src/Process.hpp
    src/Sha1.hpp
)

if(AURORE_PLATFORM_WINDOWS)
    list(
        APPEND
        aurore_data_sources

        src/Process.Windows.cpp
    )
elseif(AURORE_PLATFORM_LINUX)
    list(
        APPEND
        aurore_data_sources

        src/Process.Linux.cpp
    )
else()
    list(
        APPEND
        aurore_data_sources

        src/Process.Unsupported.cpp
    )
endif()

add_library(
    aurore_data
    STATIC
    ${aurore_data_sources}
)

add_library(Aurore::Data ALIAS aurore_data)

target_include_directories(
    aurore_data

    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"

    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    aurore_data

    PRIVATE
    Aurore::BuildOptions
    Threads::Threads
)

set_target_properties(
    aurore_data
    PROPERTIES
    OUTPUT_NAME "Aurore.Data"
    FOLDER "Aurore"
)
