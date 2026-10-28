#include "ui/message_scroller.h"
#include "ui/button.h"
#include "ui/virtual_list.h"
#include "base/scroll_bounce.h"
#include "base/virtual_list.h"

namespace gpui {

namespace component {

MessageScrollerState::~MessageScrollerState() {
    VecReset(heights);
    VecReset(needsMeasure);
}

void MessageScrollerState::Init(MessageScrollerState* self, int itemCount) {
    if (!self) {
        return;
    }
    VecClear(self->heights);
    VecClear(self->needsMeasure);
    for (int i = 0; i < itemCount; i++) {
        VecAppend(self->heights, kMessageScrollerEstimatedRowHeight);
        VecAppend(self->needsMeasure, (uint8_t)1);
    }
    self->handle = VirtualListScrollHandle{};
    self->handle.itemsCount = itemCount;
    self->followTail = true;
    // Tail following anchors past the last row until a layout walks back to
    // the row the viewport actually starts on. Same anchor scroll_to_end sets.
    VirtualListScrollToBottomDeferred(&self->handle);
}

int MessageScrollerState::ItemCount() const {
    return heights.len;
}

bool MessageScrollerState::IsFollowingTail() const {
    return followTail;
}

ListOffset MessageScrollerState::LogicalScrollTop() const {
    ListOffset top;
    // A deferred tail scroll has not been applied yet: GPUI reports
    // item_count until layout replaces that anchor with a real row.
    // ScrollToItem's request is the row itself, before the pixel offset moves.
    if (handle.pending) {
        if (followTail) {
            top.itemIx = heights.len;
            return top;
        }
        int ix = handle.pendingIx + handle.pendingOffset;
        if (ix < 0) {
            ix = 0;
        }
        if (ix > heights.len) {
            ix = heights.len;
        }
        top.itemIx = ix;
        return top;
    }
    VirtualListLogicalFromPixel(heights.els, heights.len, 0, handle.offset,
                                &top.itemIx, &top.offsetInItem);
    return top;
}

bool MessageScrollerState::IsScrolledUp() const {
    float maxOffset = handle.contentSize - handle.viewport;
    if (maxOffset <= 0) {
        return false;
    }
    if (followTail) {
        return false;
    }
    // is_scrolled_to_end, with the half-pixel a laid-out offset can carry.
    return handle.offset < maxOffset - 0.5f;
}

bool MessageScrollerState::ValidRange(int start, int end) const {
    return start >= 0 && start <= end && end <= heights.len;
}

void MessageScrollerState::Reset(Ctx* cx, int itemCount) {
    Init(this, itemCount);
    if (cx) {
        Notify(cx);
    }
}

bool MessageScrollerState::Splice(Ctx* cx, int start, int end, int count) {
    if (!ValidRange(start, end) || count < 0) {
        return false;
    }
    if (!followTail && start == 0 && handle.offset > 0) {
        float removed = 0;
        for (int i = start; i < end; i++) removed += heights[i];
        handle.offset += count * kMessageScrollerEstimatedRowHeight - removed;
        if (handle.offset < 0) handle.offset = 0;
    }
    if (end > start) {
        VecRemoveAtN(heights, start, end - start);
        VecRemoveAtN(needsMeasure, start, end - start);
    }
    if (count > 0) {
        float* rows = VecInsertSpace(heights, start, count);
        uint8_t* flags = VecInsertSpace(needsMeasure, start, count);
        for (int i = 0; i < count; i++) {
            if (rows) {
                rows[i] = kMessageScrollerEstimatedRowHeight;
            }
            if (flags) flags[i] = 1;
        }
    }
    handle.itemsCount = heights.len;

    // The default row wrapper pads every row except the last, so a row whose
    // "last" status may have flipped carries a stale measured height.
    // Remeasure the new last row and the survivor next to the splice.
    int last = heights.len - 1;
    if (last >= 0) {
        heights[last] = kMessageScrollerEstimatedRowHeight;
        needsMeasure[last] = 1;
        int neighbor = start - 1;
        if (neighbor >= 0 && neighbor != last) {
            heights[neighbor] = kMessageScrollerEstimatedRowHeight;
            needsMeasure[neighbor] = 1;
        }
    }
    // A surviving former first row may have lost its list inset, or a
    // surviving row may have become the first after a removal. `count` is
    // the first row after a prefix insert.
    if (start == 0) {
        int marks[2] = {0, count};
        for (int m = 0; m < 2; m++) {
            int index = marks[m];
            if (index >= 0 && index < heights.len && index != last) {
                heights[index] = kMessageScrollerEstimatedRowHeight;
                needsMeasure[index] = 1;
            }
        }
    }
    if (cx) {
        Notify(cx);
    }
    return true;
}

bool MessageScrollerState::Append(Ctx* cx, int count) {
    int itemCount = heights.len;
    return Splice(cx, itemCount, itemCount, count);
}

bool MessageScrollerState::Prepend(Ctx* cx, int count) {
    return Splice(cx, 0, 0, count);
}

void MessageScrollerState::Remeasure(Ctx* cx) {
    for (int i = 0; i < heights.len; i++) {
        heights[i] = kMessageScrollerEstimatedRowHeight;
        needsMeasure[i] = 1;
    }
    if (cx) {
        Notify(cx);
    }
}

bool MessageScrollerState::RemeasureItems(Ctx* cx, int start, int end) {
    if (!ValidRange(start, end)) {
        return false;
    }
    for (int i = start; i < end; i++) {
        heights[i] = kMessageScrollerEstimatedRowHeight;
        needsMeasure[i] = 1;
    }
    if (cx) {
        Notify(cx);
    }
    return true;
}

bool MessageScrollerState::ScrollToItem(Ctx* cx, int index) {
    if (index < 0 || index >= heights.len) {
        return false;
    }
    // ListState::scroll_to pins the row to the top. The virtual list's
    // deferred request only reveals it, and a remeasure then leaves the
    // offset on the estimate. The pixel anchor is the row's origin now;
    // layout's anchor adjustment keeps that row at the top once its
    // neighbors are measured. Pending stays clear so that adjustment runs.
    followTail = false;
    handle.pending = false;
    float origin = VirtualListItemOrigin(heights.els, heights.len, index);
    float most = handle.contentSize - handle.viewport;
    if (handle.contentSize > 0 && handle.viewport > 0 && origin > most) {
        origin = most > 0.f ? most : 0.f;
    }
    if (origin < 0) {
        origin = 0;
    }
    handle.offset = origin;
    if (cx) {
        Notify(cx);
    }
    return true;
}

void MessageScrollerState::ScrollToEnd(Ctx* cx) {
    followTail = true;
    VirtualListScrollToBottomDeferred(&handle);
    if (cx) {
        Notify(cx);
    }
}

void MessageScrollerState::OnScroll(MessageScrollerState* self, Ctx* cx,
                                    const ScrollEvent* ev) {
    if (!self || !ev) {
        return;
    }
    // A wheel or scrollbar move replaces a scroll that layout has not
    // applied yet, the way ListState::scroll_by replaces logical_scroll_top.
    self->handle.pending = false;
    self->handle.offset = ev->offsetY;
    // Scrolling away from the live edge releases tail following; arriving
    // back at it resumes, which is what FollowMode::Tail does.
    float maxOffset = self->handle.contentSize - self->handle.viewport;
    self->followTail = maxOffset <= 0 || ev->offsetY >= maxOffset - 0.5f;
    Notify(cx);
}

void MessageScrollerState::OnJumpToLatest(MessageScrollerState* self, Ctx* cx,
                                          const ClickEvent*) {
    if (self) {
        self->ScrollToEnd(cx);
    }
}

MessageScroller* MessageScroller::New(Ctx* cx, Str id,
                                      Entity<MessageScrollerState> state,
                                      MessageScrollerRowFn renderer,
                                      void* user) {
    Arena* a = cx->a;
    MessageScroller* s = ArenaNew<MessageScroller>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    s->state = state;
    s->renderer = renderer;
    s->user = user;
    return s;
}

MessageScroller* MessageScroller::H(float px) {
    h = px;
    return this;
}
MessageScroller* MessageScroller::Scrollbar(bool value) {
    scrollbar = value;
    return this;
}
MessageScroller* MessageScroller::JumpButton(bool value) {
    jumpButton = value;
    return this;
}
MessageScroller* MessageScroller::WithJumpButtonLabel(Str label) {
    jumpButtonLabel = label;
    return this;
}
MessageScroller* MessageScroller::WithContentStyle(const Style& s,
                                                   uint32_t fields) {
    StyleApplyFields(&contentStyle, s, fields);
    contentStyleSet |= fields;
    return this;
}
MessageScroller* MessageScroller::WithListStyle(const Style& s,
                                                uint32_t fields) {
    StyleApplyFields(&listStyle, s, fields);
    listStyleSet |= fields;
    return this;
}
MessageScroller* MessageScroller::WithRowStyle(const Style& s,
                                               uint32_t fields) {
    StyleApplyFields(&rowStyle, s, fields);
    rowStyleSet |= fields;
    return this;
}
MessageScroller* MessageScroller::WithJumpButtonStyle(const Style& s,
                                                      uint32_t fields) {
    StyleApplyFields(&jumpButtonStyle, s, fields);
    jumpButtonStyleSet |= fields;
    return this;
}
MessageScroller* MessageScroller::WithJumpButtonRenderer(
    MessageScrollerButtonFn fn) {
    jumpButtonRenderer = fn;
    return this;
}
MessageScroller* MessageScroller::WithJumpButtonTransition(float ms) {
    jumpButtonTransitionMs = ms;
    return this;
}
MessageScroller* MessageScroller::WithBottomFade(Rgba color) {
    bottomFade = color;
    hasBottomFade = true;
    return this;
}
MessageScroller* MessageScroller::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

// What one row wrapper needs, carried through the virtual list's user
// pointer the way every other row builder here carries its environment.
struct MessageScrollerRowCtx {
    MessageScroller* scroller = nullptr;
    int count = 0;
    float insetL = 0;
    float insetR = 0;
    float listTop = 0;
    float listBottom = 0;
    // StyleFieldPad was set, so the vertical insets replace pt_2 / pb_2
    // even when a side is zero. Horizontal insets override px_3 the same way.
    bool hasListPad = false;
};

static El* MessageScrollerRow(void* user, Ctx* cx, int index) {
    MessageScrollerRowCtx* rc = (MessageScrollerRowCtx*)user;
    Arena* a = cx->a;
    // The inner wrapper is the row. List padding stays on the outer box so
    // with_row_style cannot replace it, and so it is part of the measured
    // item rather than padding on the list, which the scrollbar range omits.
    El* inner = Div(a)->W(kFill)->MinW(0)->PadX(Rems(cx, 0.75f));
    if (rc->hasListPad) {
        // Rust's .pl(left) overrides .px_3 rather than adding to it.
        inner->PadL(rc->insetL);
        inner->PadR(rc->insetR);
    }
    // Spacing between rows only, like a CSS gap.
    if (index + 1 < rc->count) {
        inner->PadB(Rems(cx, 2.f));
    }
    if (rc->scroller->rowStyleSet) {
        inner->Refine(rc->scroller->rowStyle, rc->scroller->rowStyleSet);
    }
    if (rc->scroller->renderer) {
        inner->Child(rc->scroller->renderer(rc->scroller->user, cx, index));
    }
    El* row = Div(a)->W(kFill)->Child(inner);
    bool first = index == 0;
    bool last = index + 1 == rc->count;
    if (first) {
        float top = rc->hasListPad ? rc->listTop : Rems(cx, 0.5f);
        row->PadT(top);
    }
    if (last) {
        float bottom = rc->hasListPad ? rc->listBottom : Rems(cx, 0.5f);
        row->PadB(bottom);
    }
    return row;
}

El* MessageScroller::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    MessageScrollerState* st = state.Get(cx->app);
    if (!st) {
        return Div(a);
    }

