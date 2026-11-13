/**
 * @file libretro-miniaudio.h
 * @brief Single-header wrapper around miniaudio for libretro cores.
 *
 * Usage:
 *   In exactly one .c file:
 * @code
 *       #define LIBRETRO_MINIAUDIO_IMPLEMENTATION
 *       #include "libretro-miniaudio.h"
 * @endcode
 *
 *   Elsewhere just:
 * @code
 *       #include "libretro-miniaudio.h"
 * @endcode
 *
 * The implementation block pulls in miniaudio.h with MINIAUDIO_IMPLEMENTATION and MA_NO_DEVICE_IO so libretro can drive the audio callback itself.
 *
 * Compile-time options (define before including this header):
 * - LIBRETRO_MINIAUDIO_IMPLEMENTATION: emit the implementation in this
 *   translation unit. Define it in exactly one .c file.
 * - LIBRETRO_MINIAUDIO_NO_MA_CONFIG: skip the default MA_* miniaudio options
 *   this header would otherwise define.
 * - LIBRETRO_MINIAUDIO_OGG: build stb_vorbis (stb_vorbis.c must be on the
 *   include path) into the implementation for Ogg Vorbis decoding.
 * - LIBRETRO_MINIAUDIO_MAX_SOUNDS: maximum number of simultaneously loaded
 *   sounds (default 64).
 */
#ifndef LIBRETRO_MINIAUDIO_H
#define LIBRETRO_MINIAUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libretro.h"

/* miniaudio configuration. Define other MA_* options (e.g. MA_NO_MP3) before
 * including this header, or define LIBRETRO_MINIAUDIO_NO_MA_CONFIG to skip
 * these defaults entirely. */
#ifndef LIBRETRO_MINIAUDIO_NO_MA_CONFIG
#ifndef MA_NO_RUNTIME_LINKING
#define MA_NO_RUNTIME_LINKING
#endif
#ifndef MA_NO_DEVICE_IO
#define MA_NO_DEVICE_IO
#endif
#ifndef MA_NO_THREADING
#define MA_NO_THREADING
#endif
#ifndef MA_NO_GENERATION
#define MA_NO_GENERATION
#endif
#ifndef MA_NO_ENCODING
#define MA_NO_ENCODING
#endif
#endif

#include "miniaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration passed to libretro_miniaudio_init().
 *
 * Obtain defaults with libretro_miniaudio_config_default() and override fields
 * as needed.
 */
typedef struct libretro_miniaudio_config {
   uint32_t           sample_rate;  /**< Engine output rate in Hz. Default 48000, must be > 0. */
   uint32_t           channels;     /**< Must be 2: libretro takes interleaved stereo int16. */
   double             fps;          /**< Core frame rate; sets frames per run(). Default 60.0, must be > 0. */
   retro_log_printf_t log_cb;       /**< Optional libretro log callback for errors. May be NULL (default). */
} libretro_miniaudio_config;

/**
 * @brief Sound handle; an alias of miniaudio's ma_sound.
 *
 * Allocated by the caller and populated by
 * libretro_miniaudio_sound_load_from_memory() or
 * libretro_miniaudio_sound_load_from_memory_copy(). The address must stay
 * stable while the sound is loaded.
 */
typedef ma_sound libretro_miniaudio_sound;

libretro_miniaudio_config libretro_miniaudio_config_default(void);
bool libretro_miniaudio_init(const libretro_miniaudio_config* cfg);
void libretro_miniaudio_deinit(void);
void libretro_miniaudio_run(retro_audio_sample_batch_t audio_batch_cb);
ma_engine* libretro_miniaudio_engine(void);
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
/** @brief Ensures the implementation is emitted at most once per translation unit. */
#define LIBRETRO_MINIAUDIO_IMPLEMENTATION_GUARD

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
#include <stdio.h>
#include <string.h>

#ifndef LIBRETRO_MINIAUDIO_MAX_SOUNDS
/**
 * @brief Maximum number of sounds that can be loaded at once (default 64).
 *
 * Loads beyond this cap fail. Define before including the implementation to
 * change it.
 */
