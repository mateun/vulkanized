add_executable(shmup main.c)

target_link_libraries(shmup PRIVATE engine)

# Ensure shaders are compiled before building shmup
add_dependencies(shmup shaders)

# Copy compiled shaders next to shmup executable
add_custom_command(TARGET shmup POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:shmup>/shaders
    COMMENT "Copying shaders to shmup output"
)

# Copy engine assets (fonts, etc.) next to shmup executable
add_custom_command(TARGET shmup POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:shmup>/assets
    COMMENT "Copying assets to shmup output"
)
