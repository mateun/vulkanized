#ifndef ENGINE_AUDIO_H
#define ENGINE_AUDIO_H

#include "core/common.h"

/* ---- Opaque audio context (caller never sees miniaudio internals) ---- */

typedef struct AudioEngine AudioEngine;

/* ---- Sound handle (returned by load, used for play/stop) ---- */

typedef struct {
    u32 id;  /* internal index into loaded sounds array */
} SoundHandle;

/* ---- Lifecycle ---- */

/* Initialize the audio engine (creates device, starts audio thread).
 * Returns ENGINE_SUCCESS on success. */
EngineResult audio_init(AudioEngine **out_engine);

/* Shut down the audio engine (stops device, frees all loaded sounds). */
void audio_shutdown(AudioEngine *engine);

/* ---- Loading ---- */

/* Load a sound file from disk (WAV, MP3, FLAC, OGG).
 * The file is decoded in memory for low-latency playback.
 * Returns ENGINE_SUCCESS on success. */
EngineResult audio_load_sound(AudioEngine *engine, const char *file_path,
                              SoundHandle *out_handle);

/* ---- Playback ---- */

/* Play a loaded sound.
 *   loop   — true to loop indefinitely, false for one-shot
 *   volume — 0.0 = silent, 1.0 = full volume (can exceed 1.0 for gain)
 * Each call restarts the sound from the beginning. */
void audio_play_sound(AudioEngine *engine, SoundHandle handle,
                      bool loop, f32 volume);

/* Stop a currently playing sound immediately. */
void audio_stop_sound(AudioEngine *engine, SoundHandle handle);

/* ---- Global controls ---- */

/* Set the master volume (scales all sounds). Default is 1.0. */
void audio_set_master_volume(AudioEngine *engine, f32 volume);

#endif /* ENGINE_AUDIO_H */
