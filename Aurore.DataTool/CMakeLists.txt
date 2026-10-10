add_executable(
    aurore_data_tool
    main.cpp
)

add_executable(Aurore::DataTool ALIAS aurore_data_tool)

target_link_libraries(
    aurore_data_tool

    PRIVATE
    Aurore::BuildOptions
    Aurore::Data
)

set_target_properties(
    aurore_data_tool
    PROPERTIES
    OUTPUT_NAME "Aurore.DataTool"
    FOLDER "Aurore"
)
