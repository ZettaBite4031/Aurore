add_library(
    aurore_util
    STATIC

    src/ByteBuffer.cpp
    src/Nbt.cpp
    src/NbtBinary.cpp
    src/NetworkRegistry.cpp
    src/RegistrySnapshot.cpp
    src/ResourceLocation.cpp
    src/Uuid.cpp

    include/Aurore/Util/ByteBuffer.hpp
    include/Aurore/Util/Nbt.hpp
    include/Aurore/Util/NbtBinary.hpp
    include/Aurore/Util/NetworkRegistry.hpp
    include/Aurore/Util/Registry.hpp
    include/Aurore/Util/RegistrySnapshot.hpp
    include/Aurore/Util/RegistryTag.hpp
    include/Aurore/Util/ResourceLocation.hpp
    include/Aurore/Util/UUID.hpp
)

add_library(Aurore::Util ALIAS aurore_util)

target_include_directories(
    aurore_util
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
    aurore_util
    PRIVATE
    Aurore::BuildOptions
)

set_target_properties(
    aurore_util
    PROPERTIES
    OUTPUT_NAME "Aurore.Util"
    FOLDER "Aurore"
)
