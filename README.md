# libretro-miniaudio

[miniaudio](https://miniaud.io/) audio playback for [libretro](https://www.libretro.com/) cores via a single-header C library.

## Features

- **WAV**, **MP3**, and **FLAC** support out of the box
- **OGG** support via `#define LIBRETRO_MINIAUDIO_OGG` (uses stb_vorbis)
- Single-header, no external build dependencies
- Drives audio through the libretro `retro_audio_sample_batch_t` callback

## Usage

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

## Example

```c
#define LIBRETRO_MINIAUDIO_IMPLEMENTATION
#include "libretro-miniaudio.h"

static libretro_miniaudio_sound g_sound;

// Called once when a game is loaded.
bool retro_load_game(const struct retro_game_info* info) {
    libretro_miniaudio_config cfg = libretro_miniaudio_config_default();
    cfg.log_cb = log_cb;  // optional retro_log_printf_t
    if (!libretro_miniaudio_init(&cfg))
        return false;

    // Load audio directly from in-memory content data.
    if (!libretro_miniaudio_sound_load_from_memory(&g_sound, info->data, info->size, info->path)) {
        libretro_miniaudio_uninit();
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
    libretro_miniaudio_uninit();
}
```

## API

### Engine

| Function | Description |
|---|---|
| `libretro_miniaudio_config libretro_miniaudio_config_default(void)` | Returns a config with default values (48000 Hz, 2 ch, 60 fps). |
| `bool libretro_miniaudio_init(const libretro_miniaudio_config* cfg)` | Initialises the audio engine. Call once in `retro_load_game`. |
| `void libretro_miniaudio_uninit(void)` | Shuts down the engine and frees resources. |
| `void libretro_miniaudio_run(retro_audio_sample_batch_t audio_batch_cb)` | Mixes and submits one frame of audio. Call every `retro_run`. |
| `ma_engine* libretro_miniaudio_engine(void)` | Returns the underlying `ma_engine*` for advanced miniaudio use. |

### Sounds

| Function | Description |
|---|---|
| `bool libretro_miniaudio_sound_load(libretro_miniaudio_sound* sound, const char* path)` | Loads a sound from a file path. |
| `bool libretro_miniaudio_sound_load_from_memory(libretro_miniaudio_sound* sound, const void* data, size_t size, const char* name)` | Loads a sound from a memory buffer. |
| `void libretro_miniaudio_sound_unload(libretro_miniaudio_sound* sound)` | Unloads and frees a sound. |
| `void libretro_miniaudio_sound_play(libretro_miniaudio_sound* sound)` | Starts playback. |
| `void libretro_miniaudio_sound_stop(libretro_miniaudio_sound* sound)` | Stops playback. |
| `void libretro_miniaudio_sound_set_looping(libretro_miniaudio_sound* sound, bool loop)` | Enables or disables looping. |
| `void libretro_miniaudio_sound_set_volume(libretro_miniaudio_sound* sound, float volume)` | Sets volume (0.0 to 1.0). |
| `bool libretro_miniaudio_sound_is_playing(const libretro_miniaudio_sound* sound)` | Returns true if the sound is currently playing. |

## Building

```
make
```

## Running

```bash
# macOS
/Applications/RetroArch.app/Contents/MacOS/RetroArch -L testaudio_no_callback_libretro.dylib amen.wav

# Windows
retroarch -L testaudio_no_callback_libretro.dll amen.ogg

# Linux
retroarch -L testaudio_no_callback_libretro.so amen.flac
```

## License

libretro-miniaudio is licensed under the [MIT License](https://opensource.org/licenses/MIT).

[miniaudio](https://miniaud.io/) is licensed under MIT-0 or Public Domain (your choice).

[stb_vorbis](https://github.com/nothings/stb) (used for OGG support) is public domain.
