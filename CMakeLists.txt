cmake_minimum_required(VERSION 3.20)
project(ai_game_engine C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW — fetch if not installed
include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# cglm — fetch (static library)
set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    cglm
    GIT_REPOSITORY https://github.com/recp/cglm.git
    GIT_TAG        v0.9.4
)
FetchContent_MakeAvailable(cglm)

# --------------------------------------------------------------------------
# Engine static library
# --------------------------------------------------------------------------

set(ENGINE_SOURCES
    src/core/log.c
    src/core/arena.c
    src/platform/window.c
    src/platform/input.c
    src/renderer/renderer.c
    src/renderer/vk_init.c
    src/renderer/vk_pipeline.c
    src/renderer/vk_buffer.c
    src/renderer/text.c
    src/renderer/bloom.c
    src/renderer/primitives.c
    src/renderer/stb_impl.c
    src/gameplay/collision.c
    src/gameplay/particles.c
    src/audio/audio.c
)

add_library(engine STATIC ${ENGINE_SOURCES})

target_include_directories(engine PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(engine PUBLIC
    Vulkan::Vulkan
    glfw
    cglm
)

if(APPLE)
    target_link_libraries(engine PUBLIC
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework QuartzCore"
    )
endif()

# Enable validation-friendly warnings
if(MSVC)
    target_compile_options(engine PRIVATE /W4)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
    if(APPLE)
        # miniaudio uses deprecated CoreAudio APIs
        target_compile_options(engine PRIVATE -Wno-deprecated-declarations)
    endif()
endif()

# Debug defines
target_compile_definitions(engine PUBLIC
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

# --------------------------------------------------------------------------
# Shader compilation (GLSL -> SPIR-V)
# --------------------------------------------------------------------------

find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

if(NOT GLSLC)
    # Try the PATH as fallback
    find_program(GLSLC glslc)
endif()

if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or add glslc to PATH.")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_BINARY_DIR}/shaders)

file(GLOB SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/*.vert
    ${SHADER_SOURCE_DIR}/*.frag
    ${SHADER_SOURCE_DIR}/*.comp
)

file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} ${SHADER} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader: ${SHADER_NAME}"
    )

    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})

# --------------------------------------------------------------------------
# Sample games
# --------------------------------------------------------------------------

add_subdirectory(sample_games/shmup)
add_subdirectory(sample_games/cube_demo)