#define LIBRETRO_MINIAUDIO_MAX_SOUNDS 64
#endif

/** @cond INTERNAL */

/**
 * @internal
 * @brief Tracks every sound loaded through the wrapper so unload can
 *        unregister it under exactly the name it was registered with, and
 *        free any owned copy.
 */
typedef struct libretro_miniaudio_sound_entry {
   ma_sound* sound;     /**< Tracked sound, or NULL if the slot is free. */
   char      name[24];  /**< Unique internal registration name. */
   void*     data;      /**< Owned copy of the encoded data, or NULL. */
} libretro_miniaudio_sound_entry;

/**
 * @internal
 * @brief Global wrapper state. Zeroed when not initialized.
 */
typedef struct libretro_miniaudio_state {
   bool                           initialized;         /**< True between a successful init() and deinit(). */
   libretro_miniaudio_config      cfg;                 /**< Copy of the config passed to init(). */
   ma_engine                      engine;              /**< The miniaudio engine (no device). */
   uint32_t                       max_frames_per_run;  /**< ceil(sample_rate / fps), buffer capacity. */
   double                         frames_per_run;      /**< Exact sample_rate / fps. */
   double                         frames_accum;        /**< Carried fractional remainder. */
   float*                         buf_f32;             /**< Engine output scratch buffer. */
   int16_t*                       buf_s16;             /**< Converted output handed to libretro. */
   uint32_t                       load_counter;        /**< Makes registration names unique. */
   libretro_miniaudio_sound_entry sounds[LIBRETRO_MINIAUDIO_MAX_SOUNDS];  /**< Loaded-sound slots. */
} libretro_miniaudio_state;

/** @internal @brief The single global wrapper state instance. */
static libretro_miniaudio_state g_libretro_miniaudio;

/** @endcond */

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
 * @param cfg Configuration to use; copied, so it need not outlive the call.
 *            NULL uses libretro_miniaudio_config_default(). channels must be
 *            2, and sample_rate and fps must be > 0.
 * @return true on success or if already initialized, false on invalid config
 *         or engine/buffer allocation failure (logged via cfg->log_cb if set).
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

   if (cfg->channels != 2) {
      if (cfg->log_cb) {
         cfg->log_cb(RETRO_LOG_ERROR, "[libretro-miniaudio] Channels must be 2 (interleaved stereo), got %u.\n", (unsigned)cfg->channels);
      }
      return false;
   }

   /* Written as !(fps > 0) so that NaN is rejected too. */
   if (cfg->sample_rate == 0 || !(cfg->fps > 0.0)) {
      if (cfg->log_cb) {
         cfg->log_cb(RETRO_LOG_ERROR, "[libretro-miniaudio] Invalid sample rate or fps.\n");
      }
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

   /* run() emits floor or ceil of the exact rate per call, carrying the
    * remainder. Starting the accumulator at 0.5 makes the running total
    * round(calls * sample_rate / fps). Buffers are sized for the ceiling. */
   g_libretro_miniaudio.frames_per_run = (double)cfg->sample_rate / cfg->fps;
   g_libretro_miniaudio.frames_accum = 0.5;
   g_libretro_miniaudio.max_frames_per_run = (uint32_t)g_libretro_miniaudio.frames_per_run;
   if ((double)g_libretro_miniaudio.max_frames_per_run < g_libretro_miniaudio.frames_per_run) {
      g_libretro_miniaudio.max_frames_per_run++;
   }
   buf_samples = g_libretro_miniaudio.max_frames_per_run * cfg->channels;

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
 * @internal
 * @brief Uninitializes the tracked sound in the given slot and drops its
 *        resource manager registration. The slot's data copy is not freed.
 *
 * @param i Index of an occupied slot in g_libretro_miniaudio.sounds.
 */
static void libretro_miniaudio_sound_release(int i) {
   ma_resource_manager* rm;
   ma_sound_uninit(g_libretro_miniaudio.sounds[i].sound);
   rm = ma_engine_get_resource_manager(&g_libretro_miniaudio.engine);
   if (rm) {
      ma_resource_manager_unregister_data(rm, g_libretro_miniaudio.sounds[i].name);
   }
}

/**
 * @brief Tears down the audio engine and releases its internal buffers.
 *
 * Any sounds still loaded are uninitialized first, so calling
 * libretro_miniaudio_sound_unload() beforehand is optional; unloading them
 * afterwards is a no-op. Safe to call when not initialized. After this returns
 * the engine state is fully reset and libretro_miniaudio_init() may be called
 * again.
 */
void libretro_miniaudio_deinit(void) {
   int i;
   if (g_libretro_miniaudio.initialized) {
      /* Sounds must be uninitialized while the engine, node graph and resource
       * manager they reference are still alive. */
      for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_SOUNDS; ++i) {
         if (g_libretro_miniaudio.sounds[i].sound) {
            libretro_miniaudio_sound_release(i);
         }
      }
      /* Uninitializing the engine also uninitializes its resource manager.
       * Nodes registered from existing memory never own or free that memory,
       * so the tracked data copies are only released once nothing can
       * reference them. */
      ma_engine_uninit(&g_libretro_miniaudio.engine);
      for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_SOUNDS; ++i) {
         free(g_libretro_miniaudio.sounds[i].data);
      }
   }
   free(g_libretro_miniaudio.buf_f32);
   free(g_libretro_miniaudio.buf_s16);
   memset(&g_libretro_miniaudio, 0, sizeof(g_libretro_miniaudio));
}

