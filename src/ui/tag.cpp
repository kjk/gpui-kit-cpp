#include "ui/tag.h"

namespace gpui {

namespace component {

Tag* Tag::New(Ctx* cx, Str text) {
    Arena* a = cx->a;
    Tag* t = ArenaNew<Tag>(a);
    t->a = a;
    t->cx = cx;
    t->text = text;
    return t;
}

Tag* Tag::Primary() {
    variant = TagVariant::Primary;
    return this;
}
Tag* Tag::Secondary() {
    variant = TagVariant::Secondary;
    return this;
}
Tag* Tag::Danger() {
    variant = TagVariant::Danger;
    return this;
}
Tag* Tag::Success() {
    variant = TagVariant::Success;
    return this;
}
Tag* Tag::Warning() {
    variant = TagVariant::Warning;
    return this;
}
Tag* Tag::Info() {
    variant = TagVariant::Info;
    return this;
}
Tag* Tag::Outline() {
    outline = true;
    return this;
}
Tag* Tag::WithSize(UiSize s) {
    size = s;
    return this;
}
Tag* Tag::Radius(float v) {
    radius = v;
    return this;
}
Tag* Tag::Custom(Rgba bg, Rgba fg, Rgba border) {
    customBg = bg;
    customFg = fg;
    customBorder = border.a ? border : bg;
    hasCustom = true;
    return this;
}

El* Tag::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // TagVariant::fg takes `outline`: an outlined tag draws in the variant's
    // own colour rather than the colour that reads over it — except the
    // secondary one, whose border is too pale to write with.
    Rgba bg = th.secondary, fg = th.secondaryFg, bd = th.border;
    Rgba ofg = th.mutedFg;
    switch (variant) {
        case TagVariant::Primary:
            bg = th.primary;
            fg = th.primaryFg;
            bd = th.primary;
            ofg = th.primary;
            break;
        case TagVariant::Danger:
            bg = th.danger;
            fg = th.dangerFg;
            bd = th.danger;
            ofg = th.danger;
            break;
        case TagVariant::Success:
            bg = th.success;
            fg = th.successFg;
            bd = th.success;
            ofg = th.success;
            break;
        case TagVariant::Warning:
            bg = th.warning;
            fg = th.warningFg;
            bd = th.warning;
            ofg = th.warning;
            break;
        case TagVariant::Info:
            bg = th.info;
            fg = th.infoFg;
            bd = th.info;
            ofg = th.info;
            break;
        default:
            break;
    }
    if (hasCustom) {
        bg = customBg;
        fg = customFg;
        bd = customBorder;
        // Color(..) answers the same foreground either way.
        ofg = customFg;
    }
    if (outline) {
        fg = ofg;
        bg = Rgba{0, 0, 0, 0};
    }
    bool tiny = size == UiSize::XSmall || size == UiSize::Small;
    float r = radius >= 0 ? radius : (tiny ? th.radius * 0.5f : th.radius);
    return Div(a)
        ->PadX(tiny ? 6.f : 10.f)
        ->PadY(tiny ? 2.f : 4.f)
        ->Radius(r)
        ->Bg(bg)
        ->Border(1, bd)
        ->LineHeight(1.25f)
        ->ItemsCenter()
        ->Child(TextEl(a, text)->Font(12)->Fg(fg));
}

} // namespace component
} // namespace gpui
