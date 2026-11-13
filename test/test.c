/*
 * Automated tests for libretro-miniaudio.h.
 *
 * Usage: libretro_miniaudio_test [path/to/amen.wav]
 *
 * Exits non-zero if any check fails.
 */
#define LIBRETRO_MINIAUDIO_IMPLEMENTATION
#include "libretro-miniaudio.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond) do { \
   g_checks++; \
   if (!(cond)) { \
      g_failures++; \
      printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
   } \
} while (0)

/* Test assets. */
static void*          g_amen;
static size_t         g_amen_size;
static unsigned char* g_tone;
static size_t         g_tone_size;

/* Batch callback statistics. */
static size_t g_frames;    /* total frames accepted */
static size_t g_calls;     /* callback invocations */
static size_t g_nonzero;   /* non-zero samples seen in accepted frames */
static size_t g_min;       /* smallest frame count offered */
static size_t g_max;       /* largest frame count offered */

/* Log callback: counts messages, only prints them when not expected. */
static int g_log_count;
static int g_log_quiet;

static void reset_stats(void) {
   g_frames = g_calls = g_nonzero = 0;
   g_min = (size_t)-1;
   g_max = 0;
}

static void scan(const int16_t* data, size_t frames) {
   size_t i;
   for (i = 0; i < frames * 2; ++i) {
      if (data[i] != 0) {
         g_nonzero++;
      }
   }
}

/* Accepts everything. */
static size_t full_cb(const int16_t* data, size_t frames) {
   g_calls++;
   if (frames < g_min) {
      g_min = frames;
   }
   if (frames > g_max) {
      g_max = frames;
   }
   scan(data, frames);
   g_frames += frames;
   return frames;
}

/* Accepts at most 400 frames per call. */
static size_t half_cb(const int16_t* data, size_t frames) {
   size_t n = frames < 400 ? frames : 400;
   g_calls++;
   scan(data, n);
   g_frames += n;
   return n;
}

/* Accepts nothing. */
static size_t zero_cb(const int16_t* data, size_t frames) {
   (void)data;
   (void)frames;
   g_calls++;
   return 0;
}

static void log_cb(enum retro_log_level level, const char* fmt, ...) {
   va_list ap;
   (void)level;
   g_log_count++;
   if (!g_log_quiet) {
      va_start(ap, fmt);
      vfprintf(stderr, fmt, ap);
      va_end(ap);
   }
}

static libretro_miniaudio_config make_config(void) {
   libretro_miniaudio_config cfg = libretro_miniaudio_config_default();
   cfg.log_cb = log_cb;
   return cfg;
}

static void run_frames(int n) {
   int i;
   for (i = 0; i < n; ++i) {
      libretro_miniaudio_run(full_cb);
   }
}

/* Number of sounds tracked by the wrapper. */
static int tracked_count(void) {
   int i, n = 0;
   for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_SOUNDS; ++i) {
      if (g_libretro_miniaudio.sounds[i].sound) {
         n++;
      }
   }
   return n;
}

/* True when no encoded data is registered with the resource manager. */
static bool no_registrations(void) {
   return ma_engine_get_resource_manager(libretro_miniaudio_engine())->pRootDataBufferNode == NULL;
}

static void* load_file(const char* path, size_t* out_size) {
   FILE* f;
   long  size;
   void* buf;

   f = fopen(path, "rb");
   if (!f) {
      return NULL;
   }
   if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
      fclose(f);
      return NULL;
   }
   buf = malloc((size_t)size);
   if (buf && fread(buf, 1, (size_t)size, f) != (size_t)size) {
      free(buf);
      buf = NULL;
   }
   fclose(f);
   *out_size = (size_t)size;
   return buf;
}

static void put16(unsigned char* p, uint16_t v) {
   p[0] = (unsigned char)v;
   p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char* p, uint32_t v) {
   p[0] = (unsigned char)v;
   p[1] = (unsigned char)(v >> 8);
   p[2] = (unsigned char)(v >> 16);
   p[3] = (unsigned char)(v >> 24);
}

