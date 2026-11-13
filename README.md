# libretro-miniaudio

[miniaudio](https://miniaud.io/) audio playback for [libretro](https://www.libretro.com/) cores via a header-only C library.

## Features

- **WAV**, **MP3**, and **FLAC** support out of the box
- **OGG** support via `#define LIBRETRO_MINIAUDIO_OGG` (uses stb_vorbis)
- No external build dependencies
- Drives audio through the libretro `retro_audio_sample_batch_t` callback

## Usage

Copy `libretro-miniaudio.h`, `miniaudio.h` and `stb_vorbis.c` from the repository root into your core, add that directory and `libretro-common/include` (for `libretro.h`) to your include path, and link with `-lm`.

With CMake, add this repository as a subdirectory instead:

```cmake
add_subdirectory(libretro-miniaudio)
target_link_libraries(your_core PRIVATE libretro_miniaudio::libretro_miniaudio)
```

In exactly one `.c` file define the implementation before including:

```c
#define LIBRETRO_MINIAUDIO_OGG          // optional: enable OGG/Vorbis support
#define LIBRETRO_MINIAUDIO_IMPLEMENTATION
#include "libretro-miniaudio.h"
```

In all other files just include the header:

```c
#include "libretro-miniaudio.h"
```

## Configuration

`libretro_miniaudio_config_default()` returns a config struct you can customize before passing to `libretro_miniaudio_init()` (passing `NULL` uses the defaults):

| Field | Type | Default | Notes |
|---|---|---|---|
| `sample_rate` | `uint32_t` | `48000` | Output sample rate in Hz, must be > 0 |
| `channels` | `uint32_t` | `2` | Must be 2, libretro takes interleaved stereo |
| `fps` | `double` | `60.0` | Must be > 0 and match `retro_system_timing.fps` |
| `log_cb` | `retro_log_printf_t` | `NULL` | Optional libretro log callback |
| `vfs` | `struct retro_vfs_interface*` | `NULL` | Optional libretro VFS for `libretro_miniaudio_sound_load_from_file()`. `NULL` uses miniaudio's stdio VFS. Must outlive the engine |

`fps` controls how many frames of audio are mixed per `retro_run()` call, so a mismatch causes buffer underruns or overruns. Cores running at non-standard rates (50 Hz PAL, 75 Hz arcade) should set this to match. When `sample_rate / fps` is not a whole number, `libretro_miniaudio_run()` carries the fractional remainder between calls.

## Example

```c
#include "libretro.h"

#define LIBRETRO_MINIAUDIO_IMPLEMENTATION
#include "libretro-miniaudio.h"

static retro_log_printf_t log_cb;              // from RETRO_ENVIRONMENT_GET_LOG_INTERFACE
static retro_audio_sample_batch_t audio_batch_cb;
static libretro_miniaudio_sound g_sound;

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_batch_cb = cb;
}

// Called once when a game is loaded.
bool retro_load_game(const struct retro_game_info* info) {
    libretro_miniaudio_config cfg = libretro_miniaudio_config_default();
    cfg.log_cb = log_cb;
    if (!libretro_miniaudio_init(&cfg))
        return false;

    // Copy the content data, as the frontend may free it after this returns.
    if (!libretro_miniaudio_sound_load_from_memory_copy(&g_sound, info->data, info->size, info->path)) {
        libretro_miniaudio_deinit();
        return false;
    }

    libretro_miniaudio_sound_set_looping(&g_sound, true);
    libretro_miniaudio_sound_play(&g_sound);
    return true;
}

// Called every frame -- mixes and submits audio samples.
void retro_run(void) {
    libretro_miniaudio_run(audio_batch_cb);
}

// Called when the game is unloaded.
void retro_unload_game(void) {
    libretro_miniaudio_sound_unload(&g_sound);
    libretro_miniaudio_deinit();
}
```

Use `libretro_miniaudio_sound_load_from_memory()` only when the data outlives the sound.

## Streaming from files

Long music tracks can be streamed from disk instead of being held in memory. Get the frontend's VFS (version 1 or later) and pass it in the config, then load by path:

```c
static struct retro_vfs_interface* vfs;  // from RETRO_ENVIRONMENT_GET_VFS_INTERFACE

libretro_miniaudio_config cfg = libretro_miniaudio_config_default();
cfg.vfs = vfs;
libretro_miniaudio_init(&cfg);

// true streams the file a page at a time; false decodes it all up front.
libretro_miniaudio_sound_load_from_file(&g_music, "/path/to/music.ogg", true);
```

Streamed sounds keep the file open until unloaded and decode their next page during `libretro_miniaudio_run()`. WAV, FLAC and MP3 (via dr_libs) and OGG (via stb_vorbis, with `LIBRETRO_MINIAUDIO_OGG`) all support streaming.

## API

```c
libretro_miniaudio_config libretro_miniaudio_config_default(void);
bool libretro_miniaudio_init(const libretro_miniaudio_config* cfg);
void libretro_miniaudio_deinit(void);
void libretro_miniaudio_run(retro_audio_sample_batch_t audio_batch_cb);
ma_engine* libretro_miniaudio_engine(void);
void libretro_miniaudio_set_master_volume(float volume);
float libretro_miniaudio_get_master_volume(void);
const char* libretro_miniaudio_version(void);

bool libretro_miniaudio_sound_load_from_memory(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name);
bool libretro_miniaudio_sound_load_from_memory_copy(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name);
bool libretro_miniaudio_sound_load_from_file(libretro_miniaudio_sound* sound, const char* path, bool stream);
void libretro_miniaudio_sound_unload(libretro_miniaudio_sound* sound);
void libretro_miniaudio_sound_play(libretro_miniaudio_sound* sound);
void libretro_miniaudio_sound_stop(libretro_miniaudio_sound* sound);
void libretro_miniaudio_sound_set_looping(libretro_miniaudio_sound* sound, bool loop);
void libretro_miniaudio_sound_set_volume(libretro_miniaudio_sound* sound, float volume);
bool libretro_miniaudio_sound_is_playing(const libretro_miniaudio_sound* sound);
void libretro_miniaudio_sound_seek(libretro_miniaudio_sound* sound, uint64_t frame_index);
uint64_t libretro_miniaudio_sound_get_position(const libretro_miniaudio_sound* sound);
uint64_t libretro_miniaudio_sound_get_length(const libretro_miniaudio_sound* sound);
```

## Options

Define these before including `libretro-miniaudio.h`:

| Macro | Description |
|-------|-------------|
| `LIBRETRO_MINIAUDIO_IMPLEMENTATION` | Emit the implementation. Define it in exactly one `.c` file. |
| `LIBRETRO_MINIAUDIO_OGG` | Enable OGG/Vorbis decoding via `stb_vorbis.c`. |
| `LIBRETRO_MINIAUDIO_MAX_SOUNDS` | Maximum number of sounds loaded at once. Defaults to `64`. |
| `LIBRETRO_MINIAUDIO_NO_MA_CONFIG` | Skip the default miniaudio `MA_NO_*` options, so you can configure miniaudio yourself. Individual `MA_*` options can also be defined without it. |

## Building

The example core lives in `example/` and needs the `libretro-common` submodule:

```bash
git submodule update --init

# Make: builds example/miniaudio_libretro.so (.dylib on macOS, .dll on Windows)
make -C example

# or CMake: builds build/example/miniaudio_libretro.so
cmake -S . -B build
cmake --build build
```

## Running

```bash
retroarch -L example/miniaudio_libretro.so example/amen.wav
```

The example core also plays OGG, FLAC and MP3 content.

## Testing

The tests in `test/` also use the `libretro-common` submodule:

```bash
# Make: builds and runs the tests (SANITIZE=1 adds AddressSanitizer and UBSan)
make -C test

# or CMake
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

- libretro-miniaudio is licensed under the [MIT License](https://opensource.org/licenses/MIT)
- [miniaudio](https://miniaud.io/) is licensed under MIT-0 or Public Domain (your choice)
- [stb_vorbis](https://github.com/nothings/stb) (used for OGG support) is public domain
