# AI Game Engine

A lightweight 2D game engine written in C11 with a Vulkan renderer. Runs on Windows (native Vulkan) and macOS (MoltenVK). The engine builds as a static library — games link against it and own the main loop.

![Shmup sample game](assets/shmup_screenshot.png)

## Features

- **Vulkan renderer** — instanced mesh drawing, 2D orthographic camera, depth buffering, texture loading (PNG/JPG/BMP via stb_image), per-texture filter modes (smooth/pixelart), sprite sheet support, text rendering (stb_truetype)
- **Bloom post-processing** — 5-pass pipeline with HDR offscreen rendering, brightness extraction, Gaussian blur, and composite for an 80s arcade neon glow
- **80s arcade effects** — scanlines, chromatic aberration, vignette, and Reinhard tonemapping, all configurable at runtime via `BloomSettings`
- **Audio** — miniaudio backend with fire-and-forget sound playback, voice pooling (16 overlapping per sound), loop and volume controls, master volume mixing
- **Collision detection** — circle-circle brute-force (squared distance, no sqrt), single-vs-array, array-vs-array with pair output
- **Particle system** — circular burst emitter, velocity/spin/lifetime simulation, linear color fade + quadratic scale shrink, swap-remove dead particles, HDR color boost for bloom glow
- **Sprite sheets** — per-instance UV offset/scale for tile selection from atlas textures; `uv_scale={0,0}` defaults to full texture (backwards compatible)
- **Texture filtering** — per-texture filter mode selection: `TEXTURE_FILTER_SMOOTH` (bilinear) for general use, `TEXTURE_FILTER_PIXELART` (nearest-neighbor) for crisp pixel art
- **Multi-mesh system** — upload multiple meshes to a shared GPU vertex buffer, draw each with per-instance transforms and color tinting
- **Camera** — 2D orthographic with position, rotation, zoom, and configurable world-space projection
- **Text overlay** — screen-space text with alpha blending, independent of the camera
- **Input** — GLFW key and mouse button callbacks with press/down/release detection
- **Swapchain resize** — automatic recreation of swapchain, depth buffer, framebuffers, and bloom resources on window resize

## Requirements

- **Vulkan SDK** (LunarG) with `glslc` on PATH
- **CMake** 3.20+
- **C11 compiler** (MSVC, GCC, or Clang)
- **macOS**: LunarG Vulkan SDK for macOS (provides MoltenVK)

GLFW and cglm are fetched automatically via CMake FetchContent.

## Build & Run

### Windows
```bash
cmake -B build -S .
cmake --build build --config Debug
./build/sample_games/shmup/Debug/shmup.exe
```

### macOS
```bash
source ~/VulkanSDK/<version>/setup-env.sh
cmake -B build -S .
cmake --build build
./build/sample_games/shmup/shmup
```

The shmup sample is a playable shoot-em-up with WASD movement, mouse-click shooting, pixel art sprite sheet textures, bullet-enemy collisions with HDR particle explosions and sound effects, score tracking, selective bloom (HDR bullets/particles glow, normal enemies don't), and 80s neon bloom. Press ESC to quit.

## Project Structure

```
ai_game_engine/
├── CMakeLists.txt          # Engine lib + shader compilation + sample games
├── src/                    # Engine source (builds into engine.lib)
│   ├── core/               # Logging, common types, arena allocator
│   ├── platform/           # GLFW window + input abstraction (keyboard + mouse)
│   ├── renderer/           # Vulkan rendering (public API + internals + bloom)
│   ├── audio/              # Audio playback (miniaudio, voice pooling)
│   └── gameplay/           # Collision detection, particle system
├── shaders/                # GLSL 4.5 shaders (compiled to SPIR-V at build time)
│   ├── triangle.*          # Geometry pipeline
│   ├── text.*              # Text pipeline (alpha-blended)
│   ├── fullscreen.vert     # Fullscreen triangle (bloom passes)
│   └── bloom_*.frag        # Extract, blur, composite (bloom post-processing)
├── sample_games/shmup/     # Shoot-em-up sample game (playable!)
├── assets/                 # Fonts, textures, sprite sheets, sound effects
└── third_party/            # stb, miniaudio (header-only)
```

## Engine API

Games include headers from `src/` and call engine functions directly:

```c
/* Create window and renderer */
window_create(&config, &window);
renderer_create(window, &render_config, &renderer);

/* Upload geometry once */
renderer_upload_mesh(renderer, vertices, count, &mesh_handle);
renderer_load_texture(renderer, "assets/spritesheet.png", TEXTURE_FILTER_PIXELART, &tex_handle);

/* Sprite sheet: select tile from a 4×4 atlas per instance */
instance.uv_offset[0] = col / 4.0f;  /* tile column */
instance.uv_offset[1] = row / 4.0f;  /* tile row */
instance.uv_scale[0]  = 1.0f / 4.0f; /* tile width in UV space */
instance.uv_scale[1]  = 1.0f / 4.0f; /* tile height in UV space */
/* uv_scale={0,0} (default) = use full texture, no sprite sheet */

/* Enable bloom (80s arcade neon glow) */
renderer_set_bloom(renderer, true, 0.8f, 0.6f);

/* Per-frame rendering */
renderer_begin_frame(renderer);
renderer_set_camera(renderer, &camera);
renderer_draw_mesh(renderer, mesh, instances, instance_count);
renderer_draw_mesh_textured(renderer, mesh, texture, instances, count);
renderer_draw_text(renderer, "Hello", x, y, scale, r, g, b);
renderer_end_frame(renderer);

/* Audio (fire-and-forget with overlapping voice pool) */
audio_init(&audio);
audio_load_sound(audio, "assets/shoot.wav", &snd);
audio_play_sound(audio, snd, false, 0.5f);   /* one-shot, 50% volume */
audio_play_sound(audio, bgm, true, 0.3f);    /* looping background music */
audio_set_master_volume(audio, 1.0f);

/* Collision detection */
collision_circle_circle(ax, ay, ar, bx, by, br);
collision_instances_vs_instances(bullets, n_bul, bul_r, enemies, n_en, en_r, pairs, max);

/* Particle explosions */
particles_emit(&emitter, particles, count, capacity);
count = particles_update(particles, count, dt);
particles_to_instances(particles, count, instances, max);

/* Cleanup */
audio_shutdown(audio);
renderer_destroy(renderer);
window_destroy(window);
```

## License

Unlicensed — personal project.