    int count = st->heights.len;
    // GPUI's `list` lays rows out at the full list width and offsets them
    // only by vertical padding, so the horizontal component of the list
    // style is carried by every row wrapper instead.
    //
    // The vertical padding rides on the rows as well. A list measures its
    // scroll extent from its items alone, so a list that carries padding of
    // its own reports a scroll range that stops short of its end.
    float insetL = 0, insetR = 0, listTop = 0, listBottom = 0;
    bool hasListPad = (listStyleSet & StyleFieldPad) != 0;
    if (hasListPad) {
        insetL = listStyle.pad.left;
        insetR = listStyle.pad.right;
        listTop = listStyle.pad.top;
        listBottom = listStyle.pad.bottom;
    }

    // Following the tail is a standing request to sit at the newest row, so
    // it is re-armed every frame the content may have grown under it.
    if (st->followTail) {
        VirtualListScrollToBottomDeferred(&st->handle);
    }

    MessageScrollerRowCtx* rc = ArenaNew<MessageScrollerRowCtx>(a);
    rc->scroller = this;
    rc->count = count;
    rc->insetL = insetL;
    rc->insetR = insetR;
    rc->listTop = listTop;
    rc->listBottom = listBottom;
    rc->hasListPad = hasListPad;

    El* list = VirtualList::New(cx, count)
                   ->Id(id)
                   ->Sizes(st->heights.els)
                   ->MeasureRows(st->needsMeasure.els)
                   ->Handle(&st->handle)
                   ->Axis(ScrollAxis::Vertical)
                   ->Scroll(HashClickId(id),
                            ListenTo(state, &MessageScrollerState::OnScroll))
                   ->Row(&MessageScrollerRow, rc)
                   ->IntoEl();
    // The bounce owns the vertical wheel, including at the ends, so the
    // mask that used to chain there is gone. The scrollbar stays on the list.
    if (!scrollbar) {
        list->HideScrollbarY();
    }
    StrBuilder bounceName;
    bounceName.Append(id);
    bounceName.Append(StrL("-bounce"));
    list = ScrollBounce::New(cx, StrDup(a, Str(bounceName.els, bounceName.len)),
                             VirtualListScrollbar(&st->handle), list)
               ->OnScroll(ListenTo(state, &MessageScrollerState::OnScroll))
               ->IntoEl();
    // The list used to be the viewport's only child and filled it. The
    // bounce is that child now, so it has to take the same box.
    list->W(kFill)->H(kFill)->MinH(0)->MinW(0);
    if (listStyleSet) {
        list->Refine(listStyle, listStyleSet & ~(uint32_t)StyleFieldPad);
    }

