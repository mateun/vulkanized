# AI Game Engine

## Project Overview
A small game engine written in C with three main subsystems:
1. **Renderer** — Vulkan-based rendering pipeline
2. **Audio** — Sound effects and background music playback
3. **Gameplay** — GameObject/Component architecture with runtime scripting

The engine builds as a **static library** (`engine.lib`). Games link against it and own
the main loop, calling engine functions directly.

## Architecture Decisions

### Language: C (C11)
- Core engine in C for performance and simplicity
- Runtime scripting layer TBD (Lua is the leading candidate)

### Build System: CMake
- Cross-platform build support
- Handles Vulkan SDK discovery and shader compilation
- Engine is a static library; sample games are separate executables

### Platform: Windows (primary), Linux (future)
- Development on Windows 11
- Vulkan chosen for cross-platform GPU access

### Dependencies (planned)
| Library | Purpose | Notes |
|---------|---------|-------|
| Vulkan SDK | Rendering API | LunarG SDK |
| GLFW | Window/input management | Lightweight, Vulkan-aware |
| cglm | Math (vec/mat operations) | Header-only, C-native |
| stb_image | Texture loading | Header-only, single file |
| miniaudio | Audio playback | Header-only, single file |
| Lua | Runtime scripting | Embeddable, small footprint |

### Project Structure
```
ai_game_engine/
├── CLAUDE.md              # This file — project context
├── CMakeLists.txt         # Root build: engine lib + shader compilation
├── src/                   # Engine source (builds into engine.lib)
│   ├── core/              # Core utilities (memory, logging, containers)
│   │   ├── common.h       # Shared typedefs, macros, error codes
│   │   ├── log.h / log.c  # Logging system
│   │   └── arena.h / arena.c  # Arena allocator
│   ├── platform/          # Window, input, platform abstraction
│   │   ├── window.h / window.c
│   │   └── input.h / input.c
│   ├── renderer/          # Vulkan rendering
│   │   ├── renderer.h / renderer.c      # Public API (begin/end frame, draw_text, upload_vertices)
│   │   ├── renderer_types.h             # Public vertex types (Vertex, TextVertex — no Vulkan dep)
│   │   ├── vk_init.h / vk_init.c        # Instance, device, swapchain
│   │   ├── vk_pipeline.h / vk_pipeline.c # Pipeline, shaders
│   │   ├── vk_buffer.h / vk_buffer.c    # Buffers, memory, texture upload
│   │   ├── vk_types.h                   # Vulkan-specific type wrappers (internal)
│   │   ├── bloom.h / bloom.c            # Bloom post-processing (80s neon glow)
│   │   └── text.h / text.c              # Text rendering (stb_truetype, internal)
│   ├── audio/             # Audio subsystem (planned)
│   │   └── audio.h / audio.c
│   └── gameplay/          # ECS / GameObject system (planned)
│       ├── world.h / world.c
│       ├── gameobject.h / gameobject.c
│       ├── components.h / components.c
│       └── scripting.h / scripting.c
├── sample_games/          # Sample games linking against engine
│   └── shmup/             # Shoot-em-up sample
│       ├── CMakeLists.txt # Builds shmup.exe, links engine, copies assets
│       └── main.c         # Game entry point (owns main loop)
├── shaders/               # GLSL shaders (compiled to SPIR-V)
│   ├── triangle.vert / triangle.frag    # Geometry pipeline
│   ├── text.vert / text.frag           # Text pipeline (alpha-blended)
│   ├── fullscreen.vert                  # Fullscreen triangle (bloom passes)
│   ├── bloom_extract.frag               # Brightness threshold extraction
│   ├── bloom_blur.frag                  # 9-tap separable Gaussian blur
│   └── bloom_composite.frag             # Composite + tonemap + scanlines + aberration
├── assets/                # Textures, models, audio, fonts
│   └── consolas.ttf       # Font for text rendering
└── third_party/           # Vendored header-only libs
    ├── stb/               # stb_truetype.h
    └── miniaudio/
```

### Engine Public API
Games include headers from `src/` and call these functions:

```c
/* Lifecycle */
renderer_create(window, &config, &renderer);   /* RendererConfig: font_path, font_size, clear_color */
renderer_destroy(renderer);

/* Per-frame rendering (game owns the loop) */
renderer_begin_frame(renderer);
renderer_set_camera(renderer, &camera);
renderer_draw_mesh(renderer, mesh, instances, count);
renderer_draw_mesh_textured(renderer, mesh, texture, instances, count);
renderer_draw_text(renderer, "text", x, y, scale, r, g, b);
renderer_end_frame(renderer);

/* Geometry & textures */
renderer_upload_mesh(renderer, vertices, count, &mesh_handle);
renderer_load_texture(renderer, "path.png", &tex_handle);

/* Bloom post-processing (80s arcade neon glow) */
renderer_set_bloom(renderer, enabled, intensity, threshold);
renderer_set_bloom_settings(renderer, &bloom_settings);

/* Utilities */
renderer_get_extent(renderer, &w, &h);
renderer_handle_resize(renderer);
renderer_set_clear_color(renderer, r, g, b, a);
```

