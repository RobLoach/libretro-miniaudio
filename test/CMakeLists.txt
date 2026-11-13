# Built through the root CMakeLists.txt with LIBRETRO_MINIAUDIO_BUILD_TESTS.

add_executable(libretro_miniaudio_test
	"test.c"
)

target_link_libraries(libretro_miniaudio_test PRIVATE libretro_miniaudio::libretro_miniaudio)

# Include libretro-common, so that 'libretro.h' can be found.
target_include_directories(libretro_miniaudio_test PRIVATE "${PROJECT_SOURCE_DIR}/example/libretro-common/include")

add_test(NAME libretro_miniaudio_test COMMAND libretro_miniaudio_test "${PROJECT_SOURCE_DIR}/example/amen.wav")
