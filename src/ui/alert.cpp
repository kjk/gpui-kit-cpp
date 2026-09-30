#include "ui/alert.h"
#include "ui/text.h"

namespace gpui {

namespace component {

Alert* Alert::New(Ctx* cx, Str id, Str message) {
    Arena* a = cx->a;
    Alert* al = ArenaNew<Alert>(a);
    al->a = a;
    al->cx = cx;
    al->id = id;
    al->message = message;
    return al;
}

Alert* Alert::Info(Ctx* cx, Str id, Str message) {
    Alert* al = New(cx, id, message);
    al->variant = AlertVariant::Info;
    al->icon = IconName::Info;
    return al;
}
Alert* Alert::Success(Ctx* cx, Str id, Str message) {
    Alert* al = New(cx, id, message);
    al->variant = AlertVariant::Success;
    al->icon = IconName::CircleCheck;
    return al;
}
Alert* Alert::Warning(Ctx* cx, Str id, Str message) {
    Alert* al = New(cx, id, message);
    al->variant = AlertVariant::Warning;
    al->icon = IconName::TriangleAlert;
    return al;
}
Alert* Alert::Error(Ctx* cx, Str id, Str message) {
    Alert* al = New(cx, id, message);
    al->variant = AlertVariant::Error;
    al->icon = IconName::CircleX;
    return al;
}

Alert* Alert::Title(Str s) {
    title = s;
    return this;
}
Alert* Alert::Icon(IconName n) {
    icon = n;
    return this;
}
Alert* Alert::Markdown(bool v) {
    markdown = v;
    return this;
}
Alert* Alert::Content(El* e) {
    content = e;
    return this;
}
Alert* Alert::Banner() {
    banner = true;
    return this;
}
Alert* Alert::Visible(bool v) {
    visible = v;
    return this;
}
Alert* Alert::OnClose(Listener fn) {
    onClose = fn;
    return this;
}
Alert* Alert::WithSize(UiSize s) {
    size = s;
    return this;
}

El* Alert::IntoEl() {
    if (!visible) {
        return Div(a);
    }
    const Theme& th = ThemeNow(cx->app);
    Rgba fg = th.foreground, bg = th.background, bd = th.border;
    switch (variant) {
        case AlertVariant::Info:
            fg = th.info;
            bg = RgbaOpacity(th.info, 0.04f);
            bd = RgbaOpacity(th.info, 0.3f);
            break;
        case AlertVariant::Success:
            fg = th.success;
            bg = RgbaOpacity(th.success, 0.04f);
            bd = RgbaOpacity(th.success, 0.3f);
            break;
        case AlertVariant::Warning:
            fg = th.warning;
            bg = RgbaOpacity(th.warning, 0.04f);
            bd = RgbaOpacity(th.warning, 0.3f);
            break;
        case AlertVariant::Error:
            fg = th.danger;
            bg = RgbaOpacity(th.danger, 0.04f);
            bd = RgbaOpacity(th.danger, 0.3f);
            break;
        default:
            break;
    }
    // (radius, padding_x, padding_y, gap) by size — the banner shares them,
    // it only drops the rounding.
    float radius = th.radius, padX = 16, padY = 10, gap = 12;
    switch (size) {
        case UiSize::XSmall:
            padX = 12;
            padY = 6;
            gap = 6;
            break;
        case UiSize::Small:
            padX = 12;
            padY = 8;
            gap = 6;
            break;
        case UiSize::Large:
            radius = th.radiusLg;
            padX = 20;
            padY = 14;
            gap = 12;
            break;
        default:
            break;
    }
    // h_flex, so a banner keeps its parts centred; a card aligns them to the
    // top instead. text_sm whatever the size is.
    El* row = Div(a)
                  ->Role(AccessibilityRole::Alert)
                  ->FlexRow()
                  ->W(kFill)
                  ->Gap(gap)
                  ->PadX(padX)
                  ->PadY(padY)
                  ->JustifyBetween()
                  ->Font(14)
                  ->Fg(fg)
                  ->Bg(bg)
                  ->Border(1, bd);
    if (banner) {
        row->ItemsCenter();
    } else {
        row->ItemsStart()->Radius(radius);
    }
    El* inner = Div(a)->FlexRow()->Flex1()->Gap(gap)->ClipX();
    if (banner) {
        inner->ItemsCenter();
    }
    // mt_5 on the card's icon, so it sits on the first line rather than above
    // it — a pad here, the box has nothing to paint. The banner is centred.
    // Icon::new with no size of its own: Icon::render takes the inherited
    // text size, which is the row's text_sm.
    El* iconBox = Div(a)->Shrink0()->Child(IconEl(a, icon)->Fg(fg));
    if (!banner) {
        iconBox->PadT(5);
    }
    inner->Child(iconBox);
    // The title and the message stack with nothing between them: the div
    // Rust puts them in is a block, so its gap_3 never applies.
    El* col = Div(a)->FlexCol()->Flex1()->ClipX();
    // A banner never shows its title.
    if (title.s && !banner) {
        col->Child(TextEl(a, title)->Semibold()->Truncate());
    }
    if (content) {
        col->Child(content);
    } else if (markdown) {
        // TextViewStyle::default().paragraph_gap(rems(0.2)).
        col->Child(
            TextView::New(cx, message)->Font(14)->ParagraphGap(3.2f)->IntoEl());
    } else {
        col->Child(TextEl(a, message)->Wrap());
    }
    inner->Child(col);
    row->Child(inner);
    if (onClose.IsValid()) {
        // p_0p5 + rounded, icon at max(size, Medium) in the alert's color.
        float closeIcon =
            UiIconPx(size < UiSize::Medium ? UiSize(UiSize::Medium) : size);
        El* x = Div(a)
                    ->Pad(2)
                    ->Radius(th.radius)
                    ->ItemsCenter()
                    ->JustifyCenter()
                    ->Shrink0()
                    ->HoverBg(RgbaOpacity(bg, 0.8f))
                    ->Child(IconEl(a, IconName::X, closeIcon)->Fg(fg));
        BindClick(x, StrL("alert-close"), onClose);
        row->Child(x);
    }
    return row;
}

} // namespace component
} // namespace gpui
