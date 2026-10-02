#include "ui/status_bar.h"

namespace gpui {

namespace component {

StatusBar* StatusBar::New(Ctx* cx) {
    Arena* a = cx->a;
    StatusBar* s = ArenaNew<StatusBar>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

// A plain string is `impl IntoElement` in Rust and the commonest item on a
// bar; it takes the bar's own text style, so it is a bare TextEl here.
static El* BarText(Ctx* cx, Str s) {
    return TextEl(cx->a, s)->Font(12)->Fg(ThemeNow(cx->app).mutedFg);
}

StatusBar* StatusBar::Left(El* e) {
    left.Append(a, e);
    return this;
}
StatusBar* StatusBar::Left(Str s) {
    return Left(BarText(cx, s));
}
StatusBar* StatusBar::Center(El* e) {
    center.Append(a, e);
    return this;
}
StatusBar* StatusBar::Center(Str s) {
    return Center(BarText(cx, s));
}
StatusBar* StatusBar::Right(El* e) {
    right.Append(a, e);
    return this;
}
StatusBar* StatusBar::Right(Str s) {
    return Right(BarText(cx, s));
}

El* StatusBar::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // `region()`: h_flex().overflow_hidden().items_center().gap_2().
    auto region = [&]() {
        return Div(a)->FlexRow()->ClipX()->ClipY()->ItemsCenter()->Gap(
            Rems(cx, 0.5f));
    };
    bool hasLeft = left.len > 0;
    bool hasRight = right.len > 0;
    El* bar = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->Shrink0()
                  ->ItemsCenter()
                  ->Gap(Rems(cx, 0.5f))
                  ->PadY(Rems(cx, 0.25f))
                  ->PadX(Rems(cx, 0.5f))
                  ->Bg(th.tokens.statusBar)
                  ->BorderT(1, th.statusBarBorder);
    if (hasLeft) {
        El* r = region();
        for (int i = 0; i < left.len; i++) {
            r->Child(left[i]);
        }
        bar->Child(r);
    }
    El* mid = region()->Flex1();
    if (hasLeft && hasRight) {
        mid->JustifyCenter();
    } else if (hasLeft) {
        mid->JustifyEnd();
    }
    for (int i = 0; i < center.len; i++) {
        mid->Child(center[i]);
    }
    bar->Child(mid);
    if (hasRight) {
        El* r = region();
        for (int i = 0; i < right.len; i++) {
            r->Child(right[i]);
        }
        bar->Child(r);
    }
    return bar;
}

} // namespace component
} // namespace gpui
