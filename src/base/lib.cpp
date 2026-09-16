#include "base/lib.h"
#include "gpui/platform.h"

namespace gpui {

void ApplySystemReduceMotion() {
    MotionSetReduced(PlatReduceMotion());
}

void BaseInit(App* app) {
    if (!app) {
        return;
    }
    (void)BaseThemeGlobal(app);
    BaseGlobalStateInit(app);
    (void)PanelRegistryGlobal(app);
    FocusTrapInit(app);
    ApplySystemReduceMotion();

    DialogInitKeys();
    DatePickerInitKeys();
    SelectInitKeys();
    InputInitKeys();
    TreeInitKeys();

    // The modules whose escape binding is otherwise installed lazily by the
    // first rendered instance. Rust installs these during crate init.
    PopoverInitKeys();
    SheetInitKeys();
    CancelInitKeys("ColorPicker");
}

} // namespace gpui
