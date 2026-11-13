/* Arduino compiles only the files under src/, so the vendored Monocypher is pulled in from here.
 * CMake and ESP-IDF list monocypher.c themselves and never build this file. */
#if defined(ARDUINO)
#include "../third_party/monocypher/monocypher.c"
#else
typedef int vban_monocypher_unused; /* keeps the file a valid C translation unit elsewhere */
#endif
