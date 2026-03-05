add_executable(anim_demo main.c)

target_link_libraries(anim_demo PRIVATE engine)

# Link math library on Unix
if(NOT MSVC)
    target_link_libraries(anim_demo PRIVATE m)
endif()

# Ensure shaders are compiled before building anim_demo
add_dependencies(anim_demo shaders)

# Copy compiled shaders next to anim_demo executable
add_custom_command(TARGET anim_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:anim_demo>/shaders
    COMMENT "Copying shaders to anim_demo output"
)

# Copy engine assets (fonts, models, etc.) next to anim_demo executable
add_custom_command(TARGET anim_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:anim_demo>/assets
    COMMENT "Copying assets to anim_demo output"
)