    bool scrolledUp = st->IsScrolledUp();
    bool showJumpButton = jumpButton && scrolledUp;
    float jumpVisibility = 0;
    if (jumpButton) {
        jumpVisibility = MotionValue(
            cx, MotionName(cx, StrL("jump-button-visibility")),
            showJumpButton ? 1.f : 0.f, MotionNew(jumpButtonTransitionMs));
    }
    // At the live edge nothing is clipped below, so a visible fade would
    // suggest more content than there is.
    float fadeVisibility = 0;
    if (hasBottomFade) {
        fadeVisibility =
            MotionValue(cx, MotionName(cx, StrL("bottom-fade-visibility")),
                        scrolledUp ? 1.f : 0.f,
                        MotionNew(kMessageScrollerBottomFadeTransitionMs));
    }

    // Announce appended rows as a log region, like shadcn's `role="log"`
    // transcript content.
    El* viewport = Div(a)
                       ->Role(AccessibilityRole::Log)
                       ->W(kFill)
                       ->H(kFill)
                       ->MinH(0)
                       ->MinW(0)
                       ->Child(list);
    // The fade sits above the rows but below the scrollbar and the jump
    // button, so neither control is washed out by it.
    if (hasBottomFade && fadeVisibility > 0) {
        viewport->Child(
            Div(a)
                ->Absolute()
                ->Left(0)
                ->Right(0)
                ->Bottom(0)
                ->H(Rems(cx, 3.f))
                ->Opacity(fadeVisibility)
                ->Bg(BackgroundLinear(
                    180.f, ColorStopAt(RgbaOpacity(bottomFade, 0.f), 0.f),
                    ColorStopAt(bottomFade, 1.f))));
    }
    if (contentStyleSet) {
        viewport->Refine(contentStyle, contentStyleSet);
    }