/**
 * @brief Pumps one frame's worth of audio out to libretro.
 *
 * Reads floor or ceil of sample_rate / fps frames from the engine, carrying
 * the fractional remainder between calls so the total does not drift,
 * converts them from float to signed 16-bit PCM, and submits them via the
 * provided libretro batch callback. Call once per retro_run() invocation.
 * Short or failed engine reads are padded with silence. No-op when not
 * initialized.
 *
 * @param audio_batch_cb The audio batch callback the core received via
 *                       retro_set_audio_sample_batch(). NULL is a no-op.
 */
void libretro_miniaudio_run(retro_audio_sample_batch_t audio_batch_cb) {
   uint32_t frames;
   uint32_t channels;
   ma_uint64 frames_read = 0;
   const int16_t* data;
   size_t remaining;
   size_t accepted;

   if (!g_libretro_miniaudio.initialized || !audio_batch_cb) {
      return;
   }

   g_libretro_miniaudio.frames_accum += g_libretro_miniaudio.frames_per_run;
   frames = (uint32_t)g_libretro_miniaudio.frames_accum;
   if (frames > g_libretro_miniaudio.max_frames_per_run) {
      frames = g_libretro_miniaudio.max_frames_per_run;
   }
   g_libretro_miniaudio.frames_accum -= (double)frames;
   channels = g_libretro_miniaudio.cfg.channels;

   if (frames == 0) {
      return;
   }

   /* Pad short or failed reads with silence so stale samples never go out. */
   if (ma_engine_read_pcm_frames(&g_libretro_miniaudio.engine, g_libretro_miniaudio.buf_f32, frames, &frames_read) != MA_SUCCESS) {
      frames_read = 0;
   }
   if (frames_read < frames) {
      memset(g_libretro_miniaudio.buf_f32 + frames_read * channels, 0,
         (size_t)(frames - frames_read) * channels * sizeof(float));
   }
   ma_pcm_f32_to_s16(g_libretro_miniaudio.buf_s16,
      g_libretro_miniaudio.buf_f32,
      (ma_uint64)frames * channels, ma_dither_mode_none);

   /* The frontend may accept fewer frames than offered; stop if it takes none. */
   data = g_libretro_miniaudio.buf_s16;
   remaining = frames;
   while (remaining > 0) {
      accepted = audio_batch_cb(data, remaining);
      if (accepted == 0 || accepted >= remaining) {
         break;
      }
      data += accepted * channels;
      remaining -= accepted;
   }
}

/**
 * @brief Returns a pointer to the underlying ma_engine.
 *
 * Use this to call miniaudio APIs directly when the wrapper does not expose
 * the functionality you need. The returned pointer is owned by the wrapper;
 * do not uninitialize it manually.
 *
 * @return Pointer to the internal ma_engine. Valid only between
 *         libretro_miniaudio_init() and libretro_miniaudio_deinit().
 */
