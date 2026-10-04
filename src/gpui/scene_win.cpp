/* Windows reads the scene level from the paint options. The fallback the
   other targets return is scene.cpp's SceneLevelFallback. */

#include "gpui/scene.h"

namespace gpui {

int SceneLevelOn() {
    static_assert((int)WinSceneMode::Off == kSceneOff);
    static_assert((int)WinSceneMode::Replay == kSceneReplay);
    static_assert((int)WinSceneMode::Cache == kSceneCache);
    static_assert((int)WinSceneMode::Skip == kSceneSkip);
    static_assert((int)WinSceneMode::Damage == kSceneDamage);
    return (int)WinPaintOptionsGet().scene;
}

} // namespace gpui
