#include "ui/separator.h"

namespace gpui {

namespace component {

Separator* Separator::Vertical(Ctx* cx) {
    Arena* a = cx->a;
    Separator* s = ArenaNew<Separator>(a);
    s->a = a;
    s->cx = cx;
    s->vertical = true;
    return s;
}

Separator* Separator::Horizontal(Ctx* cx) {
    Arena* a = cx->a;
    Separator* s = ArenaNew<Separator>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

Separator* Separator::Dashed() {
    line = SeparatorStyle::Dashed;
    return this;
}

Separator* Separator::Label(Str s) {
    label = s;
    return this;
}

Separator* Separator::Color(Rgba c) {
    color = c;
    hasColor = true;
    return this;
}

El* Separator::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Rgba c = hasColor ? color : th.border;
    El* root = Div(a)->ItemsCenter()->JustifyCenter()->Shrink0();
    if (vertical) {
        // The rule is an absolute child, so it runs the whole height whether
        // or not there is a label; the label is drawn over it on the theme's
        // own background, which is what masks the middle of it.
        root->H(kFill);
        if (!label.s) {
            root->W(1);
        }
        El* lineEl = Div(a)->W(1)->H(kFill);
        if (label.s) {
            lineEl->Absolute();
        }
        // A dashed rule is the border alone; filling the box as well would
        // paint the gaps back in.
        if (line == SeparatorStyle::Dashed) {
            // PathBuilder::stroke(1).dash_array([4, 2]) in separator.rs.
            lineEl->Dashed()->DashArray(4, 2)->Border(1, c);
        } else {
            lineEl->Bg(c);
        }
        root->Child(lineEl);
    } else {
        root->FlexRow()->W(kFill)->H(label.s ? Rems(cx, 1.5f) : 1.f);
        // With a label beside it the rule gives way, so the pair together is
        // as wide as the separator and no wider.
        El* lineEl = Div(a)->H(1);
        if (label.s) {
            lineEl->Flex1()->MinW(0);
        } else {
            lineEl->W(kFill);
        }
        if (line == SeparatorStyle::Dashed) {
            // PathBuilder::stroke(1).dash_array([4, 2]) in separator.rs.
            lineEl->Dashed()->DashArray(4, 2)->Border(1, c);
        } else {
            lineEl->Bg(c);
        }
        root->Child(lineEl);
    }
    if (label.s) {
        root->Child(TextEl(a, label)
                        ->Font(12)
                        ->Fg(th.mutedFg)
                        ->Bg(th.tokens.background)
                        ->PadX(Rems(cx, 0.5f)));
    }
    return root;
}

} // namespace component
} // namespace gpui