ma_engine* libretro_miniaudio_engine(void) {
   return &g_libretro_miniaudio.engine;
}

/**
 * @internal
 * @brief Shared implementation of the sound_load_from_memory*() functions.
 *
 * Registers the encoded data with the resource manager, initializes the sound
 * from it, and records the registration so libretro_miniaudio_sound_unload()
 * can release it.
 *
 * @param sound Caller-allocated sound handle to populate.
 * @param data  Encoded audio bytes.
 * @param size  Size of data in bytes.
 * @param name  Optional label used only in log messages. May be NULL.
 * @param copy  When true, the data is duplicated and owned by the wrapper.
 * @return true on success, false on invalid input, no free slot, or failure.
 */
static bool libretro_miniaudio_sound_load_internal(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name, bool copy) {
   ma_resource_manager* rm;
   void*                data_copy;
   const void*          reg_data;
   char*                reg_name;
   size_t               i;
   ma_result            r;
   int                  slot;

   if (!sound || !data || size == 0 || !g_libretro_miniaudio.initialized) {
      return false;
   }

   rm = ma_engine_get_resource_manager(&g_libretro_miniaudio.engine);
   if (!rm) {
      return false;
   }

   slot = -1;
   for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_SOUNDS; ++i) {
      if (!g_libretro_miniaudio.sounds[i].sound) {
         slot = (int)i;
         break;
      }
   }
   if (slot < 0) {
      if (g_libretro_miniaudio.cfg.log_cb) {
         g_libretro_miniaudio.cfg.log_cb(RETRO_LOG_ERROR,
            "[libretro-miniaudio] Too many sounds loaded (max %d).\n", LIBRETRO_MINIAUDIO_MAX_SOUNDS);
      }
      return false;
   }

   data_copy = NULL;
   reg_data  = data;
   if (copy) {
      data_copy = malloc(size);
      if (!data_copy) {
         return false;
      }
      memcpy(data_copy, data, size);
      reg_data = data_copy;
   }

   /* Register under a unique internal name. Sharing a name would make the
    * resource manager reuse the first registration and ignore this buffer,
    * so the caller's name is only used for logging. */
   reg_name = g_libretro_miniaudio.sounds[slot].name;
   sprintf(reg_name, "lrm_%x_%lx", (unsigned)slot, (unsigned long)g_libretro_miniaudio.load_counter++);

   if (ma_resource_manager_register_encoded_data(rm, reg_name, reg_data, size) != MA_SUCCESS) {
      free(data_copy);
      return false;
   }

   r = ma_sound_init_from_file(&g_libretro_miniaudio.engine, reg_name, 0, NULL, NULL, sound);
   if (r != MA_SUCCESS) {
      if (g_libretro_miniaudio.cfg.log_cb) {
         /* Surface miniaudio's error code so callers can distinguish "no decoder"
          * (e.g. an Ogg with a Skeleton stream stb_vorbis can't parse) from I/O
          * failures. */
         g_libretro_miniaudio.cfg.log_cb(RETRO_LOG_ERROR,
            "[libretro-miniaudio] ma_sound_init_from_file('%s') failed: %d.\n", (name && name[0]) ? name : reg_name, (int)r);
      }
      ma_resource_manager_unregister_data(rm, reg_name);
      free(data_copy);
      return false;
   }

   g_libretro_miniaudio.sounds[slot].sound = sound;
   g_libretro_miniaudio.sounds[slot].data  = data_copy;
   return true;
}

