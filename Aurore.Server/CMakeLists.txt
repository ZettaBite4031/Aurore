add_executable(
    aurore_server
    main.cpp
)

add_executable(Aurore::Server ALIAS aurore_server)

target_link_libraries(
    aurore_server
    PRIVATE
    Aurore::BuildOptions
    Aurore::Core
)

set_target_properties(
    aurore_server
    PROPERTIES
    OUTPUT_NAME "Aurore.Server"
    FOLDER "Aurore"
)
