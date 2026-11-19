#ifndef GPUI_BASE_LAYOUT_DIRECTION_H_
#define GPUI_BASE_LAYOUT_DIRECTION_H_

#include "gpui/gpui.h"

namespace gpui {

enum class LayoutDirection : uint8_t {
    Ltr,
    Rtl
};

LayoutDirection LayoutDirectionGlobal(const App* app);
void LayoutDirectionSetGlobal(App* app, LayoutDirection direction);
LayoutDirection LayoutDirectionFromLocale(Str locale);
LayoutDirection LayoutDirectionSystem();
void LayoutDirectionInit(App* app);
Anchor LogicalAnchor(Anchor anchor, const App* app);
IconName DirectionalChevron(const App* app, bool towardEnd);

inline bool LayoutIsRtl(const App* app) {
    return LayoutDirectionGlobal(app) == LayoutDirection::Rtl;
}

} // namespace gpui
#endif // GPUI_BASE_LAYOUT_DIRECTION_H_
