#ifndef GPUI_SRC_UI_TOOLTIP_H_
#define GPUI_SRC_UI_TOOLTIP_H_
/* Themed tooltip — crates/ui/src/tooltip.rs */

#include "base/tooltip.h"
#include "ui/sizing.h"

namespace gpui {

namespace component {

using TooltipDefaults = ::gpui::TooltipDefaults;

struct Tooltip {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str text = {};

    static Tooltip* New(Ctx* cx, Str text);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_TOOLTIP_H_
