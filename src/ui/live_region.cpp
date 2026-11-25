#include "ui/live_region.h"

namespace gpui::component {

LiveRegion* LiveRegion::New(Ctx* cx, Str id, Str text) {
    LiveRegion* self = StyledBuilder<LiveRegion>::New(cx);
    self->id = StrDup(cx->a, id);
    self->text = StrDup(cx->a, text);
    return self;
}

LiveRegion* LiveRegion::Assertive() {
    live = AccessibilityLive::Assertive;
    return this;
}

El* LiveRegion::IntoEl() {
    if (cx->win) {
        cx->win->hasLiveRegions = true;
    }
    El* region =
        Div(a)->Id(id)->Role(AccessibilityRole::Status)->AriaLive(live);
    if (styleSet) {
        region->Refine(style, styleSet);
    }
    if (len(text) > 0) {
        region->AriaLabel(text)->AriaValue(text)->Child(TextEl(a, text));
    }
    return region;
}

} // namespace gpui::component
