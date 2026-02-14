# AI Game Engine

A lightweight 2D game engine written in C11 with a Vulkan renderer. The engine builds as a static library — games link against it and own the main loop.

## Features

- **Vulkan renderer** — instanced mesh drawing, 2D orthographic camera, depth buffering, texture loading (PNG/JPG/BMP via stb_image), text rendering (stb_truetype)
- **Multi-mesh system** — upload multiple meshes to a shared GPU vertex buffer, draw each with per-instance transforms and color tinting
- **Camera** — 2D orthographic with position, rotation, zoom, and configurable world-space projection
- **Text overlay** — screen-space text with alpha blending, independent of the camera
- **Input** — GLFW key callbacks with press/release detection
- **Swapchain resize** — automatic recreation of swapchain, depth buffer, and framebuffers on window resize

## Requirements

- **Vulkan SDK** (LunarG) with `glslc` on PATH
- **CMake** 3.20+
- **C11 compiler** (MSVC, GCC, or Clang)

GLFW and cglm are fetched automatically via CMake FetchContent.

## Build

```bash
cmake -B build -S .
cmake --build build --config Debug
```

## Run

```bash
./build/sample_games/shmup/Debug/shmup.exe
```

The shmup sample renders enemies (red quads) and a player (white triangle) with a score display and frametime counter. Press ESC to quit.

## Project Structure

```
ai_game_engine/
├── CMakeLists.txt          # Engine lib + shader compilation + sample games
├── src/                    # Engine source (builds into engine.lib)
│   ├── core/               # Logging, common types, arena allocator
│   ├── platform/           # GLFW window + input abstraction
│   └── renderer/           # Vulkan rendering (public API + internals)
├── shaders/                # GLSL 4.5 shaders (compiled to SPIR-V at build time)
├── sample_games/shmup/     # Shoot-em-up sample game
├── assets/                 # Fonts, textures
└── third_party/stb/        # stb_truetype, stb_image (header-only)
```

## Engine API

Games include headers from `src/` and call engine functions directly:

```c
/* Create window and renderer */
window_create(&config, &window);
renderer_create(window, &render_config, &renderer);

/* Upload geometry once */
renderer_upload_mesh(renderer, vertices, count, &mesh_handle);
renderer_load_texture(renderer, "assets/sprite.png", &tex_handle);

/* Per-frame rendering */
renderer_begin_frame(renderer);
renderer_set_camera(renderer, &camera);
renderer_draw_mesh(renderer, mesh, instances, instance_count);
renderer_draw_mesh_textured(renderer, mesh, texture, instances, count);
renderer_draw_text(renderer, "Hello", x, y, scale, r, g, b);
renderer_end_frame(renderer);

/* Cleanup */
renderer_destroy(renderer);
window_destroy(window);
```

## License

Unlicensed — personal project.
