/* Windows presents through a FLIP_SEQUENTIAL chain whose back buffers keep
   the last frame's pixels, which cmd/bench-scene.ts has checked on screen, so
   a frame that differs in part may be drawn in part. */

#include "gpui/scene.h"

namespace gpui {

bool SceneDamageSupported() {
    return true;
}

} // namespace gpui
