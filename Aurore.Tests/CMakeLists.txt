set(
    aurore_test_sources

    main.cpp

    ByteBufferTests.cpp
    ClientLifecycleTests.cpp
    ConfigurationTests.cpp
    ConfigurationProtocolTests.cpp
    ConfigurationRuntimeTests.cpp
    ConfigurationSequenceTests.cpp
    LoginRequestTests.cpp
    NbtTests.cpp
    NetworkTests.cpp
    NetworkRegistryTests.cpp
    OfflineLoginStabilizationTests.cpp
    PacketCodecTests.cpp
    PacketFrameTests.cpp
    ProtocolConnectionTests.cpp
    ProtocolPipelineTests.cpp
    ProtocolSessionTests.cpp
    RegistryCompatibilityTests.cpp
    RegistrySnapshotTests.cpp
    RegistryTagTests.cpp
    RegistryTests.cpp
    ResourceLocationTests.cpp
    ServerDataFoundationTests.cpp
    SyntheticRegistrySnapshotTests.cpp
    TickScheduleTests.cpp
)

# IOCP backend internals and the current loopback client fixture are
# Windows-specific. Platform-neutral network contract tests remain in
# NetworkTests.cpp and run on every supported build platform.
if(AURORE_NETWORK_BACKEND_IOCP)
    list(
        APPEND
        aurore_test_sources

        WindowsNetworkBackendTests.cpp
        NetworkLoopbackTests.cpp
    )
endif()

add_executable(
    aurore_tests
    ${aurore_test_sources}
)

add_executable(Aurore::Tests ALIAS aurore_tests)

target_include_directories(
    aurore_tests
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/Aurore.Network/src"
    "${PROJECT_SOURCE_DIR}/Aurore.Core/src"
)

target_link_libraries(
    aurore_tests

    PRIVATE
    Aurore::BuildOptions
    Aurore::Core
    Aurore::Network
    Aurore::Protocol
    Aurore::Util
    Aurore::World
    GTest::gtest
    Threads::Threads
)

if(AURORE_NETWORK_BACKEND_IOCP)
    target_link_libraries(
        aurore_tests
        PRIVATE
        ws2_32
        mswsock
    )
endif()

set_target_properties(
    aurore_tests
    PROPERTIES
    OUTPUT_NAME "Aurore.Tests"
    FOLDER "Aurore"
)

include(GoogleTest)

gtest_discover_tests(
    aurore_tests
    DISCOVERY_TIMEOUT 30
)
