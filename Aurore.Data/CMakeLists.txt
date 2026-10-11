set(
    aurore_data_sources

    src/DataPreparation.cpp
    src/DataPreparationService.cpp
    src/FileIdentity.cpp
    src/JavaRuntime.cpp
    src/MinecraftDataCorpus.cpp
    src/MinecraftDataGenerator.cpp
    src/MinecraftSource.cpp
    src/MojangMetadata.cpp
    src/OfficialMinecraftSource.cpp
    src/Sha1.cpp
    src/VanillaBiomeImporter.cpp
    src/VanillaDataImporter.cpp
    src/VanillaDataImportSupport.cpp
    src/VanillaDimensionTypeImporter.cpp

    include/Aurore/Data/DataPreparation.hpp
    include/Aurore/Data/DataPreparationService.hpp
    include/Aurore/Data/MinecraftDataCorpus.hpp
    include/Aurore/Data/MinecraftSource.hpp
    include/Aurore/Data/VanillaDataImporter.hpp

    src/FileIdentity.hpp
    src/HttpClient.hpp
    src/JavaRuntime.hpp
    src/MinecraftDataGenerator.hpp
    src/MojangMetadata.hpp
    src/OfficialMinecraftSource.hpp
    src/Process.hpp
    src/Sha1.hpp
    src/VanillaDataImportSupport.hpp
)

if(AURORE_PLATFORM_WINDOWS)
    list(
        APPEND
        aurore_data_sources

        src/HttpClient.Windows.cpp
        src/Process.Windows.cpp
    )
elseif(AURORE_PLATFORM_LINUX)
    list(
        APPEND
        aurore_data_sources

        src/HttpClient.Linux.cpp
        src/Process.Linux.cpp
    )
else()
    list(
        APPEND
        aurore_data_sources

        src/HttpClient.Unsupported.cpp
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

    PUBLIC
    Aurore::Util

    PRIVATE
    Aurore::BuildOptions
    Sonnet::sonnet
    Threads::Threads
)

if(AURORE_PLATFORM_WINDOWS)
    target_link_libraries(
        aurore_data
        PRIVATE
        winhttp
    )
elseif(AURORE_PLATFORM_LINUX)
    set(CURL_USE_STATIC_LIBS OFF)
    find_package(CURL REQUIRED)

    target_link_libraries(
        aurore_data
        PRIVATE
        CURL::libcurl
    )
endif()

set_target_properties(
    aurore_data
    PROPERTIES
    OUTPUT_NAME "Aurore.Data"
    FOLDER "Aurore"
)
