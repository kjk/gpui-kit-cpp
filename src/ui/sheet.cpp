#include "ui/sheet.h"
#include "base/actions.h"
#include "base/motion.h"
#include "ui/button.h"
#include "ui/title_bar.h"

namespace gpui {

namespace component {

// sheet.rs: the surface takes 0.15 s to slide in.
static const float kSheetMotionMs = 150.f;
static_assert(kSheetDefaultMarginTop == kTitleBarHeight);

Sheet* Sheet::New(Ctx* cx) {
    Arena* a = cx->a;
    Sheet* s = ArenaNew<Sheet>(a);
    s->a = a;
    s->cx = cx;
    return s;
}
Sheet* Sheet::Title(Str s) {
    title = s;
    titleEl = nullptr;
    return this;
}
Sheet* Sheet::Title(El* e) {
    titleEl = e;
    title = {};
    return this;
}
Sheet* Sheet::Placement(SheetPlacement p) {
    placement = p;
    return this;
}
Sheet* Sheet::Size(float px) {
    size = px;
    return this;
}
Sheet* Sheet::Resizable(bool v) {
    resizable = v;
    return this;
}
Sheet* Sheet::Overlay(bool v) {
    overlay = v;
    return this;
}
Sheet* Sheet::OverlayClosable(bool v) {
    overlayClosable = v;
    return this;
}
Sheet* Sheet::Open(bool v) {
    open = v;
    return this;
}
Sheet* Sheet::Body(El* e) {
    body = e;
    return this;
}
Sheet* Sheet::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}
Sheet* Sheet::Footer(El* e) {
    footer = e;
    return this;
}
Sheet* Sheet::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
Sheet* Sheet::Scroll(int id, float y, Listener fn) {
    scrollId = id;
    scrollY = y;
    onScroll = fn;
    return this;
}
Sheet* Sheet::OnClose(Listener fn) {
    onClose = fn;
    return this;
}

El* Sheet::IntoEl(WinSize win) {
    if (!open) {
        return Div(a);
    }
    const Theme& th = ThemeNow(cx->app);
    // The frame's content area, not just its shadow padding: a sheet sits
    // flush against the inside of the window border and starts below the
    // title bar, rather than hanging into the shadow on one side and leaving
    // a border's gap on the other.
    Edges windowPadding = WindowContentInsets(cx->win);
    float viewW = win.dipW - windowPadding.left - windowPadding.right;
    float viewH = win.dipH - windowPadding.top - windowPadding.bottom;
    bool horizontal =
        placement == SheetPlacement::Left || placement == SheetPlacement::Right;
    // The surface carries no padding of its own: the title bar, the body and
    // the footer each have theirs, which is what puts the body's scrollbar
    // against the edge and rules the footer off across the whole width.
    // theme.sheet.margin_top: a sheet that hangs from the top starts under
    // the title bar rather than over it. One rising from the bottom does not.
    float marginTop =
        placement == SheetPlacement::Bottom ? 0.f : th.sheet.marginTop;
    El* surface = Div(a)
                      ->Absolute()
                      ->FlexCol()
                      ->Bg(th.tokens.background)
                      // gpui's occlude(): an empty part of the surface still
                      // stands above the dismiss capture behind it.
                      ->StopMouseDown();
    StyleApplyFields(&surface->style, style, styleSet);
    // These are applied after refine_style in Rust and therefore win over a
    // caller's width/height and edge placement refinements.
    surface->W(horizontal ? size : viewW)
        ->H(horizontal ? viewH - marginTop : size);
    // sheet.rs's "slide": 0.15 s from a hundred pixels off its own edge to
    // where it belongs. GPUI's default easing is linear, and the offset is
    // whichever way the sheet came in from.
    float delta = MotionAppear(cx, MotionId(StrL("sheet")), kSheetMotionMs);
    float off = -100.f + delta * 100.f;
    switch (placement) {
        case SheetPlacement::Left:
            surface->Top(marginTop)->Left(off)->BorderR(1, th.border);
            break;
        case SheetPlacement::Top:
            surface->Top(marginTop + off)->Left(0)->BorderB(1, th.border);
            break;
        case SheetPlacement::Bottom:
            surface->Top(viewH - size - off)->Left(0)->BorderT(1, th.border);
            break;
        default:
            surface->Top(marginTop)
                ->Left(viewW - size - off)
                ->BorderL(1, th.border);
            break;
    }
    El* head = Div(a)
                   ->FlexRow()
                   ->W(kFill)
                   ->Shrink0()
                   ->PadL(Rems(cx, 1.f))
                   ->PadR(Rems(cx, 0.75f))
                   ->PadY(Rems(cx, 0.5f))
                   ->ItemsCenter()
                   ->JustifyBetween();
    head->Child(
        titleEl ? titleEl
                : TextEl(a, title)->Font(16)->Semibold()->Fg(th.foreground));
    head->Child(Button::New(cx, StrL("sheet-close"))
                    ->Ghost()
                    ->WithSize(UiSize::Small)
                    ->Icon(IconName::X)
                    ->OnClick(onClose)
                    ->IntoEl());
    surface->Child(head);
    if (body || children.len > 0) {
        float bodyPadL = (styleSet & StyleFieldPad) ? style.pad.left : 16.f;
        float bodyPadR = (styleSet & StyleFieldPad) ? style.pad.right : 16.f;
        El* bodyColumn = Div(a)->FlexCol()->W(kFill)->H(kFill);
        if (body) {
            bodyColumn->Child(body);
        }
        for (El* child : children) {
            bodyColumn->Child(child);
        }
        El* pane = Div(a)
                       ->FlexCol()
                       ->W(kFill)
                       ->Flex1()
                       ->MinH(0)
                       ->ClipY()
                       ->PadL(bodyPadL)
                       ->PadR(bodyPadR)
                       ->Child(bodyColumn);
        if (scrollId) {
            pane->ScrollY(scrollY)->ScrollId(scrollId)->OnScroll(onScroll);
        }
        surface->Child(pane);
    }
    if (footer) {
        surface->Child(Div(a)
                           ->FlexRow()
                           ->W(kFill)
                           ->Shrink0()
                           ->PadX(Rems(cx, 1.f))
                           ->PadY(Rems(cx, 0.75f))
                           ->ItemsCenter()
                           ->JustifyBetween()
                           ->Child(footer));
    }
    El* backdrop = nullptr;
    if (overlay) {
        backdrop = Div(a)->Absolute()->Top(0)->Left(0)->W(viewW)->H(viewH)->Bg(
            th.overlay);
    }
    return gpui::Sheet::New(cx)
        ->Overlay(backdrop)
        ->OverlayInteractive(overlay)
        ->OverlayClosable(overlay && overlayClosable)
        ->Surface(surface)
        ->OnClose(onClose)
        ->IntoEl()
        ->Top(windowPadding.top)
        ->Left(windowPadding.left)
        ->W(viewW)
        ->H(viewH);
}

} // namespace component
} // namespace gpui
