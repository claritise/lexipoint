// Lexipoint needs a touchscreen (D20, docs/v0.1/standalone-repo.md §4 step 4): the build enforces it rather than
// the code quietly assuming it, and a touch device the SDK supports passes on its own. Firmware only: the host
// tests don't compile this file.
#include <BoardConfig.h>

#if !FREEINK_CAP_TOUCH
#error "Lexipoint needs a touchscreen device (FREEINK_CAP_TOUCH)"
#endif