/* 22050 Hz mono s16 WAV holding 'frames' frames of a square wave. */
static unsigned char* make_wav(uint32_t frames, size_t* out_size) {
   size_t         data_bytes = (size_t)frames * 2;
   size_t         size = 44 + data_bytes;
   size_t         i;
   unsigned char* w = (unsigned char*)malloc(size);

   if (!w) {
      return NULL;
   }
   memcpy(w, "RIFF", 4);
   put32(w + 4, (uint32_t)(size - 8));
   memcpy(w + 8, "WAVEfmt ", 8);
   put32(w + 16, 16);         /* fmt chunk size */
   put16(w + 20, 1);          /* PCM */
   put16(w + 22, 1);          /* mono */
   put32(w + 24, 22050);      /* sample rate */
   put32(w + 28, 22050 * 2);  /* byte rate */
   put16(w + 32, 2);          /* block align */
   put16(w + 34, 16);         /* bits per sample */
   memcpy(w + 36, "data", 4);
   put32(w + 40, (uint32_t)data_bytes);
   for (i = 0; i < frames; ++i) {
      put16(w + 44 + i * 2, ((i / 50) & 1) ? 8000 : (uint16_t)-8000);
   }
   *out_size = size;
   return w;
}

/* --- frames: fractional frame accounting -------------------------------- */

static void check_rate(uint32_t sample_rate, double fps, int runs) {
   libretro_miniaudio_config cfg = make_config();
   double expect;

   cfg.sample_rate = sample_rate;
   cfg.fps = fps;
   CHECK(libretro_miniaudio_init(&cfg));
   reset_stats();
   run_frames(runs);
   libretro_miniaudio_deinit();

   /* The running total never drifts from round(runs * rate / fps), and each
    * call emits floor or ceil of the exact per-frame count. */
   expect = floor((double)runs * sample_rate / fps + 0.5);
   CHECK(fabs((double)g_frames - expect) <= 1.0);
   CHECK(g_max - g_min <= 1);
}

static void test_frames(void) {
   check_rate(48000, 60.0, 6000);
   CHECK(g_min == 800 && g_max == 800);
   check_rate(48000, 59.94, 6000);
   check_rate(44100, 60.0, 6000);
   check_rate(44100, 59.94, 20000);
   check_rate(32000, 60.0988, 6000);
}

/* --- config: invalid configurations are rejected ------------------------ */

static void check_reject(uint32_t sample_rate, uint32_t channels, double fps) {
   libretro_miniaudio_config cfg = make_config();
   int logs = g_log_count;
   bool ok;

   cfg.sample_rate = sample_rate;
   cfg.channels = channels;
   cfg.fps = fps;
   g_log_quiet = 1;
   ok = libretro_miniaudio_init(&cfg);
   g_log_quiet = 0;
   CHECK(!ok);
   CHECK(g_log_count > logs);
   if (ok) {
      libretro_miniaudio_deinit();
   }
}

static void test_config(void) {
   check_reject(48000, 1, 60.0);
   check_reject(48000, 6, 60.0);
   check_reject(48000, 2, 0.0);
   check_reject(48000, 2, -1.0);
   check_reject(48000, 2, NAN);
   check_reject(0, 2, 60.0);
}

/* --- load/unload stress ------------------------------------------------- */

