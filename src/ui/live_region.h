#ifndef GPUI_UI_LIVE_REGION_H_
#define GPUI_UI_LIVE_REGION_H_

#include "ui/sizing.h"

namespace gpui::component {

// crates/component/src/live_region.rs. Keep the ID stable to announce only
// a change of text. The region leaves keyboard focus where it is.
struct LiveRegion : StyledBuilder<LiveRegion> {
    Str id = {};
    Str text = {};
    AccessibilityLive live = AccessibilityLive::Polite;

    static LiveRegion* New(Ctx* cx, Str id, Str text);
    LiveRegion* Assertive();
    El* IntoEl();
};

} // namespace gpui::component

#endif // GPUI_UI_LIVE_REGION_H_
