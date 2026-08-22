/* The single translation unit that expands miniaudio's implementation.
 *
 * It lives alone so the ~90k lines of third-party C are compiled exactly once,
 * and so the whole file can be built with warnings off (see the module's
 * CMakeLists.txt) without relaxing anything on our own code.
 *
 * This TU belongs to apricot_host. It must never end up in apricot_sim. */

#define MINIAUDIO_IMPLEMENTATION

/* No encoders, no decoders, no file IO: apricot synthesises every sound it
 * plays (see audio/synth.h), so all of that is dead weight and dead surface. */
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION

#include "miniaudio.h"
