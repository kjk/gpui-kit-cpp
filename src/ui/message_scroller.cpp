#include "ui/message_scroller.h"
#include "ui/button.h"
#include "ui/virtual_list.h"

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
}

int MessageScrollerState::ItemCount() const {
    return heights.len;
}

bool MessageScrollerState::IsFollowingTail() const {
    return followTail;
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
    followTail = false;
    VirtualListScrollToItemDeferred(&handle, index, ScrollStrategy::Top);
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
    float padTop = 0;
    float padBottom = 0;
};

static El* MessageScrollerRow(void* user, Ctx* cx, int index) {
    MessageScrollerRowCtx* rc = (MessageScrollerRowCtx*)user;
    Arena* a = cx->a;
    El* row = Div(a)->W(kFill)->MinW(0)->PadX(12);
    if (rc->insetL > 0) {
        // Rust's .pl(left) overrides .px_3 rather than adding to it.
        row->PadL(rc->insetL);
    }
    if (rc->insetR > 0) {
        row->PadR(rc->insetR);
    }
    // Spacing between rows only, like a CSS gap: the list's own bottom
    // padding owns the gap after the last row.
    if (index + 1 < rc->count) {
        row->PadB(32);
    }
    if (index == 0 && rc->padTop > 0) {
        row->PadT(rc->padTop);
    }
    if (index + 1 == rc->count && rc->padBottom > 0) {
        row->PadB(rc->padBottom);
    }
    if (rc->scroller->rowStyleSet) {
        row->Refine(rc->scroller->rowStyle, rc->scroller->rowStyleSet);
    }
    if (rc->scroller->renderer) {
        row->Child(rc->scroller->renderer(rc->scroller->user, cx, index));
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
    float viewH = h > 0 ? h : 192.f;
    // GPUI's `list` lays rows out at the full list width and offsets them
    // only by vertical padding, so the horizontal component of the list
    // style is carried by every row wrapper instead.
    float insetL = 0, insetR = 0, padTop = 8, padBottom = 8;
    if (listStyleSet & StyleFieldPad) {
        insetL = listStyle.pad.left;
        insetR = listStyle.pad.right;
        // py_2 plus whatever the caller added.
        padTop = 8 + listStyle.pad.top;
        padBottom = 8 + listStyle.pad.bottom;
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
    rc->padTop = padTop;
    rc->padBottom = padBottom;

    El* list = VirtualList::New(cx, count)
                   ->Id(id)
                   ->Sizes(st->heights.els)
                   ->MeasureRows(st->needsMeasure.els)
                   ->ViewH(viewH)
                   ->Handle(&st->handle)
                   ->Axis(ScrollAxis::Vertical)
                   ->Scroll(HashClickId(id),
                            ListenTo(state, &MessageScrollerState::OnScroll))
                   ->Row(&MessageScrollerRow, rc)
                   ->IntoEl();
    // Keep vertical wheel scrolling from leaking into an ancestor scroller:
    // the mask consumes vertical-dominant wheel events while the list can
    // move and chains to the ancestor only at the edges.
    list->ScrollMask(Axis::Vertical);
    if (!scrollbar) {
        list->HideScrollbarY();
    }
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
                       ->H(viewH)
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
                // h(rems(3.)) at the 16px root.
                ->H(48)
                ->Opacity(fadeVisibility)
                ->Bg(BackgroundLinear(
                    180.f, ColorStopAt(RgbaOpacity(bottomFade, 0.f), 0.f),
                    ColorStopAt(bottomFade, 1.f))));
    }
    if (contentStyleSet) {
        viewport->Refine(contentStyle, contentStyleSet);
    }

    El* root = Div(a)->PathId(id)->W(kFill)->H(viewH)->ClipX()->ClipY()->Child(
        viewport);
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
                        ->Bottom(8 + jumpVisibility * 8)
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
