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
static retro_log_printf_t log_cb;

static void fallback_log(enum retro_log_level level, const char* fmt, ...) {
  (void)level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

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
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

void retro_get_system_av_info(struct retro_system_av_info* info) {
  float aspect = 4.0f / 3.0f;
  float sampling_rate = 48000.0f;

  info->timing = (struct retro_system_timing){
      .fps = 30.0,
      .sample_rate = sampling_rate,
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

  if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) {
    log_cb = logging.log;
  } else {
    log_cb = fallback_log;
  }
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
  audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
  input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
  input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
  video_cb = cb;
}

static unsigned x_coord;
static unsigned y_coord;

void retro_reset(void) {
  x_coord = 0;
  y_coord = 0;
}

static void update_input(void) {
  input_poll_cb();
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

  video_cb(buf, 320, 240, stride << 2);
}

static void check_variables(void) {
}

static libretro_miniaudio_sound g_sound;

void retro_run(void) {
  update_input();
  render_checkered();

  libretro_miniaudio_run(audio_batch_cb);

  bool updated = false;
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
    check_variables();
  }
}

bool retro_load_game(const struct retro_game_info* info) {
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
    log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
    return false;
  }

  check_variables();

  if (!info || !info->data || info->size == 0) {
    log_cb(RETRO_LOG_ERROR, "No content provided; launch with an audio file as content.\n");
    return false;
  }

  libretro_miniaudio_config audio_cfg = libretro_miniaudio_config_default();
  audio_cfg.log_cb = log_cb;
  if (!libretro_miniaudio_init(&audio_cfg)) {
    return false;
  }

  if (!libretro_miniaudio_sound_load_from_memory(&g_sound, info->data, info->size, info->path)) {
    log_cb(RETRO_LOG_ERROR, "Failed to initialize sound from content data.\n");
    libretro_miniaudio_uninit();
    return false;
  }

  libretro_miniaudio_sound_set_looping(&g_sound, true);
  libretro_miniaudio_sound_play(&g_sound);

  return true;
}

void retro_unload_game(void) {
  libretro_miniaudio_sound_unload(&g_sound);
  libretro_miniaudio_uninit();
}

unsigned retro_get_region(void) {
  return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
  return retro_load_game(info);
}

size_t retro_serialize_size(void) {
  return 0;
}

bool retro_serialize(void* data_, size_t size) {
  return true;
}

bool retro_unserialize(const void* data_, size_t size) {
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
