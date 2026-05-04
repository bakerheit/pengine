/* stb_vorbis must be compiled BEFORE miniaudio so that
   STB_VORBIS_INCLUDE_STB_VORBIS_H is defined when miniaudio.h is processed.
   Without it, miniaudio skips the Vorbis decoder and .ogg files fail with
   MA_INVALID_FILE. The stb include path is set by CMakeLists. */
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
