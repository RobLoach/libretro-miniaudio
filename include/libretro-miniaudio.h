/*
 * libretro-miniaudio.h - single-header wrapper around miniaudio for libretro cores.
 *
 * Usage:
 *   In exactly one .c file:
 *       #define LIBRETRO_MINIAUDIO_IMPLEMENTATION
 *       #include "libretro-miniaudio.h"
 *
 *   Elsewhere just:
 *       #include "libretro-miniaudio.h"
 *
 * The implementation block pulls in miniaudio.h with MINIAUDIO_IMPLEMENTATION and MA_NO_DEVICE_IO so libretro can drive the audio callback itself.
 */
#ifndef LIBRETRO_MINIAUDIO_H
#define LIBRETRO_MINIAUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libretro.h"
#include "miniaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libretro_miniaudio_config {
   uint32_t           sample_rate;  /* default 48000 */
   uint32_t           channels;     /* default 2     */
   double             fps;          /* default 60.0  */
   retro_log_printf_t log_cb;       /* optional      */
} libretro_miniaudio_config;

typedef ma_sound libretro_miniaudio_sound;

libretro_miniaudio_config libretro_miniaudio_config_default(void);
bool libretro_miniaudio_init(const libretro_miniaudio_config* cfg);
void libretro_miniaudio_uninit(void);
void libretro_miniaudio_run(retro_audio_sample_batch_t audio_batch_cb);
ma_engine* libretro_miniaudio_engine(void);
bool libretro_miniaudio_sound_load(libretro_miniaudio_sound* sound, const char* path);
bool libretro_miniaudio_sound_load_from_memory(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name);
void libretro_miniaudio_sound_unload(libretro_miniaudio_sound* sound);
void libretro_miniaudio_sound_play(libretro_miniaudio_sound* sound);
void libretro_miniaudio_sound_stop(libretro_miniaudio_sound* sound);
void libretro_miniaudio_sound_set_looping(libretro_miniaudio_sound* sound, bool loop);
void libretro_miniaudio_sound_set_volume(libretro_miniaudio_sound* sound, float volume);
bool libretro_miniaudio_sound_is_playing(const libretro_miniaudio_sound* sound);
bool libretro_miniaudio_sound_load_from_memory_copy(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name);
void libretro_miniaudio_set_master_volume(float volume);
float libretro_miniaudio_get_master_volume(void);
void libretro_miniaudio_sound_seek(libretro_miniaudio_sound* sound, uint64_t frame_index);
uint64_t libretro_miniaudio_sound_get_position(const libretro_miniaudio_sound* sound);
uint64_t libretro_miniaudio_sound_get_length(const libretro_miniaudio_sound* sound);
const char* libretro_miniaudio_version(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_MINIAUDIO_H */

#ifdef LIBRETRO_MINIAUDIO_IMPLEMENTATION
#ifndef LIBRETRO_MINIAUDIO_IMPLEMENTATION_GUARD
#define LIBRETRO_MINIAUDIO_IMPLEMENTATION_GUARD

#define MA_NO_RUNTIME_LINKING
#define MA_NO_DEVICE_IO

#ifdef LIBRETRO_MINIAUDIO_OGG
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#endif /* LIBRETRO_MINIAUDIO_OGG */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#ifdef LIBRETRO_MINIAUDIO_OGG
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#endif /* LIBRETRO_MINIAUDIO_OGG */

#include <stdlib.h>
#include <string.h>

#define LIBRETRO_MINIAUDIO_MAX_COPIES 64

typedef struct libretro_miniaudio_copy_entry {
   ma_sound* sound;
   char      name[24];
   void*     data;
} libretro_miniaudio_copy_entry;

typedef struct libretro_miniaudio_state {
   bool                           initialized;
   libretro_miniaudio_config      cfg;
   ma_engine                      engine;
   uint32_t                       frames_per_run;
   float*                         buf_f32;
   int16_t*                       buf_s16;
   libretro_miniaudio_copy_entry  copies[LIBRETRO_MINIAUDIO_MAX_COPIES];
} libretro_miniaudio_state;

static libretro_miniaudio_state g_libretro_miniaudio;

/**
 * @brief Returns a libretro_miniaudio_config populated with default values.
 *
 * Defaults are 48000 Hz, 2 channels, 60.0 fps, and a NULL log callback.
 *
 * @return A config struct initialized with default values.
 */
libretro_miniaudio_config libretro_miniaudio_config_default(void) {
   libretro_miniaudio_config cfg;
   cfg.sample_rate = 48000;
   cfg.channels = 2;
   cfg.fps = 60.0;
   cfg.log_cb = NULL;
   return cfg;
}

/**
 * @brief Initializes the global audio engine.
 *
 * Safe to call multiple times; subsequent calls are a no-op while the engine
 * is already initialized and return true. The engine is created with
 * MA_NO_DEVICE_IO, so audio is driven by libretro_miniaudio_run() rather than
 * a backend device.
 *
 * @param cfg Provide the configuration for the miniaudio. NULL will load the defaults.
 * @return true on success, false on invalid input or engine/buffer allocation failure.
 */
bool libretro_miniaudio_init(const libretro_miniaudio_config* cfg) {
   ma_engine_config ec;
   uint32_t         buf_samples;
   libretro_miniaudio_config default_cfg;

   if (g_libretro_miniaudio.initialized) {
      return true;
   }

   if (cfg == NULL) {
      default_cfg = libretro_miniaudio_config_default();
      cfg = &default_cfg;
   }

   if (cfg->channels == 0 || cfg->sample_rate == 0 || cfg->fps <= 0.0) {
      return false;
   }

   g_libretro_miniaudio.cfg = *cfg;

   ec             = ma_engine_config_init();
   ec.noDevice    = MA_TRUE;
   ec.channels    = cfg->channels;
   ec.sampleRate  = cfg->sample_rate;

   if (ma_engine_init(&ec, &g_libretro_miniaudio.engine) != MA_SUCCESS) {
      if (cfg->log_cb) {
         cfg->log_cb(RETRO_LOG_ERROR, "[libretro-miniaudio] Failed to initialize audio engine.\n");
      }
      return false;
   }

   g_libretro_miniaudio.frames_per_run = (uint32_t)(((double)cfg->sample_rate / cfg->fps) + 0.5);
   buf_samples = g_libretro_miniaudio.frames_per_run * cfg->channels;

   g_libretro_miniaudio.buf_f32 = (float*)calloc(buf_samples, sizeof(float));
   g_libretro_miniaudio.buf_s16 = (int16_t*)calloc(buf_samples, sizeof(int16_t));
   if (!g_libretro_miniaudio.buf_f32 || !g_libretro_miniaudio.buf_s16) {
      if (cfg->log_cb) {
         cfg->log_cb(RETRO_LOG_ERROR, "[libretro-miniaudio] Failed to allocate audio buffers.\n");
      }
      ma_engine_uninit(&g_libretro_miniaudio.engine);
      free(g_libretro_miniaudio.buf_f32);
      free(g_libretro_miniaudio.buf_s16);
      memset(&g_libretro_miniaudio, 0, sizeof(g_libretro_miniaudio));
      return false;
   }

   g_libretro_miniaudio.initialized = true;
   return true;
}

/**
 * @brief Tears down the audio engine and releases its internal buffers.
 *
 * Safe to call when not initialized. After this returns the engine state is
 * fully reset and libretro_miniaudio_init() may be called again.
 */
static void libretro_miniaudio_copy_free(ma_sound* sound) {
   int i;
   ma_resource_manager* rm;
   for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_COPIES; ++i) {
      if (g_libretro_miniaudio.copies[i].sound == sound) {
         rm = ma_engine_get_resource_manager(&g_libretro_miniaudio.engine);
         if (rm) {
            ma_resource_manager_unregister_data(rm, g_libretro_miniaudio.copies[i].name);
         }
         free(g_libretro_miniaudio.copies[i].data);
         memset(&g_libretro_miniaudio.copies[i], 0, sizeof(g_libretro_miniaudio.copies[i]));
         break;
      }
   }
}