## Coding Conventions
- **Naming**: `snake_case` for functions and variables, `PascalCase` for struct types, `UPPER_SNAKE` for macros/constants
- **Prefixes**: Each subsystem prefixes its public API (e.g., `renderer_create()`, `audio_play()`, `world_create()`)
- **Error handling**: Functions return `EngineResult` enum; no exceptions (it's C)
- **Memory**: Arena allocators preferred; minimize malloc/free pairs
- **Headers**: Each `.c` file has a matching `.h` with include guards (`#ifndef ENGINE_MODULE_H`)
- **Vulkan**: All Vulkan handles wrapped in engine structs, never exposed in public headers

## Implementation Roadmap

### Phase 1: Foundation + Triangle on Screen
- [x] Project scaffolding (CLAUDE.md, CMakeLists.txt, directory structure)
- [x] Core utilities (logging, common types, arena allocator)
- [x] Platform layer (GLFW window creation, input polling)
- [x] Vulkan init (instance, device, surface, swapchain)
- [x] Render pass + graphics pipeline (hardcoded triangle)
- [x] Main loop (acquire/present, command buffer recording)
- **Milestone: colored triangle rendered on screen**

### Phase 2: Renderer Essentials
- [x] Vertex buffers (GPU-local via staging)
- [x] Text rendering (stb_truetype, separate alpha-blended pipeline)
- [x] Frame timing display (11px scaled text, delta time measurement)
- [x] Engine/game split (engine as static library, sample_games/shmup)
- [x] Instanced rendering (per-instance position, rotation, scale, color)
- [x] 2D orthographic camera (position, rotation, zoom, half_height)
- [x] Texture loading + sampling (stb_image, PNG/JPG/BMP)
- [x] Depth buffering (D32_SFLOAT)
- [x] Swapchain recreation on resize
- [x] Bloom post-processing (5-pass: HDR scene, brightness extract, Gaussian blur, composite)
- [x] 80s arcade effects (scanlines, chromatic aberration, vignette, Reinhard tonemap)
- **Milestone: textured sprites with neon bloom on screen**

### Phase 3: Audio
- [ ] miniaudio integration
- [ ] Sound effect playback (fire-and-forget)
- [ ] Background music (looping, crossfade)
- [ ] Volume control / mixing
- **Milestone: audio plays alongside rendering**

### Phase 4: Gameplay Framework
- [ ] GameObject + Component data structures
- [ ] World/Scene management
- [ ] Transform component (position, rotation, scale)
- [ ] Mesh renderer component
- [ ] Lua scripting integration
- [ ] Script component (attach Lua scripts to GameObjects)
- **Milestone: Lua scripts driving game objects in real time**

### Phase 5: Polish
- [ ] Resource management (asset loading/caching)
- [x] Basic UI rendering (debug text via stb_truetype)
- [x] Frame timing / delta time display
- [ ] Hot-reload for Lua scripts
- [ ] Simple scene serialization

## Build & Run
```bash
# Configure
cmake -B build -S .

# Build
cmake --build build --config Debug

# Run shmup sample
./build/sample_games/shmup/Debug/shmup.exe
```

## Environment
- Vulkan SDK: `D:\Programs\VulkanSDK` (API 1.4.304)
- CMake: 4.1.2 at `D:\Programs\cmake\bin\cmake.exe`
- Compiler: MSVC 19.38 (VS 2022 Community)
- glslc: in PATH via Vulkan SDK
- Generator: Visual Studio 17 2022 (x64)

## Current Status
**Phase 2 complete — Full 2D renderer with bloom post-processing.**
- Phase 1 complete: triangle on screen, all Vulkan infrastructure working
- Phase 2 complete: textured instanced rendering, camera, depth, resize, bloom
- Engine is a static library (`engine.lib`); games link against it
- Sample game `shmup` demonstrates the full API: textured enemies with neon bloom
- Instanced rendering: per-instance position, rotation, scale, color via vertex attributes
- 2D orthographic camera with zoom and world-space projection (push constants)
- Texture loading: stb_image (PNG/JPG/BMP), descriptor sets, sampler, dummy texture for untextured draws
- Depth buffering: D32_SFLOAT format, cleared each frame
- Swapchain resize: full recreation of swapchain, depth buffer, framebuffers, bloom resources
- Alpha blending on geometry pipeline with fragment discard for alpha < 0.01
- **Bloom post-processing** (5-pass pipeline):
  - Pass 1: Scene → offscreen HDR (R16G16B16A16_SFLOAT)
  - Pass 2: Brightness extract (soft-knee threshold)
  - Pass 3–4: Separable Gaussian blur at half resolution (ping-pong)
  - Pass 5: Composite → swapchain (additive bloom + Reinhard tonemap + scanlines + chromatic aberration + vignette)
  - Scene pipeline duplication: separate VkPipelines for HDR render pass
  - Fullscreen triangle trick: post-processing with no vertex buffer
  - `BloomSettings` struct: intensity, threshold, soft_threshold, scanline_strength, scanline_count, aberration
- Text rendering: stb_truetype font atlas, alpha-blended overlay, works in both bloom and non-bloom paths
- VSync off (IMMEDIATE present mode) for uncapped FPS
- Next: audio (miniaudio), gameplay framework (ECS, Lua scripting)
