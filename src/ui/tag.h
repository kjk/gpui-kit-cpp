#ifndef GPUI_SRC_UI_TAG_H_
#define GPUI_SRC_UI_TAG_H_
/* Themed tag — crates/ui/src/tag.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

enum class TagVariant : uint8_t {
    Primary,
    Secondary,
    Danger,
    Success,
    Warning,
    Info
};

struct Tag {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    TagVariant variant = TagVariant::Secondary;
    bool outline = false;
    UiSize size = UiSize::Medium;
    float radius = -1;
    Str text = {};
    Rgba customBg = {};
    Rgba customFg = {};
    Rgba customBorder = {};
    bool hasCustom = false;
    // ParentElement: what tag.rs renders inside the chip, after `text`.
    ArenaVec<El*> children;

    static Tag* New(Ctx* cx, Str text);
    Tag* Primary();
    Tag* Secondary();
    Tag* Danger();
    Tag* Success();
    Tag* Warning();
    Tag* Info();
    Tag* Outline();
    Tag* WithSize(UiSize s);
    Tag* Radius(float v);
    Tag* Child(El* e);
    // Tag::custom(color, foreground, border) — and Tag::color(name), which
    // is the same three off a Tailwind scale.
    Tag* Custom(Rgba bg, Rgba fg, Rgba border = {});
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_TAG_H_
