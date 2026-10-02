#include "ui/tooltip.h"

namespace gpui {

namespace component {

Tooltip* Tooltip::New(Ctx* cx, Str text) {
    Arena* a = cx->a;
    Tooltip* t = ArenaNew<Tooltip>(a);
    t->a = a;
    t->cx = cx;
    t->text = text;
    return t;
}

// tooltip.rs Render: base's tooltip as a popover-coloured h_flex with
// m_3, a border, shadow_md and the theme's radius, py_0p5 px_2 gap_3 at
// text_sm, inside a div that lets the margin show; as tall as its text.
El* Tooltip::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Rgba ink = Rgba8(0, 0, 0, 26);
    BoxShadow shadowMd[2] = {
        {0, 4.f, 6.f, -1.f, ink, false},
        {0, 2.f, 4.f, -2.f, ink, false},
    };
    El* popup = gpui::Tooltip::New(cx, StrL("tooltip-popup"))
                    ->FlexRow()
                    ->ItemsCenter()
                    ->JustifyBetween()
                    ->Margin(Rems(cx, 0.75f))
                    ->Bg(th.tokens.popover)
                    ->Fg(th.popoverFg)
                    ->Border(1, th.border)
                    ->Shadows(shadowMd, 2)
                    ->Radius(th.radius)
                    ->PadY(Rems(cx, 0.125f))
                    ->PadX(Rems(cx, 0.5f))
                    ->Font(14)
                    ->Gap(Rems(cx, 0.75f))
                    ->Child(Div(a)->Child(TextEl(a, text)));
    return Div(a)->Child(popup);
}

} // namespace component
} // namespace gpui
