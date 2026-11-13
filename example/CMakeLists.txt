# Built through the root CMakeLists.txt with LIBRETRO_MINIAUDIO_BUILD_EXAMPLE.

option(BUILD_SHARED_LIBS "Create a shared library instead of a static library." ON)

# Sources.
add_library(miniaudio_libretro
	"libretro.c"
)

# Link the header-only libretro-miniaudio library.
target_link_libraries(miniaudio_libretro PRIVATE libretro_miniaudio::libretro_miniaudio)

############################################
# Standard libretro core boilerplate code. #
############################################

# Avoid some relocation-related linker errors when building a shared library that depends on static libraries.
set_target_properties(miniaudio_libretro PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Include libretro-common, so that 'libretro.h' can be found.
target_include_directories(miniaudio_libretro PRIVATE "libretro-common/include" ".")

# Provide an 'stdint.h' for older versions of MSVC.
if(MSVC AND MSVC_VERSION LESS 1600)
	target_include_directories(miniaudio_libretro PRIVATE "libretro-common/include/compat/msvc")
endif()

# Give the code a way of determining that it is targeting libretro.
target_compile_definitions(miniaudio_libretro PRIVATE __LIBRETRO__)

# Allow only the libretro API functions to be exported.
set_target_properties(miniaudio_libretro PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# Restrict exported symbols to retro_* with the version script on GNU-style linkers.
if(NOT APPLE AND NOT MSVC AND NOT EMSCRIPTEN)
	set_property(TARGET miniaudio_libretro APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/link.T")
	set_property(TARGET miniaudio_libretro APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/link.T")
endif()

# Adjust the library's filename. This is relied upon by libretro's CI infrastructure.

# Remove the 'lib' prefix.
set_target_properties(miniaudio_libretro PROPERTIES PREFIX "")

# Append the given suffix.
set_target_properties(miniaudio_libretro PROPERTIES OUTPUT_NAME "miniaudio_libretro${LIBRETRO_SUFFIX}")

# Emscripten builds are expected to use the '.bc' file extension instead of '.a'.
if(LIBRETRO_STATIC AND EMSCRIPTEN)
	set_target_properties(miniaudio_libretro PROPERTIES SUFFIX ".bc")
endif()

# Obtain the Git revision so that it can be displayed in the core's version string.
find_package(Git)
if(GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
		OUTPUT_VARIABLE GIT_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(GIT_VERSION)
		target_compile_definitions(miniaudio_libretro PRIVATE GIT_VERSION=" ${GIT_VERSION}")
	endif()
endif()
