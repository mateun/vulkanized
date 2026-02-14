#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#include "audio/audio.h"
#include "core/log.h"

#include <stdlib.h>  /* malloc, free */
#include <string.h>  /* memset */

/* --------------------------------------------------------------------------
 * Internal limits
 * ------------------------------------------------------------------------ */

#define AUDIO_MAX_SOUNDS  64   /* max loaded sound assets */
#define AUDIO_MAX_VOICES  16   /* max simultaneous plays per sound */

/* --------------------------------------------------------------------------
 * Voice: one playback instance of a sound
 * ------------------------------------------------------------------------ */

typedef struct {
    ma_sound sound;     /* miniaudio sound (cloned from source data) */
    bool     active;    /* true if this voice is currently initialized */
} Voice;

/* --------------------------------------------------------------------------
 * Internal sound slot (one per loaded file)
 * Each slot holds the source data node and a pool of voice instances
 * that can play simultaneously.
 * ------------------------------------------------------------------------ */

typedef struct {
    ma_sound   source;                   /* base sound (used as data source for copies) */
    Voice      voices[AUDIO_MAX_VOICES]; /* fire-and-forget voice pool */
    bool       loaded;                   /* true if this slot has a valid sound */
    char       path[256];                /* file path (for logging) */
} SoundSlot;

/* --------------------------------------------------------------------------
 * AudioEngine (opaque struct, hidden from public header)
 * ------------------------------------------------------------------------ */

struct AudioEngine {
    ma_engine   engine;                    /* miniaudio high-level engine */
    SoundSlot   sounds[AUDIO_MAX_SOUNDS];  /* loaded sound pool */
    u32         sound_count;               /* number of sounds loaded so far */
};

/* --------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

EngineResult audio_init(AudioEngine **out_engine)
{
    AudioEngine *ae = (AudioEngine *)malloc(sizeof(AudioEngine));
    if (!ae) {
        LOG_ERROR("audio_init: out of memory");
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }
    memset(ae, 0, sizeof(*ae));

    ma_engine_config config = ma_engine_config_init();
    /* Default device, default sample rate, default channel count */

    ma_result result = ma_engine_init(&config, &ae->engine);
    if (result != MA_SUCCESS) {
        LOG_ERROR("audio_init: ma_engine_init failed (error %d)", result);
        free(ae);
        return ENGINE_ERROR_GENERIC;
    }

    LOG_INFO("Audio engine initialized (miniaudio)");
    *out_engine = ae;
    return ENGINE_SUCCESS;
}

void audio_shutdown(AudioEngine *engine)
{
    if (!engine) return;

    /* Uninit all voices and source sounds */
    for (u32 i = 0; i < engine->sound_count; i++) {
        SoundSlot *slot = &engine->sounds[i];
        if (!slot->loaded) continue;

        for (i32 v = 0; v < AUDIO_MAX_VOICES; v++) {
            if (slot->voices[v].active) {
                ma_sound_uninit(&slot->voices[v].sound);
                slot->voices[v].active = false;
            }
        }
        ma_sound_uninit(&slot->source);
        slot->loaded = false;
    }

    ma_engine_uninit(&engine->engine);
    LOG_INFO("Audio engine shut down");
    free(engine);
}

/* --------------------------------------------------------------------------
 * Loading
 * ------------------------------------------------------------------------ */

