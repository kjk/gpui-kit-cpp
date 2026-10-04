/* Every non-Windows target keeps the scene level SceneTakeArg wrote. */

#include "gpui/scene.h"

namespace gpui {

int SceneLevelFallback();

int SceneLevelOn() {
    return SceneLevelFallback();
}

} // namespace gpui