void libretro_miniaudio_uninit(void) {
   int i;
   if (g_libretro_miniaudio.initialized) {
      for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_COPIES; ++i) {
         if (g_libretro_miniaudio.copies[i].data) {
            free(g_libretro_miniaudio.copies[i].data);
         }
      }
      ma_engine_uninit(&g_libretro_miniaudio.engine);
   }
   free(g_libretro_miniaudio.buf_f32);
   free(g_libretro_miniaudio.buf_s16);
   memset(&g_libretro_miniaudio, 0, sizeof(g_libretro_miniaudio));
}

/**
 * @brief Pumps one frame's worth of audio out to libretro.
 *
 * Reads sample_rate / fps frames from the engine, converts them from float to
 * signed 16-bit PCM, and submits them via the provided libretro batch
 * callback. Call once per retro_run() invocation.
 *
 * @param audio_batch_cb The audio batch callback the core received via
 *                       retro_set_audio_sample_batch(). Must be non-NULL.
 */
void libretro_miniaudio_run(retro_audio_sample_batch_t audio_batch_cb) {
   uint32_t frames;
   uint32_t channels;

   if (!g_libretro_miniaudio.initialized || !audio_batch_cb) {
      return;
   }

   frames = g_libretro_miniaudio.frames_per_run;
   channels = g_libretro_miniaudio.cfg.channels;

   ma_engine_read_pcm_frames(&g_libretro_miniaudio.engine, g_libretro_miniaudio.buf_f32, frames, NULL);
   ma_pcm_f32_to_s16(g_libretro_miniaudio.buf_s16,
      g_libretro_miniaudio.buf_f32,
      (ma_uint64)frames * channels, ma_dither_mode_none);
   audio_batch_cb(g_libretro_miniaudio.buf_s16, frames);
}

