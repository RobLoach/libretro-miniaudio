cmake_minimum_required(VERSION 3.11...3.31)

project(libretro_miniaudio LANGUAGES C)

# Build the example libretro core and tests only when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
	set(LIBRETRO_MINIAUDIO_IS_TOP_LEVEL ON)
else()
	set(LIBRETRO_MINIAUDIO_IS_TOP_LEVEL OFF)
endif()
option(LIBRETRO_MINIAUDIO_BUILD_EXAMPLE "Build the example libretro core in example/." ${LIBRETRO_MINIAUDIO_IS_TOP_LEVEL})
option(LIBRETRO_MINIAUDIO_BUILD_TESTS "Build the tests in test/." ${LIBRETRO_MINIAUDIO_IS_TOP_LEVEL})

############################################
# Header-only library.                     #
############################################

add_library(libretro_miniaudio INTERFACE)
add_library(libretro_miniaudio::libretro_miniaudio ALIAS libretro_miniaudio)

# 'libretro-miniaudio.h', 'miniaudio.h' and 'stb_vorbis.c' all live in the root.
target_include_directories(libretro_miniaudio INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# miniaudio requires the math library on Unix-like platforms.
if(UNIX)
	target_link_libraries(libretro_miniaudio INTERFACE m)
endif()

############################################
# Example libretro core.                   #
############################################

if(LIBRETRO_MINIAUDIO_BUILD_EXAMPLE)
	add_subdirectory(example)
endif()

############################################
# Tests.                                   #
############################################

if(LIBRETRO_MINIAUDIO_BUILD_TESTS)
	enable_testing()
	add_subdirectory(test)
endif()