/**
 * @brief Loads an encoded audio asset (wav/flac/mp3/etc.) from a memory buffer.
 *
 * The buffer is NOT copied; it must remain valid until the sound is unloaded
 * or libretro_miniaudio_deinit() is called. Useful for sounds embedded in the
 * core via xxd / incbin / build-time blobs. Use
 * libretro_miniaudio_sound_load_from_memory_copy() for transient buffers.
 * Every load gets its own resource manager registration. At most
 * LIBRETRO_MINIAUDIO_MAX_SOUNDS sounds can be loaded at once.
 *
 * @param sound Caller-allocated sound handle to populate. Must not be NULL.
 * @param data  Pointer to the encoded audio bytes. Must not be NULL.
 * @param size  Size of the encoded buffer in bytes. Must be > 0.
 * @param name  Optional label (e.g. "sfx/jump.wav") used only in log
 *              messages. May be NULL. Names need not be unique.
 * @return true on success, false if not initialized, on invalid input, when
 *         the sound cap is reached, or on decode failure.
 */
bool libretro_miniaudio_sound_load_from_memory(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name) {
   return libretro_miniaudio_sound_load_internal(sound, data, size, name, false);
}

/**
 * @brief Releases a sound previously loaded by libretro_miniaudio_sound_load*().
 *
 * The sound is uninitialized first, then its resource manager registration is
 * dropped and any data copy owned by the wrapper is freed.
 *
 * Only sounds currently tracked by the wrapper are touched, so this is a safe
 * no-op for NULL, a sound that was never loaded (or failed to load), a sound
 * that was already unloaded, or any sound after libretro_miniaudio_deinit().
 *
 * @param sound Sound handle to release.
 */
void libretro_miniaudio_sound_unload(libretro_miniaudio_sound* sound) {
   int i;

   if (!sound) {
      return;
   }

   for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_SOUNDS; ++i) {
      if (g_libretro_miniaudio.sounds[i].sound == sound) {
         libretro_miniaudio_sound_release(i);
         free(g_libretro_miniaudio.sounds[i].data);
         memset(&g_libretro_miniaudio.sounds[i], 0, sizeof(g_libretro_miniaudio.sounds[i]));
         break;
      }
   }
}

/**
 * @brief Starts (or resumes) playback of the given sound.
 *
 * @param sound Sound handle to play. NULL is tolerated and treated as a no-op,
 *              as is any call when not initialized.
 */
void libretro_miniaudio_sound_play(libretro_miniaudio_sound* sound) {
   if (sound && g_libretro_miniaudio.initialized) {
      ma_sound_start(sound);
   }
}

/**
 * @brief Stops playback of the given sound without releasing it.
 *
 * @param sound Sound handle to stop. NULL is tolerated and treated as a no-op,
 *              as is any call when not initialized.
 */
void libretro_miniaudio_sound_stop(libretro_miniaudio_sound* sound) {
   if (sound && g_libretro_miniaudio.initialized) {
      ma_sound_stop(sound);
   }
}

/**
 * @brief Enables or disables looping for the given sound.
 *
 * @param sound Sound handle to modify. NULL is tolerated and treated as a no-op,
 *              as is any call when not initialized.
 * @param loop  true to loop on completion, false to play once.
 */
void libretro_miniaudio_sound_set_looping(libretro_miniaudio_sound* sound, bool loop) {
   if (sound && g_libretro_miniaudio.initialized) {
      ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
   }
}

/**
 * @brief Sets the linear playback volume for the given sound.
 *
 * @param sound  Sound handle to modify. NULL is tolerated and treated as a no-op,
 *               as is any call when not initialized.
 * @param volume Linear gain (1.0 = unity, 0.0 = silence, > 1.0 amplifies).
 */
void libretro_miniaudio_sound_set_volume(libretro_miniaudio_sound* sound, float volume) {
   if (sound && g_libretro_miniaudio.initialized) {
      ma_sound_set_volume(sound, volume);
   }
}

/**
 * @brief Reports whether the given sound is currently playing.
 *
 * @param sound Sound handle to query. NULL is tolerated.
 * @return true if the sound is non-NULL and currently playing, false otherwise
 *         (including when not initialized).
 */
bool libretro_miniaudio_sound_is_playing(const libretro_miniaudio_sound* sound) {
   if (!sound || !g_libretro_miniaudio.initialized) {
      return false;
   }
   return ma_sound_is_playing(sound) == MA_TRUE;
}

