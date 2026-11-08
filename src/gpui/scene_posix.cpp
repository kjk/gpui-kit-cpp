/* Cairo, Core Graphics and Canvas2D back buffers have not been checked to
   hold the last frame's pixels, so every non-Windows target redraws a changed
   frame whole and only skips an identical one. */

#include "gpui/scene.h"

namespace gpui {

bool SceneDamageSupported() {
    return false;
}

} // namespace gpui