EngineResult audio_load_sound(AudioEngine *engine, const char *file_path,
                              SoundHandle *out_handle)
{
    if (engine->sound_count >= AUDIO_MAX_SOUNDS) {
        LOG_ERROR("audio_load_sound: max sounds reached (%d)", AUDIO_MAX_SOUNDS);
        return ENGINE_ERROR_GENERIC;
    }

    u32 idx = engine->sound_count;
    SoundSlot *slot = &engine->sounds[idx];

    /* Load from file — MA_SOUND_FLAG_DECODE forces full decode into memory
     * for low-latency playback (fire-and-forget sounds).
     * MA_SOUND_FLAG_NO_PITCH disables pitch processing for performance.
     * We don't start this source directly — it's a template for voices. */
    ma_result result = ma_sound_init_from_file(
        &engine->engine, file_path,
        MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH,
        NULL, NULL, &slot->source);

    if (result != MA_SUCCESS) {
        LOG_ERROR("audio_load_sound: failed to load '%s' (error %d)", file_path, result);
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    slot->loaded = true;
    /* Safe copy — ensure null-terminated */
    {
        size_t len = strlen(file_path);
        if (len >= sizeof(slot->path)) len = sizeof(slot->path) - 1;
        memcpy(slot->path, file_path, len);
        slot->path[len] = '\0';
    }
    engine->sound_count++;

    out_handle->id = idx;
    LOG_INFO("Loaded sound [%u]: %s", idx, file_path);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Playback — voice pool for overlapping sounds
 * ------------------------------------------------------------------------ */

/* Find an idle voice (not playing), or steal the oldest one */
static Voice *find_voice(SoundSlot *slot, ma_engine *engine)
{
    /* First pass: find an uninitialized slot */
    for (i32 v = 0; v < AUDIO_MAX_VOICES; v++) {
        if (!slot->voices[v].active) {
            /* Initialize a new voice by copying from the source */
            ma_result r = ma_sound_init_copy(
                engine, &slot->source,
                MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH,
                NULL, &slot->voices[v].sound);
            if (r == MA_SUCCESS) {
                slot->voices[v].active = true;
                return &slot->voices[v];
            }
            /* If copy failed, try next slot */
            continue;
        }
    }

    /* Second pass: find a voice that's finished playing */
    for (i32 v = 0; v < AUDIO_MAX_VOICES; v++) {
        if (slot->voices[v].active && ma_sound_at_end(&slot->voices[v].sound)) {
            ma_sound_seek_to_pcm_frame(&slot->voices[v].sound, 0);
            return &slot->voices[v];
        }
    }

    /* Third pass: steal the first non-looping voice (oldest by index) */
    for (i32 v = 0; v < AUDIO_MAX_VOICES; v++) {
        if (slot->voices[v].active && !ma_sound_is_looping(&slot->voices[v].sound)) {
            ma_sound_stop(&slot->voices[v].sound);
            ma_sound_seek_to_pcm_frame(&slot->voices[v].sound, 0);
            return &slot->voices[v];
        }
    }

    /* All voices busy with looping sounds — steal first anyway */
    if (slot->voices[0].active) {
        ma_sound_stop(&slot->voices[0].sound);
        ma_sound_seek_to_pcm_frame(&slot->voices[0].sound, 0);
        return &slot->voices[0];
    }

    return NULL;  /* should never happen */
}

void audio_play_sound(AudioEngine *engine, SoundHandle handle,
                      bool loop, f32 volume)
{
    if (handle.id >= engine->sound_count) {
        LOG_WARN("audio_play_sound: invalid handle %u", handle.id);
        return;
    }

    SoundSlot *slot = &engine->sounds[handle.id];
    if (!slot->loaded) {
        LOG_WARN("audio_play_sound: sound %u not loaded", handle.id);
        return;
    }

    Voice *voice = find_voice(slot, &engine->engine);
    if (!voice) {
        LOG_WARN("audio_play_sound: no free voice for sound %u", handle.id);
        return;
    }

    ma_sound_set_looping(&voice->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&voice->sound, volume);
    ma_sound_seek_to_pcm_frame(&voice->sound, 0);
    ma_sound_start(&voice->sound);
}

void audio_stop_sound(AudioEngine *engine, SoundHandle handle)
{
    if (handle.id >= engine->sound_count) {
        LOG_WARN("audio_stop_sound: invalid handle %u", handle.id);
        return;
    }

    SoundSlot *slot = &engine->sounds[handle.id];
    if (!slot->loaded) return;

    /* Stop ALL voices for this sound */
    for (i32 v = 0; v < AUDIO_MAX_VOICES; v++) {
        if (slot->voices[v].active) {
            ma_sound_stop(&slot->voices[v].sound);
        }
    }
}

/* --------------------------------------------------------------------------
 * Global controls
 * ------------------------------------------------------------------------ */

void audio_set_master_volume(AudioEngine *engine, f32 volume)
{
    ma_engine_set_volume(&engine->engine, volume);
}