static void test_load_unload_stress(void) {
   static libretro_miniaudio_sound many[LIBRETRO_MINIAUDIO_MAX_SOUNDS + 1];
   libretro_miniaudio_config cfg = make_config();
   libretro_miniaudio_sound a, b, c, d;
   const char* long_a = "sfx/this/is/a/very/long/virtual/path/name/amen_break_A.wav";
   const char* long_b = "sfx/this/is/a/very/long/virtual/path/name/amen_break_B.wav";
   int it, i, loaded, leaked;
   bool ok;

   CHECK(libretro_miniaudio_init(&cfg));

   /* Long and NULL names through both loaders, unloaded in varying order. */
   loaded = 1;
   leaked = 0;
   for (it = 0; it < 50; ++it) {
      ok = libretro_miniaudio_sound_load_from_memory(&a, g_amen, g_amen_size, long_a);
      ok = libretro_miniaudio_sound_load_from_memory_copy(&b, g_amen, g_amen_size, long_b) && ok;
      ok = libretro_miniaudio_sound_load_from_memory(&c, g_amen, g_amen_size, NULL) && ok;
      ok = libretro_miniaudio_sound_load_from_memory_copy(&d, g_amen, g_amen_size, NULL) && ok;
      if (!ok) {
         loaded = 0;
         break;
      }
      libretro_miniaudio_sound_play(&a);
      libretro_miniaudio_sound_play(&b);
      libretro_miniaudio_sound_play(&c);
      libretro_miniaudio_sound_play(&d);
      run_frames(3);
      if (it & 1) {
         libretro_miniaudio_sound_unload(&a);
         libretro_miniaudio_sound_unload(&b);
         libretro_miniaudio_sound_unload(&c);
         libretro_miniaudio_sound_unload(&d);
      }
      else {
         libretro_miniaudio_sound_unload(&d);
         libretro_miniaudio_sound_unload(&c);
         libretro_miniaudio_sound_unload(&b);
         libretro_miniaudio_sound_unload(&a);
      }
      if (!no_registrations() || tracked_count() != 0) {
         leaked = 1;
         break;
      }
   }
   CHECK(loaded);
   CHECK(!leaked);

   /* Every slot is usable, and one more is refused. */
   loaded = 0;
   for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_SOUNDS; ++i) {
      loaded += libretro_miniaudio_sound_load_from_memory_copy(&many[i], g_amen, g_amen_size, NULL);
   }
   CHECK(loaded == LIBRETRO_MINIAUDIO_MAX_SOUNDS);
   g_log_quiet = 1;
   CHECK(!libretro_miniaudio_sound_load_from_memory(&many[LIBRETRO_MINIAUDIO_MAX_SOUNDS], g_amen, g_amen_size, NULL));
   g_log_quiet = 0;
   for (i = 0; i < LIBRETRO_MINIAUDIO_MAX_SOUNDS; ++i) {
      libretro_miniaudio_sound_unload(&many[i]);
   }
   CHECK(tracked_count() == 0);
   CHECK(no_registrations());

   libretro_miniaudio_deinit();
}

/* --- shared names: different buffers loaded under the same name --------- */

/* Loads d1 then d2 as "same.wav", one with each loader, then unloads the copy
 * and checks that the other sound still plays its own data. */
static void check_shared_name(const void* d1, size_t s1, uint64_t l1,
                              const void* d2, size_t s2, uint64_t l2, bool first_copy) {
   libretro_miniaudio_sound x, y;
   libretro_miniaudio_sound* copy_snd = first_copy ? &x : &y;
   libretro_miniaudio_sound* keep_snd = first_copy ? &y : &x;
   uint64_t keep_len = first_copy ? l2 : l1;
   bool ok1, ok2;

   ok1 = first_copy ? libretro_miniaudio_sound_load_from_memory_copy(&x, d1, s1, "same.wav")
                    : libretro_miniaudio_sound_load_from_memory(&x, d1, s1, "same.wav");
   ok2 = first_copy ? libretro_miniaudio_sound_load_from_memory(&y, d2, s2, "same.wav")
                    : libretro_miniaudio_sound_load_from_memory_copy(&y, d2, s2, "same.wav");
   CHECK(ok1 && ok2);
   if (!ok1 || !ok2) {
      return;
   }
   CHECK(libretro_miniaudio_sound_get_length(&x) == l1);
   CHECK(libretro_miniaudio_sound_get_length(&y) == l2);

   libretro_miniaudio_sound_unload(copy_snd);
   CHECK(libretro_miniaudio_sound_get_length(keep_snd) == keep_len);
   libretro_miniaudio_sound_play(keep_snd);
   reset_stats();
   run_frames(10);
   CHECK(g_frames == 8000);
   CHECK(g_nonzero > 0);
   CHECK(libretro_miniaudio_sound_is_playing(keep_snd));
   libretro_miniaudio_sound_unload(keep_snd);
   CHECK(tracked_count() == 0);
   CHECK(no_registrations());
}

