# libretro-miniaudio

[miniaudio](https://miniaud.io/) use for libretro.

## Usage

```c
#define LIBRETRO_MINIAUDIO_OGG // Optionally enable ogg support.
#define LIBRETRO_MINIAUDIO_IMPLEMENTATION
#include "libretro-miniaudio.h"
```

## Building
To compile, you will need a C compiler and assorted toolchain installed.

	make

## Running

```
# mac
/Applications/RetroArch.app/Contents/MacOS/RetroArch -L testaudio_no_callback_libretro.dylib amen.wav

# windows
retroArch -L testaudio_no_callback_libretro.dll amen.ogg

# linux
retroArch -L testaudio_no_callback_libretro.so amen.flac
```