    // `size_full` unless the caller fixed the height; the viewport and the
    // list fill it, and the list virtualizes against what layout gave it.
    El* root = Div(a)
                   ->PathId(id)
                   ->W(kFill)
                   ->H(h > 0 ? h : kFill)
                   ->MinH(0)
                   ->ClipX()
                   ->ClipY()
                   ->Child(viewport);
    if (jumpButton && jumpVisibility > 0) {
        // No explicit width or height: Button sizes an icon-only button as a
        // square on its own, and a renderer that adds a label or another
        // semantic size must be able to change the layout.
        Button* button = Button::New(cx, StrL("jump-to-latest"))
                             ->Secondary()
                             ->Icon(IconName::ArrowDown)
                             ->Tooltip(jumpButtonLabel)
                             ->Rounded(th.radiusFull)
                             ->OnClick(ListenTo(
                                 state, &MessageScrollerState::OnJumpToLatest));
        if (jumpButtonRenderer) {
            jumpButtonRenderer(button);
        }
        if (!showJumpButton) {
            button->Disabled(true);
        }
        El* el = button->IntoEl()
                     ->Border(1, th.border)
                     ->Bg(th.tokens.background)
                     ->Fg(th.foreground);
        if (jumpButtonStyleSet) {
            el->Refine(jumpButtonStyle, jumpButtonStyleSet);
        }
        root->Child(Div(a)
                        ->Absolute()
                        ->Left(0)
                        ->Right(0)
                        // bottom(rems(0.5 + visibility * 0.5))
                        ->Bottom(Rems(cx, 0.5f + jumpVisibility * 0.5f))
                        ->Flex()
                        ->JustifyCenter()
                        ->Opacity(jumpVisibility)
                        ->Child(el));
    }
    if (styleSet) {
        root->Refine(style, styleSet);
    }
    return root;
}

} // namespace component
} // namespace gpui