static void test_shared_names(void) {
   libretro_miniaudio_config cfg = make_config();
   libretro_miniaudio_sound a, b;
   uint64_t amen_len = 0, tone_len = 0;

   CHECK(libretro_miniaudio_init(&cfg));

   /* Reference lengths, each loaded alone. */
   CHECK(libretro_miniaudio_sound_load_from_memory(&a, g_amen, g_amen_size, NULL));
   CHECK(libretro_miniaudio_sound_load_from_memory(&b, g_tone, g_tone_size, NULL));
   amen_len = libretro_miniaudio_sound_get_length(&a);
   tone_len = libretro_miniaudio_sound_get_length(&b);
   libretro_miniaudio_sound_unload(&a);
   libretro_miniaudio_sound_unload(&b);
   CHECK(tone_len == 24000);  /* 11025 frames at 22050 Hz, resampled to 48000 Hz */
   CHECK(amen_len > 0 && amen_len != tone_len);

   check_shared_name(g_amen, g_amen_size, amen_len, g_tone, g_tone_size, tone_len, false);
   check_shared_name(g_tone, g_tone_size, tone_len, g_amen, g_amen_size, amen_len, false);
   check_shared_name(g_amen, g_amen_size, amen_len, g_tone, g_tone_size, tone_len, true);
   check_shared_name(g_tone, g_tone_size, tone_len, g_amen, g_amen_size, amen_len, true);

   libretro_miniaudio_deinit();
}

/* --- teardown: deinit without unload, and unload edge cases ------------- */

static void test_teardown(void) {
   libretro_miniaudio_config cfg = make_config();
   libretro_miniaudio_sound s[3], d, r, z;
   int i;

   /* Uninit with sounds still loaded and playing. */
   CHECK(libretro_miniaudio_init(&cfg));
   CHECK(libretro_miniaudio_sound_load_from_memory(&s[0], g_amen, g_amen_size, "a"));
   CHECK(libretro_miniaudio_sound_load_from_memory_copy(&s[1], g_amen, g_amen_size, "b"));
   CHECK(libretro_miniaudio_sound_load_from_memory_copy(&s[2], g_amen, g_amen_size, "c"));
   for (i = 0; i < 3; ++i) {
      libretro_miniaudio_sound_play(&s[i]);
   }
   reset_stats();
   run_frames(5);
   CHECK(g_frames == 4000);
   CHECK(g_nonzero > 0);
   CHECK(libretro_miniaudio_sound_is_playing(&s[2]));
   CHECK(libretro_miniaudio_sound_get_position(&s[2]) > 0);
   libretro_miniaudio_deinit();
   CHECK(tracked_count() == 0);
   CHECK(!g_libretro_miniaudio.initialized);

   /* After deinit every call is a no-op, queries return false/0 and run()
    * never calls back. */
   for (i = 0; i < 3; ++i) {
      libretro_miniaudio_sound_unload(&s[i]);
      libretro_miniaudio_sound_play(&s[i]);
      libretro_miniaudio_sound_stop(&s[i]);
      libretro_miniaudio_sound_set_looping(&s[i], true);
      libretro_miniaudio_sound_set_volume(&s[i], 0.5f);
      libretro_miniaudio_sound_seek(&s[i], 10);
      CHECK(!libretro_miniaudio_sound_is_playing(&s[i]));
      CHECK(libretro_miniaudio_sound_get_position(&s[i]) == 0);
      CHECK(libretro_miniaudio_sound_get_length(&s[i]) == 0);
      libretro_miniaudio_sound_unload(&s[i]);
   }
   reset_stats();
   libretro_miniaudio_run(full_cb);
   CHECK(g_calls == 0);
   libretro_miniaudio_deinit();  /* double deinit */

   /* Re-init; double unload, zeroed, NULL and stale handles are no-ops. */
   CHECK(libretro_miniaudio_init(&cfg));
   CHECK(libretro_miniaudio_sound_load_from_memory_copy(&d, g_amen, g_amen_size, "d"));
   CHECK(libretro_miniaudio_sound_load_from_memory(&r, g_amen, g_amen_size, "r"));
   libretro_miniaudio_sound_unload(&d);
   libretro_miniaudio_sound_unload(&d);
   CHECK(tracked_count() == 1);
   memset(&z, 0, sizeof(z));
   libretro_miniaudio_sound_unload(&z);
   libretro_miniaudio_sound_unload(NULL);
   for (i = 0; i < 3; ++i) {
      libretro_miniaudio_sound_unload(&s[i]);
   }
   CHECK(tracked_count() == 1);

   /* Normal use still works after all of the above. */
   libretro_miniaudio_sound_play(&r);
   reset_stats();
   run_frames(10);
   CHECK(g_frames == 8000);
   CHECK(g_nonzero > 0);
   CHECK(libretro_miniaudio_sound_is_playing(&r));
   CHECK(libretro_miniaudio_sound_get_position(&r) > 0);
   CHECK(libretro_miniaudio_sound_get_length(&r) > 0);
   libretro_miniaudio_sound_unload(&r);
   CHECK(tracked_count() == 0);
   CHECK(no_registrations());
   libretro_miniaudio_deinit();

   /* A copy loaded in a later session, deinit without unload, then unload. */
   CHECK(libretro_miniaudio_init(&cfg));
   CHECK(libretro_miniaudio_sound_load_from_memory_copy(&s[0], g_amen, g_amen_size, "e"));
   libretro_miniaudio_sound_play(&s[0]);
   run_frames(3);
   libretro_miniaudio_deinit();
   libretro_miniaudio_sound_unload(&s[0]);
   CHECK(tracked_count() == 0);
}

