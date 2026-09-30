#ifndef GPUI_UI_COLLAPSIBLE_H_
#define GPUI_UI_COLLAPSIBLE_H_
/* Themed collapsible — crates/ui/src/collapsible.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

struct Collapsible {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    bool open = false;
    // `motion_id(id)`: a stable identity turns the open/close into a
    // reversible measured reveal. Without one the content is dropped while
    // closed, exactly as before.
    Str motionId = {};
    bool hasMotion = false;
    El* trigger = nullptr;
    // ParentElement: what shows whether or not the collapsible is open, after
    // the trigger.
    ArenaVec<El*> children;
    El* content = nullptr;
    // How many children had been added when Content() was called: Rust's
    // base keeps the two in call order, so content set before a child comes
    // before it.
    int contentAt = 0;
    // The caller's own style on the collapsible's root: `w_full()` and
    // `gap_2()` are what every one of the story's carries.
    float width = 0;
    float gap = 0;

    static Collapsible* New(Ctx* cx);
    Collapsible* W(float v);
    Collapsible* Gap(float v);
    Collapsible* Open(bool v);
    Collapsible* MotionId(Str id);
    Collapsible* Trigger(El* e);
    Collapsible* Child(El* e);
    Collapsible* Content(El* e);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_COLLAPSIBLE_H_
