/* Arduino puts only src/ on the include path; this forwards to the vendored Monocypher header.
 * Other builds find this file first too, and it resolves to the same header. */
#include "../third_party/monocypher/monocypher.h"
