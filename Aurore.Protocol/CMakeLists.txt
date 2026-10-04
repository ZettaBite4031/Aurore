add_library(
    aurore_protocol
    STATIC

    src/ConfigurationSequence.cpp
    src/PacketCodec.cpp
    src/PacketFrame.cpp
    src/ProtocolConnection.cpp
    src/ProtocolDiagnostics.cpp
    src/ProtocolPipeline.cpp
    src/ProtocolSession.cpp
    src/RegistryCompatibility.cpp

    src/Packets/Configuration.cpp
    src/Packets/Handshake.cpp
    src/Packets/Login.cpp
    src/Packets/Status.cpp

    include/Aurore/Protocol/ConfigurationSequence.hpp
    include/Aurore/Protocol/PacketCodec.hpp
    include/Aurore/Protocol/PacketFrame.hpp
    include/Aurore/Protocol/ProtocolConnection.hpp
    include/Aurore/Protocol/ProtocolDiagnostics.hpp
    include/Aurore/Protocol/ProtocolPipeline.hpp
    include/Aurore/Protocol/ProtocolSession.hpp
    include/Aurore/Protocol/ProtocolTypes.hpp
    include/Aurore/Protocol/RegistryCompatibility.hpp

    include/Aurore/Protocol/Packets/Configuration.hpp
    include/Aurore/Protocol/Packets/Handshake.hpp
    include/Aurore/Protocol/Packets/Login.hpp
    include/Aurore/Protocol/Packets/Status.hpp
)

add_library(Aurore::Protocol ALIAS aurore_protocol)

target_include_directories(
    aurore_protocol
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
    aurore_protocol

    PUBLIC
    Aurore::Util

    PRIVATE
    Aurore::BuildOptions
)

set_target_properties(
    aurore_protocol
    PROPERTIES
    OUTPUT_NAME "Aurore.Protocol"
    FOLDER "Aurore"
)
