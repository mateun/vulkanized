add_executable(cube_demo main.c)

target_link_libraries(cube_demo PRIVATE engine)

# Link math library on Unix
if(NOT MSVC)
    target_link_libraries(cube_demo PRIVATE m)
endif()

# Ensure shaders are compiled before building cube_demo
add_dependencies(cube_demo shaders)

# Copy compiled shaders next to cube_demo executable
add_custom_command(TARGET cube_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:cube_demo>/shaders
    COMMENT "Copying shaders to cube_demo output"
)

# Copy engine assets (fonts, etc.) next to cube_demo executable
add_custom_command(TARGET cube_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:cube_demo>/assets
    COMMENT "Copying assets to cube_demo output"
)