/**
 * @brief Returns a pointer to the underlying ma_engine.
 *
 * Use this to call miniaudio APIs directly when the wrapper does not expose
 * the functionality you need. The returned pointer is owned by the wrapper;
 * do not uninitialize it manually.
 *
 * @return Pointer to the internal ma_engine. Valid only between
 *         libretro_miniaudio_init() and libretro_miniaudio_uninit().
 */
ma_engine* libretro_miniaudio_engine(void) {
   return &g_libretro_miniaudio.engine;
}

/**
 * @brief Loads an encoded audio asset from a file path.
 *
 * @param sound Caller-allocated sound handle to populate.
 * @param path  Filesystem path to a supported audio file (wav/flac/mp3/etc.).
 * @return true on success, false if the engine is not initialized or loading fails.
 */
bool libretro_miniaudio_sound_load(libretro_miniaudio_sound* sound, const char* path) {
   if (!sound || !path || !g_libretro_miniaudio.initialized) {
      return false;
   }
   return ma_sound_init_from_file(&g_libretro_miniaudio.engine, path, 0, NULL, NULL, sound) == MA_SUCCESS;
}

/**
 * @brief Loads an encoded audio asset (wav/flac/mp3/etc.) from a memory buffer.
 *
 * The buffer is NOT copied; it must remain valid for the lifetime of the
 * engine. Useful for sounds embedded in the core via xxd / incbin /
 * build-time blobs. Loading the same buffer twice refcounts on the same
 * registration inside the resource manager.
 *
 * @param sound Caller-allocated sound handle to populate.
 * @param data  Pointer to the encoded audio bytes. Must outlive the engine.
 * @param size  Size of the encoded buffer in bytes. Must be > 0.
 * @param name  Optional virtual path to register the buffer under (e.g.
 *              "sfx/jump.wav"). Pass NULL to auto-derive a stable name from
 *              the data pointer. Reusing the same name with the same data
 *              refcounts on the existing registration.
 * @return true on success, false on invalid input or load failure.
 */
