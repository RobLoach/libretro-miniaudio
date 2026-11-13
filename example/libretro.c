#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

#define LIBRETRO_MINIAUDIO_OGG
#define LIBRETRO_MINIAUDIO_IMPLEMENTATION
#include "libretro-miniaudio.h"

static uint32_t* frame_buf;
static struct retro_log_callback logging;

static void fallback_log(enum retro_log_level level, const char* fmt, ...) {
  (void)level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

static retro_log_printf_t log_cb = fallback_log;

void retro_init(void) {
  frame_buf = calloc(320 * 240, sizeof(uint32_t));
}

void retro_deinit(void) {
  free(frame_buf);
  frame_buf = NULL;
}

unsigned retro_api_version(void) {
  return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
  log_cb(RETRO_LOG_INFO, "Plugging device %u into port %u.\n", device, port);
}

void retro_get_system_info(struct retro_system_info* info) {
  memset(info, 0, sizeof(*info));
  info->library_name = "libretro-miniaudio";
#ifdef GIT_VERSION
  info->library_version = "0.0.1" GIT_VERSION;
#else
  info->library_version = "0.0.1";
#endif
  info->need_fullpath = false;
  info->valid_extensions = "wav|flac|mp3|ogg";
}

static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;

#define CORE_FPS 60.0
#define CORE_SAMPLE_RATE 48000

void retro_get_system_av_info(struct retro_system_av_info* info) {
  float aspect = 4.0f / 3.0f;

  info->timing = (struct retro_system_timing){
      .fps = CORE_FPS,
      .sample_rate = CORE_SAMPLE_RATE,
  };

  info->geometry = (struct retro_game_geometry){
      .base_width = 320,
      .base_height = 240,
      .max_width = 320,
      .max_height = 240,
      .aspect_ratio = aspect,
  };
}

void retro_set_environment(retro_environment_t cb) {
  environ_cb = cb;

  bool no_content = false;
  cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

  if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging) && logging.log) {
    log_cb = logging.log;
  }
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
  (void)cb;  // Audio is sent in batches via audio_batch_cb.
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
  input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
  (void)cb;  // The core has no input.
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
  video_cb = cb;
}

static unsigned x_coord;
static unsigned y_coord;

static libretro_miniaudio_sound g_sound;
static bool g_sound_loaded;

void retro_reset(void) {
  x_coord = 0;
  y_coord = 0;
  if (g_sound_loaded) {
    libretro_miniaudio_sound_seek(&g_sound, 0);
    libretro_miniaudio_sound_play(&g_sound);
  }
}

static void render_checkered(void) {
  uint32_t* buf = frame_buf;
  unsigned stride = 320;

  uint32_t color_r = 0xff << 16;
  uint32_t color_g = 0xff << 8;

  uint32_t* line = buf;
  for (unsigned y = 0; y < 240; y++, line += stride) {
    unsigned index_y = ((y - y_coord) >> 4) & 1;
    for (unsigned x = 0; x < 320; x++) {
      unsigned index_x = ((x - x_coord) >> 4) & 1;
      line[x] = (index_y ^ index_x) ? color_r : color_g;
    }
  }

  if (video_cb) {
    video_cb(buf, 320, 240, stride << 2);
  }
}

void retro_run(void) {
  if (input_poll_cb) {
    input_poll_cb();
  }

  // Scroll the checkerboard one pixel per frame.
  x_coord++;
  y_coord++;
  render_checkered();

  if (audio_batch_cb) {
    libretro_miniaudio_run(audio_batch_cb);
  }
}

bool retro_load_game(const struct retro_game_info* info) {
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
    log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
    return false;
  }

  if (!info || !info->data || info->size == 0) {
    log_cb(RETRO_LOG_ERROR, "No content provided; launch with an audio file as content.\n");
    return false;
  }

  libretro_miniaudio_config audio_cfg = libretro_miniaudio_config_default();
  audio_cfg.sample_rate = CORE_SAMPLE_RATE;
  audio_cfg.fps = CORE_FPS;
  audio_cfg.log_cb = log_cb;
  if (!libretro_miniaudio_init(&audio_cfg)) {
    return false;
  }

  if (!libretro_miniaudio_sound_load_from_memory_copy(&g_sound, info->data, info->size, info->path)) {
    log_cb(RETRO_LOG_ERROR, "Failed to initialize sound from content data.\n");
    libretro_miniaudio_deinit();
    return false;
  }

  libretro_miniaudio_sound_set_looping(&g_sound, true);
  libretro_miniaudio_sound_play(&g_sound);
  g_sound_loaded = true;

  return true;
}

void retro_unload_game(void) {
  if (g_sound_loaded) {
    libretro_miniaudio_sound_unload(&g_sound);
    g_sound_loaded = false;
  }
  libretro_miniaudio_deinit();
}

unsigned retro_get_region(void) {
  return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
  (void)type;
  (void)info;
  (void)num;
  return false;  // No special content types.
}

// Save state: the sound's playback position in frames, then a playing flag.
#define SAVE_STATE_SIZE (sizeof(uint64_t) + 1)

size_t retro_serialize_size(void) {
  return SAVE_STATE_SIZE;
}

bool retro_serialize(void* data, size_t size) {
  if (!g_sound_loaded || !data || size < SAVE_STATE_SIZE) {
    return false;
  }

  uint8_t* out = data;
  uint64_t position = libretro_miniaudio_sound_get_position(&g_sound);
  memcpy(out, &position, sizeof(position));
  out[sizeof(position)] = libretro_miniaudio_sound_is_playing(&g_sound) ? 1 : 0;
  return true;
}

bool retro_unserialize(const void* data, size_t size) {
  if (!g_sound_loaded || !data || size < SAVE_STATE_SIZE) {
    return false;
  }

  const uint8_t* in = data;
  uint64_t position;
  memcpy(&position, in, sizeof(position));
  libretro_miniaudio_sound_seek(&g_sound, position);
  if (in[sizeof(position)]) {
    libretro_miniaudio_sound_play(&g_sound);
  } else {
    libretro_miniaudio_sound_stop(&g_sound);
  }
  return true;
}

void* retro_get_memory_data(unsigned id) {
  (void)id;
  return NULL;
}

size_t retro_get_memory_size(unsigned id) {
  (void)id;
  return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char* code) {
  (void)index;
  (void)enabled;
  (void)code;
}