/**
 * @brief Loads an encoded audio asset from a memory buffer, copying it first.
 *
 * Same as libretro_miniaudio_sound_load_from_memory(), except the data is
 * duplicated and owned by the wrapper, so the caller's buffer may be freed as
 * soon as this returns. The copy is freed by libretro_miniaudio_sound_unload()
 * or libretro_miniaudio_deinit().
 *
 * @param sound Caller-allocated sound handle to populate. Must not be NULL.
 * @param data  Pointer to the encoded audio bytes. Must not be NULL.
 * @param size  Size of the encoded buffer in bytes. Must be > 0.
 * @param name  Optional label used only in log messages. May be NULL.
 * @return true on success, false if not initialized, on invalid input, when
 *         the sound cap is reached, or on allocation or decode failure.
 */
bool libretro_miniaudio_sound_load_from_memory_copy(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name) {
   return libretro_miniaudio_sound_load_internal(sound, data, size, name, true);
}

/**
 * @brief Sets the engine's master volume, applied to all sounds.
 *
 * No-op when not initialized.
 *
 * @param volume Linear gain (1.0 = unity, 0.0 = silence, > 1.0 amplifies).
 */
void libretro_miniaudio_set_master_volume(float volume) {
   if (!g_libretro_miniaudio.initialized) {
      return;
   }
   ma_engine_set_volume(&g_libretro_miniaudio.engine, volume);
}

/**
 * @brief Returns the engine's master volume.
 *
 * @return Linear master gain, or 1.0 when not initialized.
 */
float libretro_miniaudio_get_master_volume(void) {
   if (!g_libretro_miniaudio.initialized) {
      return 1.0f;
   }
   return ma_engine_get_volume(&g_libretro_miniaudio.engine);
}

/**
 * @brief Seeks the given sound to a PCM frame.
 *
 * The seek is applied on the next engine read, i.e. the next
 * libretro_miniaudio_run().
 *
 * @param sound       Sound handle to seek. NULL is tolerated and treated as a
 *                    no-op, as is any call when not initialized.
 * @param frame_index Target position in PCM frames at the engine's sample rate.
 */
void libretro_miniaudio_sound_seek(libretro_miniaudio_sound* sound, uint64_t frame_index) {
   if (sound && g_libretro_miniaudio.initialized) {
      ma_sound_seek_to_pcm_frame(sound, (ma_uint64)frame_index);
   }
}

/**
 * @brief Returns the current playback position of the given sound.
 *
 * A pending seek is reported as the new position.
 *
 * @param sound Sound handle to query. NULL is tolerated.
 * @return Position in PCM frames at the engine's sample rate, or 0 if sound
 *         is NULL, not initialized, or the cursor cannot be queried.
 */
uint64_t libretro_miniaudio_sound_get_position(const libretro_miniaudio_sound* sound) {
   ma_uint64 cursor = 0;
   if (sound && g_libretro_miniaudio.initialized) {
      if (ma_sound_get_cursor_in_pcm_frames(sound, &cursor) != MA_SUCCESS) {
         return 0;
      }
   }
   return (uint64_t)cursor;
}

/**
 * @brief Returns the total length of the given sound.
 *
 * @param sound Sound handle to query. NULL is tolerated.
 * @return Length in PCM frames at the engine's sample rate, or 0 if sound is
 *         NULL, not initialized, or the length cannot be determined.
 */
uint64_t libretro_miniaudio_sound_get_length(const libretro_miniaudio_sound* sound) {
   ma_uint64 length = 0;
   if (sound && g_libretro_miniaudio.initialized) {
      if (ma_sound_get_length_in_pcm_frames(sound, &length) != MA_SUCCESS) {
         return 0;
      }
   }
   return (uint64_t)length;
}

/**
 * @brief Returns the version of the bundled miniaudio.
 *
 * @return Static string MA_VERSION_STRING, e.g. "0.11.25". Never NULL.
 */
const char* libretro_miniaudio_version(void) {
   return MA_VERSION_STRING;
}

#endif /* LIBRETRO_MINIAUDIO_IMPLEMENTATION_GUARD */
#endif /* LIBRETRO_MINIAUDIO_IMPLEMENTATION */