bool libretro_miniaudio_sound_load_from_memory(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name) {
   static const char    hex[] = "0123456789abcdef";
   ma_resource_manager* rm;
   uintptr_t            ptr;
   char                 auto_name[24];
   const char*          reg_name;
   size_t               i;
   ma_result            r;

   if (!sound || !data || size == 0 || !g_libretro_miniaudio.initialized) {
      return false;
   }

   rm = ma_engine_get_resource_manager(&g_libretro_miniaudio.engine);
   if (!rm) {
      return false;
   }

   if (name && name[0]) {
      reg_name = name;
   } else {
      /* Derive a stable virtual name from the data pointer (no stdio needed).
       * Loading the same buffer twice will refcount on the same registration. */
      ptr          = (uintptr_t)data;
      auto_name[0] = 'l'; auto_name[1] = 'r'; auto_name[2] = 'm'; auto_name[3] = '_';
      for (i = 0; i < 16; ++i) {
         auto_name[4 + 15 - i] = hex[ptr & 0xf];
         ptr >>= 4;
      }
      auto_name[20] = '\0';
      reg_name      = auto_name;
   }

   if (ma_resource_manager_register_encoded_data(rm, reg_name, data, size) != MA_SUCCESS) {
      return false;
   }

   r = ma_sound_init_from_file(&g_libretro_miniaudio.engine, reg_name, 0, NULL, NULL, sound);
   if (r != MA_SUCCESS) {
      if (g_libretro_miniaudio.cfg.log_cb) {
         /* Surface miniaudio's error code so callers can distinguish "no decoder"
          * (e.g. an Ogg with a Skeleton stream stb_vorbis can't parse) from I/O
          * failures. */
         g_libretro_miniaudio.cfg.log_cb(RETRO_LOG_ERROR,
            "[libretro-miniaudio] ma_sound_init_from_file('%s') failed: %d.\n", reg_name, (int)r);
      }
      ma_resource_manager_unregister_data(rm, reg_name);
      return false;
   }

   return true;
}

/**
 * @brief Releases a sound previously loaded by libretro_miniaudio_sound_load*().
 *
 * @param sound Sound handle to release. NULL is tolerated and treated as a no-op.
 */
void libretro_miniaudio_sound_unload(libretro_miniaudio_sound* sound) {
   if (sound) {
      libretro_miniaudio_copy_free(sound);
      ma_sound_uninit(sound);
   }
}

/**
 * @brief Starts (or resumes) playback of the given sound.
 *
 * @param sound Sound handle to play. NULL is tolerated and treated as a no-op.
 */
void libretro_miniaudio_sound_play(libretro_miniaudio_sound* sound) {
   if (sound) {
      ma_sound_start(sound);
   }
}

/**
 * @brief Stops playback of the given sound without releasing it.
 *
 * @param sound Sound handle to stop. NULL is tolerated and treated as a no-op.
 */
void libretro_miniaudio_sound_stop(libretro_miniaudio_sound* sound) {
   if (sound) {
      ma_sound_stop(sound);
   }
}

/**
 * @brief Enables or disables looping for the given sound.
 *
 * @param sound Sound handle to modify. NULL is tolerated and treated as a no-op.
 * @param loop  true to loop on completion, false to play once.
 */
void libretro_miniaudio_sound_set_looping(libretro_miniaudio_sound* sound, bool loop) {
   if (sound) {
      ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
   }
}

/**
 * @brief Sets the linear playback volume for the given sound.
 *
 * @param sound  Sound handle to modify. NULL is tolerated and treated as a no-op.
 * @param volume Linear gain (1.0 = unity, 0.0 = silence, > 1.0 amplifies).
 */
void libretro_miniaudio_sound_set_volume(libretro_miniaudio_sound* sound, float volume) {
   if (sound) {
      ma_sound_set_volume(sound, volume);
   }
}

/**
 * @brief Reports whether the given sound is currently playing.
 *
 * @param sound Sound handle to query. NULL is tolerated.
 * @return true if the sound is non-NULL and currently playing, false otherwise.
 */
