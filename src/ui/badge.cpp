#include "ui/badge.h"

namespace gpui {

namespace component {

Badge* Badge::New(Ctx* cx) {
    Arena* a = cx->a;
    Badge* b = ArenaNew<Badge>(a);
    b->a = a;
    b->cx = cx;
    return b;
}

// Only dot() and icon() pick the variant; count() sets the value the Number
// variant shows and leaves a dot a dot, as the Dot story asks for.
Badge* Badge::Count(int n) {
    count = n;
    return this;
}
Badge* Badge::Max(int n) {
    max = n;
    return this;
}
Badge* Badge::Dot() {
    kind = BadgeKind::Dot;
    return this;
}
Badge* Badge::Icon(IconName n) {
    icon = n;
    kind = BadgeKind::Icon;
    return this;
}
Badge* Badge::Color(Rgba c) {
    color = c;
    hasColor = true;
    return this;
}
Badge* Badge::WithSize(UiSize s) {
    size = s;
    return this;
}
Badge* Badge::Child(El* c) {
    child = c;
    return this;
}

El* Badge::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // A number badge with nothing to say is not drawn; a dot or an icon is
    // always there.
    bool visible = kind != BadgeKind::Number || count > 0;
    El* root = Div(a);
    if (child) {
        root->Child(child);
    }
    if (!visible) {
        return root;
    }
    // (size, text_size) for the icon variant. The number's own text is 10
    // whatever the badge's size is.
    float box = 16;
    if (size == UiSize::Large) {
        box = 24;
    } else if (size == UiSize::Small || size == UiSize::XSmall) {
        box = 10;
    }
    Rgba bg = hasColor ? color : th.red;
    El* mark = Div(a)
                   ->Absolute()
                   ->FlexRow()
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->Bg(bg)
                   ->Fg(Rgb(0xff, 0xff, 0xff));
    if (kind == BadgeKind::Dot) {
        mark->Top(0)->Right(0)->W(6)->H(6)->Radius(3);
    } else if (kind == BadgeKind::Icon) {
        // The ring in the page's own background is what separates the glyph
        // from whatever it sits on.
        mark->Right(0)
            ->Bottom(0)
            ->W(box)
            ->H(box)
            ->Radius(th.radiusFull)
            ->Border(1, th.background)
            ->Child(IconEl(a, icon, box * 0.6f));
    } else {
        int shown = count > max ? max : count;
        Str txt = count > max ? StrDup(a, fmt("%d+", shown))
                              : StrDup(a, fmt("%d", shown));
        // The chip hangs off the corner by a step per digit, so a longer
        // count grows leftwards rather than pushing past the child.
        float step = 3, top = -3;
        if (size == UiSize::Large) {
            step = 1;
            top = 2;
        } else if (size == UiSize::Small || size == UiSize::XSmall) {
            step = 4;
            top = -4;
        }
        mark->Top(top)
            ->Right(-step * (float)len(txt))
            ->Pad(2)
            ->MinW(14)
            ->Radius(7)
            ->Child(TextEl(a, txt)->Font(10)->LineHeight(1.f));
    }
    root->Child(mark);
    return root;
}

} // namespace component
} // namespace gpui