/* --- batch callback: partial and zero acceptance ------------------------ */

static void test_batch_callback(void) {
   libretro_miniaudio_config cfg = make_config();
   libretro_miniaudio_sound s;
   size_t expect;
   int i, ok;

   CHECK(libretro_miniaudio_init(&cfg));
   expect = (size_t)(cfg.sample_rate / cfg.fps);  /* 800 */

   /* A callback that takes 400 at a time still receives every frame. */
   ok = 1;
   for (i = 0; i < 10; ++i) {
      reset_stats();
      libretro_miniaudio_run(half_cb);
      if (g_frames != expect || g_calls != (expect + 399) / 400) {
         ok = 0;
      }
   }
   CHECK(ok);

   /* A callback that takes nothing makes run() give up after one call. */
   reset_stats();
   libretro_miniaudio_run(zero_cb);
   CHECK(g_calls == 1);

   /* With no sounds the output is pure silence. */
   reset_stats();
   run_frames(10);
   CHECK(g_frames == expect * 10);
   CHECK(g_nonzero == 0);

   /* With a sound playing, partial delivery still carries audio. */
   CHECK(libretro_miniaudio_sound_load_from_memory(&s, g_amen, g_amen_size, "amen"));
   libretro_miniaudio_sound_play(&s);
   reset_stats();
   for (i = 0; i < 10; ++i) {
      libretro_miniaudio_run(half_cb);
   }
   CHECK(g_frames == expect * 10);
   CHECK(g_nonzero > 0);

   /* play/stop/seek return void; check their effect. */
   libretro_miniaudio_sound_stop(&s);
   CHECK(!libretro_miniaudio_sound_is_playing(&s));
   libretro_miniaudio_sound_seek(&s, 1000);
   CHECK(libretro_miniaudio_sound_get_position(&s) == 1000);
   libretro_miniaudio_sound_play(&s);
   CHECK(libretro_miniaudio_sound_is_playing(&s));
   CHECK(libretro_miniaudio_sound_get_length(&s) > 0);

   libretro_miniaudio_sound_unload(&s);
   libretro_miniaudio_deinit();
}

int main(int argc, char* argv[]) {
   const char* amen_path = argc > 1 ? argv[1] : "../example/amen.wav";

   /* Keep failure output ahead of any sanitizer report. */
   setvbuf(stdout, NULL, _IONBF, 0);

   g_amen = load_file(amen_path, &g_amen_size);
   if (!g_amen) {
      fprintf(stderr, "error: could not read '%s'; pass the path to example/amen.wav as the first argument.\n", amen_path);
      return 2;
   }
   g_tone = make_wav(11025, &g_tone_size);
   if (!g_tone) {
      fprintf(stderr, "error: out of memory\n");
      free(g_amen);
      return 2;
   }

   test_frames();
   test_config();
   test_load_unload_stress();
   test_shared_names();
   test_teardown();
   test_batch_callback();

   free(g_amen);
   free(g_tone);

   printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
   return g_failures ? 1 : 0;
}
