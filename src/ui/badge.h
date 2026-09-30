#ifndef GPUI_UI_BADGE_H_
#define GPUI_UI_BADGE_H_
/* Themed badge — crates/ui/src/badge.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

enum class BadgeKind : uint8_t {
    Number,
    Dot,
    Icon
};

struct Badge {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    int count = 0;
    int max = 99;
    BadgeKind kind = BadgeKind::Number;
    IconName icon = IconName::None;
    Rgba color = {};
    bool hasColor = false;
    UiSize size = UiSize::Medium;
    // ParentElement: badge.rs extends a Vec, and the dot sits over all of
    // them.
    ArenaVec<El*> children;

    static Badge* New(Ctx* cx);
    Badge* Count(int n);
    Badge* Max(int n);
    Badge* Dot();
    Badge* Icon(IconName n);
    Badge* Color(Rgba c);
    Badge* WithSize(UiSize s);
    Badge* Child(El* c);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_BADGE_H_