bool libretro_miniaudio_sound_is_playing(const libretro_miniaudio_sound* sound) {
   if (!sound) {
      return false;
   }
   return ma_sound_is_playing(sound) == MA_TRUE;
}

bool libretro_miniaudio_sound_load_from_memory_copy(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name) {
   static const char    hex[] = "0123456789abcdef";
   ma_resource_manager* rm;
   void*                data_copy;
   uintptr_t            ptr;
   char                 auto_name[24];
   const char*          reg_name;
   size_t               i;
   ma_result            r;
   int                  slot;

   if (!sound || !data || size == 0 || !g_libretro_miniaudio.initialized) {
      return false;
   }

   slot = -1;
   for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_COPIES; ++i) {
      if (!g_libretro_miniaudio.copies[i].sound) {
         slot = (int)i;
         break;
      }
   }
   if (slot < 0) {
      return false;
   }

   data_copy = malloc(size);
   if (!data_copy) {
      return false;
   }
   memcpy(data_copy, data, size);

   rm = ma_engine_get_resource_manager(&g_libretro_miniaudio.engine);
   if (!rm) {
      free(data_copy);
      return false;
   }

   if (name && name[0]) {
      reg_name = name;
   } else {
      ptr          = (uintptr_t)data_copy;
      auto_name[0] = 'l'; auto_name[1] = 'r'; auto_name[2] = 'm'; auto_name[3] = '_';
      for (i = 0; i < 16; ++i) {
         auto_name[4 + 15 - i] = hex[ptr & 0xf];
         ptr >>= 4;
      }
      auto_name[20] = '\0';
      reg_name      = auto_name;
   }

   if (ma_resource_manager_register_encoded_data(rm, reg_name, data_copy, size) != MA_SUCCESS) {
      free(data_copy);
      return false;
   }

   r = ma_sound_init_from_file(&g_libretro_miniaudio.engine, reg_name, 0, NULL, NULL, sound);
   if (r != MA_SUCCESS) {
      ma_resource_manager_unregister_data(rm, reg_name);
      free(data_copy);
      return false;
   }

   g_libretro_miniaudio.copies[slot].sound = sound;
   strncpy(g_libretro_miniaudio.copies[slot].name, reg_name, sizeof(g_libretro_miniaudio.copies[slot].name) - 1);
   g_libretro_miniaudio.copies[slot].name[sizeof(g_libretro_miniaudio.copies[slot].name) - 1] = '\0';
   g_libretro_miniaudio.copies[slot].data  = data_copy;
   return true;
}

void libretro_miniaudio_set_master_volume(float volume) {
   if (!g_libretro_miniaudio.initialized) {
      return;
   }
   ma_engine_set_volume(&g_libretro_miniaudio.engine, volume);
}

float libretro_miniaudio_get_master_volume(void) {
   if (!g_libretro_miniaudio.initialized) {
      return 1.0f;
   }
   return ma_engine_get_volume(&g_libretro_miniaudio.engine);
}

void libretro_miniaudio_sound_seek(libretro_miniaudio_sound* sound, uint64_t frame_index) {
   if (sound) {
      ma_sound_seek_to_pcm_frame(sound, (ma_uint64)frame_index);
   }
}

uint64_t libretro_miniaudio_sound_get_position(const libretro_miniaudio_sound* sound) {
   ma_uint64 cursor = 0;
   if (sound) {
      ma_sound_get_cursor_in_pcm_frames(sound, &cursor);
   }
   return (uint64_t)cursor;
}

uint64_t libretro_miniaudio_sound_get_length(const libretro_miniaudio_sound* sound) {
   ma_uint64 length = 0;
   if (sound) {
      ma_sound_get_length_in_pcm_frames(sound, &length);
   }
   return (uint64_t)length;
}

const char* libretro_miniaudio_version(void) {
   return MA_VERSION_STRING;
}

#endif /* LIBRETRO_MINIAUDIO_IMPLEMENTATION_GUARD */
#endif /* LIBRETRO_MINIAUDIO_IMPLEMENTATION */
