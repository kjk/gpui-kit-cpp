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
    // badge.rs's usize count and max, in 64 bits.
    uint64_t count = 0;
    uint64_t max = 99;
    BadgeKind kind = BadgeKind::Number;
    IconName icon = IconName::None;
    Rgba color = {};
    bool hasColor = false;
    UiSize size = UiSize::Medium;
    // ParentElement: badge.rs extends a Vec, and the dot sits over all of
    // them.
    ArenaVec<El*> children;

    static Badge* New(Ctx* cx);
    Badge* Count(uint64_t n);
    Badge* Max(uint64_t n);
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
