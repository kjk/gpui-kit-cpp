#include "base/resizable.h"
#include "base/theme.h"

namespace gpui {

Rgba ResizableHandleColor(const base_theme::Theme& theme, bool active) {
    if (active) {
        return theme.resizable.hasActiveHandle ? theme.resizable.activeHandle
                                               : theme.tokens.colors.ring;
    }
    return theme.resizable.hasHandle ? theme.resizable.handle
                                     : theme.tokens.colors.border;
}

template <typename T>
static void ResizableVecRemove(Vec<T>* values, int ix) {
    if (!values || ix < 0 || ix >= values->len) return;
    if (ix + 1 < values->len) {
        memmove(values->els + ix, values->els + ix + 1,
                (size_t)(values->len - ix - 1) * sizeof(T));
    }
    values->len--;
    values->els[values->len] = T{};
}

static float PanelMin(const float* mins, int ix) {
    return mins ? mins[ix] : kResizablePanelMinSize;
}

static float PanelMax(const float* maxs, int ix) {
    // Rust's range ends at Pixels::MAX when a panel names no ceiling.
    return maxs ? maxs[ix] : 1e9f;
}

bool ResizablePanelResize(float* sizes, const float* mins, const float* maxs,
                          int n, int ix, float size, float containerSize) {
    // The handle sits between ix and ix + 1, so the last panel has none.
    if (n <= 1 || ix < 0 || ix >= n - 1) {
        return false;
    }
    float moved = size - sizes[ix];
    if (moved == 0) {
        return false;
    }
    float lo = PanelMin(mins, ix);
    float hi = PanelMax(maxs, ix);
    float newSize = size < lo ? lo : (size > hi ? hi : size);
    int mainIx = ix;
    float old = sizes[ix];

    if (moved > 0) {
        // Growing: the panels after it give up what they can spare, nearest
        // first, each stopping at its own minimum.
        float changed = newSize - sizes[ix];
        sizes[ix] = newSize;
        int i = ix;
        while (changed > 0 && i < n - 1) {
            i++;
            float spare = sizes[i] - PanelMin(mins, i);
            if (spare < 0) {
                spare = 0;
            }
            float take = changed < spare ? changed : spare;
            sizes[i] -= take;
            changed -= take;
        }
    } else {
        // Shrinking. Rust measures what is left to give from the requested
        // size rather than the clamped one, so a request below the minimum
        // stops there and the remainder is not handed on.
        float changed = newSize - size;
        sizes[ix] = newSize;
        int i = ix;
        while (changed > 0 && i > 0) {
            i--;
            float spare = sizes[i] - PanelMin(mins, i);
            if (spare < 0) {
                spare = 0;
            }
            float take = changed < spare ? changed : spare;
            changed -= take;
            sizes[i] -= take;
        }
        sizes[mainIx + 1] += old - size - changed;
    }

    float total = 0;
    for (int i = 0; i < n; i++) {
        total += sizes[i];
    }
    if (total > containerSize) {
        float overflow = total - containerSize;
        float shrunk = sizes[mainIx] - overflow;
        sizes[mainIx] = shrunk < lo ? lo : shrunk;
    }
    return true;
}

void ResizableAdjustToContainer(float* sizes, int n, float containerSize) {
    if (containerSize <= 0) {
        return;
    }
    float total = 0;
    for (int i = 0; i < n; i++) {
        total += sizes[i];
    }
    if (total <= 0) {
        return;
    }
    for (int i = 0; i < n; i++) {
        sizes[i] = containerSize * (sizes[i] / total);
    }
}

// The name a resize drag goes by, which is `DragPanel` in Rust.
static const Str kResizeDrag = StrL("resizable-handle");

float ResizablePanelSize(const ResizableState* s, int ix, float declared) {
    if (!s || ix < 0 || ix >= s->sizes.len || s->sizes[ix] <= 0) {
        return declared;
    }
    return s->sizes[ix];
}

void ResizableState::OnHandleDown(ResizableState* self, Ctx* cx,
                                  const MouseDownEvent* ev, intptr_t ix) {
    if (ev->button != MouseButton::Left) {
        return;
    }
    self->dragging = (int)ix;
    Notify(cx);
}

void ResizableState::OnHandleDrag(ResizableState* self, Ctx* cx,
                                  const DragMoveEvent* ev) {
    int ix = self->dragging;
    if (ix < 0 || ix + 1 >= self->sizes.len) {
        return;
    }
    // A hidden panel is not on the boundary being dragged and must not be
    // given or taken space, so the arithmetic sees the panels that are drawn
    // and nothing else. Rust leaves the hidden slot in its array and lets its
    // number drift; the sizes are compacted here and written back instead.
    int cap = self->sizes.len;
    float* sizes = (float*)Alloc(cx->a, (int)sizeof(float) * cap);
    float* mins = (float*)Alloc(cx->a, (int)sizeof(float) * cap);
    float* maxs = (float*)Alloc(cx->a, (int)sizeof(float) * cap);
    int* back = (int*)Alloc(cx->a, (int)sizeof(int) * cap);
    int n = 0;
    int at = -1;
    for (int i = 0; i < self->sizes.len; i++) {
        if (i < self->shown.len && !self->shown[i]) {
            continue;
        }
        sizes[n] = self->sizes[i];
        mins[n] = self->mins[i];
        maxs[n] = self->maxs[i];
        back[n] = i;
        if (i == ix) {
            at = n;
        }
        n++;
    }
    if (at < 0 || at + 1 >= n) {
        return;
    }
    // Where the boundary now is, in the group's own coordinates: the size the
    // panels before it take up plus what the pointer has moved to.
    float before = 0;
    for (int i = 0; i < at; i++) {
        before += sizes[i];
    }
    bool horiz = AxisIsHorizontal(self->axis);
    float pt =
        horiz ? ev->event.x - self->bounds.x : ev->event.y - self->bounds.y;
    float want = pt - before;
    float container = horiz ? self->bounds.w : self->bounds.h;
    if (!ResizablePanelResize(sizes, mins, maxs, n, at, want, container)) {
        return;
    }
    for (int i = 0; i < n; i++) {
        self->sizes[back[i]] = sizes[i];
    }
    Notify(cx);
}

void ResizableState::OnHandleUp(ResizableState* self, Ctx* cx,
                                const MouseUpEvent*) {
    if (self->dragging < 0) {
        return;
    }
    self->dragging = -1;
    // The complete panel-size slice, once the boundary has settled.
    if (self->onResized.IsValid()) {
        ResizablePanelEvent ev = {self->sizes.els, self->sizes.len};
        ListenerCall(cx->app, cx->win, self->onResized, &ev);
    }
    Notify(cx);
}

bool ResizableState::ResizePanel(Ctx* cx, int ix, float size) {
    if (ix < 0 || ix >= sizes.len) {
        return false;
    }
    int handle = ix;
    float requested = size;
    if (ix == sizes.len - 1) {
        if (ix == 0) return false;
        handle = ix - 1;
        requested = sizes[handle] + sizes[ix] - size;
    }
    bool changed =
        ResizablePanelResize(sizes.els, mins.els, maxs.els, sizes.len, handle,
                             requested, ContainerSize());
    // Rust calls done_resizing for every valid panel, even when the requested
    // value was already current or the range clamps it back to that value.
    if (onResized.IsValid()) {
        ResizablePanelEvent ev = {sizes.els, sizes.len};
        ListenerCall(cx->app, cx->win, onResized, &ev);
    }
    Notify(cx);
    return changed;
}

bool ResizableState::InsertPanel(Ctx* cx, float size, int ix) {
    int n = sizes.len;
    if (ix < 0) ix = n;
    if (ix > n) ix = n;
    if (ix < 0) return false;
    float container = ContainerSize();
    if (container < 1.f) container = 1.f;
    float left = container > size ? container - size : 1.f;
    for (int i = 0; i < n; i++) sizes[i] = left * sizes[i] / container;
    VecInsertAt(sizes, ix, size);
    VecInsertAt(mins, ix, PANEL_MIN_SIZE);
    VecInsertAt(maxs, ix, 1e9f);
    VecInsertAt(grows, ix, false);
    VecInsertAt(shown, ix, true);
    VecInsertAt(laid, ix, Bounds{});
    Notify(cx);
    return true;
}

bool ResizableState::RemovePanel(Ctx* cx, int ix) {
    if (ix < 0 || ix >= sizes.len) return false;
    ResizableVecRemove(&sizes, ix);
    ResizableVecRemove(&mins, ix);
    ResizableVecRemove(&maxs, ix);
    if (ix < grows.len) ResizableVecRemove(&grows, ix);
    if (ix < shown.len) ResizableVecRemove(&shown, ix);
    if (ix < laid.len) ResizableVecRemove(&laid, ix);
    if (dragging == ix)
        dragging = -1;
    else if (dragging > ix)
        dragging--;
    ResizableAdjustToContainer(sizes.els, sizes.len, ContainerSize());
    Notify(cx);
    return true;
}

bool ResizableState::ResetPanel(Ctx* cx, int ix) {
    if (ix < 0 || ix >= sizes.len) return false;
    mins[ix] = PANEL_MIN_SIZE;
    maxs[ix] = 1e9f;
    if (ix < grows.len) grows[ix] = false;
    if (ix < shown.len) shown[ix] = true;
    if (ix < laid.len) laid[ix] = {};
    ResizableAdjustToContainer(sizes.els, sizes.len, ContainerSize());
    Notify(cx);
    return true;
}

void ResizableState::Clear() {
    VecClear(sizes);
    VecClear(mins);
    VecClear(maxs);
    VecClear(grows);
    VecClear(shown);
    VecClear(laid);
    dragging = -1;
    lastContainer = 0;
}

ResizablePanelGroup* ResizablePanelGroup::New(Ctx* cx, Str id,
                                              Entity<ResizableState> state,
                                              gpui::Axis axis) {
    Arena* a = cx->a;
    ResizablePanelGroup* r = ArenaNew<ResizablePanelGroup>(a);
    r->a = a;
    r->cx = cx;
    r->id = id;
    r->groupAxis = axis;
    BaseTheme theme = base_theme::Theme::Global(cx->app);
    r->handleColor = ResizableHandleColor(theme, false);
    r->handleDragColor = ResizableHandleColor(theme, true);
    // `self.state.unwrap_or(window.use_keyed_state(self.id, .., ResizableState
    // ::default()))`: a group only needs the caller to hold its state when the
    // caller means to drive it -- the programmatic story resizes panels from
    // buttons. Every other group is `h_resizable("id")` and nothing more, and
    // the sizes a drag leaves belong to the element that was dragged.
    r->state = state.IsValid() ? state
                               : ElementStateEntity<ResizableState>(
                                     cx, id, StrL("gpui::ResizableState"));
    if (ResizableState* s = r->state.Get(cx)) {
        s->axis = axis;
    }
    return r;
}

ResizablePanelGroup* ResizablePanelGroup::W(float v) {
    width = v;
    return this;
}
ResizablePanelGroup* ResizablePanelGroup::H(float v) {
    height = v;
    return this;
}
void ResizeHandleState::OnDown(ResizeHandleState* self, Ctx* cx,
                               const MouseDownEvent* ev) {
    // `if bounds.contains(&ev.position)`: the listener is the element's, so
    // being called is already the answer to that.
    self->active = true;
    if (self->nextDown.IsValid()) {
        ListenerCall(cx->app, cx->win, self->nextDown, ev);
    }
    Notify(cx);
}

void ResizeHandleState::OnUp(ResizeHandleState* self, Ctx* cx,
                             const MouseUpEvent* ev) {
    // Any release ends it, whether or not it landed on the handle.
    self->active = false;
    if (self->nextUp.IsValid()) {
        ListenerCall(cx->app, cx->win, self->nextUp, ev);
    }
    Notify(cx);
}

Entity<ResizeHandleState> ResizeHandleStateFor(Ctx* cx, Str name) {
    return ElementStateEntity<ResizeHandleState>(
        cx, name, StrL("gpui::ResizeHandleState"));
}

ResizeHandle* ResizeHandle::New(Ctx* cx, Str id, Axis axis) {
    ResizeHandle* out = ArenaNew<ResizeHandle>(cx->a);
    out->cx = cx;
    out->id = id;
    out->axis = axis;
    BaseTheme theme = base_theme::Theme::Global(cx->app);
    out->color = ResizableHandleColor(theme, false);
    out->activeColor = ResizableHandleColor(theme, true);
    return out;
}

ResizeHandle* ResizeHandle::Placement(Side value) {
    placement = value;
    hasPlacement = true;
    return this;
}

ResizeHandle* ResizeHandle::OnDrag(Listener listener) {
    onDrag = listener;
    return this;
}

ResizeHandle* ResizeHandle::WithAppearance(void* user,
                                           ResizeHandleRenderer renderer) {
    appearanceUser = user;
    appearance = renderer;
    return this;
}

ResizeHandle* ResizeHandle::Colors(Rgba rest, Rgba active) {
    color = rest;
    activeColor = active;
    return this;
}

El* ResizeHandle::IntoEl() {
    Entity<ResizeHandleState> state = ResizeHandleStateFor(cx, id);
    ResizeHandleState* stored = state.Get(cx);
    bool active = stored && stored->active;
    ResizeHandleContext context = {axis, active};
    El* line = appearance ? appearance(appearanceUser, &context, cx) : nullptr;
    if (!line) {
        line = Div(cx->a)->Bg(active ? activeColor : color);
        if (AxisIsHorizontal(axis))
            line->W(kResizeHandleSize)->H(kFill);
        else
            line->H(kResizeHandleSize)->W(kFill);
    }
    El* handle = Div(cx->a)
                     ->Absolute()
                     ->PathClick(id)
                     ->OnMouseDown(ListenTo(state, &ResizeHandleState::OnDown))
                     ->OnMouseUp(ListenTo(state, &ResizeHandleState::OnUp))
                     ->OnMouseUpOut(ListenTo(state, &ResizeHandleState::OnUp));
    if (onDrag.IsValid()) {
        handle->OnDrag(kResizeDrag, 0)->OnDragMove(onDrag);
    }
    if (AxisIsHorizontal(axis)) {
        handle->Cursor(CursorKind::ColResize)->Top(0)->H(kFill);
        if (hasPlacement && SideIsLeft(placement)) {
            // The left dock is the source's special one-sided hit band:
            // right(1), w(1), pl(4). Keep its line at the outer edge.
            handle->Right(1)
                ->W(kResizeHandleSize + kResizeHandlePadding)
                ->JustifyEnd();
        } else {
            handle->Left(-kResizeHandlePadding)
                ->W(kResizeHandleSize + kResizeHandlePadding * 2)
                ->JustifyCenter();
        }
    } else {
        handle->Cursor(CursorKind::RowResize)
            ->Left(0)
            ->Top(-kResizeHandlePadding)
            ->H(kResizeHandleSize + kResizeHandlePadding * 2)
            ->W(kFill)
            ->ItemsCenter();
    }
    return handle->Child(line);
}

ResizeHandle* resize_handle(Ctx* cx, Str id, Axis axis) {
    return ResizeHandle::New(cx, id, axis);
}

ResizablePanelGroup* ResizablePanelGroup::Size(float v) {
    if (AxisIsHorizontal(groupAxis))
        height = v;
    else
        width = v;
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::WithState(
    Entity<ResizableState> value) {
    state = value;
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::Axis(gpui::Axis value) {
    groupAxis = value;
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::HandleColors(Rgba rest,
                                                       Rgba dragging) {
    handleColor = rest;
    handleDragColor = dragging;
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::WithHandleAppearance(
    void* user, ResizeHandleRenderer renderer) {
    handleAppearanceUser = user;
    handleAppearance = renderer;
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::OnResize(Listener listener) {
    onResize = listener;
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::Panel(El* content, float size,
                                                float min, float max) {
    panels.Append(a, content);
    sizes.Append(a, size);
    mins.Append(a, min);
    maxs.Append(a, max);
    grows.Append(a, false);
    shown.Append(a, true);
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::Grow(El* content, float min) {
    Panel(content, 0, min, 0);
    return Flex();
}

ResizablePanelGroup* ResizablePanelGroup::Flex() {
    if (grows.len > 0) {
        grows[grows.len - 1] = true;
    }
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::Visible(bool v) {
    if (shown.len > 0) {
        shown[shown.len - 1] = v;
    }
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::Child(ResizablePanel* panel) {
    if (!panel) return this;
    Panel(panel->content, panel->size, panel->min, panel->max);
    grows[grows.len - 1] = panel->grow;
    shown[shown.len - 1] = panel->visible;
    return this;
}

ResizablePanelGroup* ResizablePanelGroup::Children(ResizablePanel** values,
                                                   int count) {
    panels.len = 0;
    sizes.len = 0;
    mins.len = 0;
    maxs.len = 0;
    grows.len = 0;
    shown.len = 0;
    for (int i = 0; values && i < count; i++) Child(values[i]);
    return this;
}

void ResizableState::OnSettled(ResizableState*, Ctx* cx, const void*) {
    Notify(cx);
}

static void MeasureResizableGroup(PaintCtx* paint, El* root, void* data) {
    Entity<ResizableState> entity = *(Entity<ResizableState>*)data;
    ResizableState* state = entity.Get(paint->app);
    if (!state) return;
    bool horizontal = AxisIsHorizontal(state->axis);
    float container = horizontal ? root->w : root->h;
    state->bounds = {root->x, root->y, root->w, root->h};
    if (container <= 0 || container == state->lastContainer) return;
    if (state->lastContainer <= 0) {
        El* panel = root->first;
        for (int i = 0; i < state->sizes.len && panel; i++) {
            if (!state->shown[i]) continue;
            state->sizes[i] = horizontal ? panel->w : panel->h;
            panel = panel->next;
        }
    } else {
        ResizableAdjustToContainer(state->sizes.els, state->sizes.len,
                                   container);
    }
    state->lastContainer = container;
    WindowPost(paint->window, ListenTo(entity, &ResizableState::OnSettled));
}

El* ResizablePanelGroup::IntoEl() {
    ResizableState* s = state.Get(cx);
    if (s) {
        s->axis = groupAxis;
        s->onResized = onResize;
    }
    bool horiz = !s || AxisIsHorizontal(s->axis);
    // The group's name, on the stack while its panels and handles are built.
    // Nesting one group inside another is the ordinary case here, and without
    // this both groups' `resizable-handle-0` would be one element state.
    IdScope scope(cx, id);
    El* root = Div(a)->Id(id)->W(width)->H(height);
    root->FlexRow();
    if (!horiz) {
        root->FlexCol();
    }
    if (!s) {
        for (El* panel : panels) {
            root->Child(panel);
        }
        return root;
    }

    // The declared sizes are the state's until a drag has moved one, and the
    // count is the caller's: a page that changes how many panels it has gets
    // what it declared rather than the old group's numbers.
    if (s->sizes.len != panels.len) {
        VecClear(s->sizes);
        VecClear(s->mins);
        VecClear(s->maxs);
        for (int i = 0; i < panels.len; i++) {
            // A declared initial size holds until the first measurement;
            // only an unsized panel starts with a flex-resolved size.
            VecAppend(s->sizes, sizes[i]);
            VecAppend(s->mins, mins[i]);
            // A declared 0 is Rust's `Pixels::MAX` — no ceiling — and the
            // arithmetic takes a number, not a flag.
            VecAppend(s->maxs, maxs[i] > 0 ? maxs[i] : 1e9f);
        }
        s->lastContainer = 0;
    } else {
        for (int i = 0; i < panels.len; i++) {
            s->mins[i] = mins[i];
            s->maxs[i] = maxs[i] > 0 ? maxs[i] : 1e9f;
        }
    }
    VecClear(s->grows);
    VecClear(s->shown);
    for (int i = 0; i < panels.len; i++) {
        VecAppend(s->grows, grows[i]);
        VecAppend(s->shown, shown[i]);
    }
    while (s->laid.len < panels.len) {
        VecAppend(s->laid, Bounds{});
    }

    // Layout is measured after the tree is built. Adopt its answer there and
    // post the settling notification after this draw, including caller state.
    auto* measuredState = ArenaNew<Entity<ResizableState>>(a);
    *measuredState = state;
    root->customUser = measuredState;
    root->customPaint = &MeasureResizableGroup;

    Listener down = ListenTo(state, &ResizableState::OnHandleDown, 0);
    Listener drag = ListenTo(state, &ResizableState::OnHandleDrag);
    Listener up = ListenTo(state, &ResizableState::OnHandleUp);
    for (int i = 0; i < panels.len; i++) {
        // `visible(false)` draws nothing at all — Rust's panel renders a bare
        // div, which takes no room and carries no handle.
        if (!shown[i]) {
            continue;
        }
        // No clip: the handle straddles the boundary, four DIPs either side,
        // which is where Rust puts it and what a clip would cut off.
        // `div().id(("resizable-panel", panel_ix))`: the panel names itself,
        // which is what the handle drawn inside it folds under.
        El* box = Div(a)
                      ->Id(StrDup(a, fmt("resizable-panel-%d", i)))
                      ->FlexCol()
                      ->Shrink0();
        // What the handle is measured against: the panel's own size along
        // the axis. Across it the handle fills the panel — the group's
        // measured box would do as well, but not on the frame that measures
        // it, and a handle with no height on the first frame cannot be
        // grabbed until something else happens to redraw the page.
        float boxW = horiz ? s->sizes[i] : kFill;
        float boxH = horiz ? kFill : s->sizes[i];
        if (s->sizes[i] <= 0) {
            // Rust's own declaration, for the frames before the layout has
            // answered: `flex().flex_grow_1().size_full()`, then `flex_none()`
            // where the caller cancelled the growth, then the size range, then
            // the declared size as the basis.
            box->SizeFull();
            if (grows[i]) {
                box->Grow(1)->Shrink(1);
            } else {
                box->FlexNone();
            }
            if (horiz) {
                box->MinW(mins[i])->MaxW(maxs[i] > 0 ? maxs[i] : 1e9f);
            } else {
                box->MinH(mins[i])->MaxH(maxs[i] > 0 ? maxs[i] : 1e9f);
            }
            if (sizes[i] > 0) {
                box->Basis(sizes[i]);
            }
            if (i < s->laid.len) {
                box->BoundsOut(&s->laid[i]);
            }
        } else if (horiz) {
            box->W(s->sizes[i])->H(kFill);
        } else {
            box->H(s->sizes[i])->W(kFill);
        }
        if (panels[i]) {
            box->Child(panels[i]);
        }
        // The handle sits over the boundary rather than taking room from it:
        // a hairline with four DIPs of grab either side, absolutely placed on
        // the panel's trailing edge. The last panel has no boundary after it,
        // and neither has the last one that is drawn — Rust draws the handle
        // from the panel *after* the boundary, so hiding a panel takes the
        // handle before it away.
        bool hasNext = false;
        for (int j = i + 1; j < panels.len; j++) {
            hasNext = hasNext || shown[j];
        }
        if (hasNext) {
            // Whether this handle is the one being dragged is the handle's
            // own state, kept where Rust keeps it: `with_element_state` under
            // the handle's name. The group's `dragging` is what the resize
            // arithmetic needs, which is a different question.
            Str hid = StrDup(a, fmt("resizable-handle-%d", i));
            Entity<ResizeHandleState> hs = ResizeHandleStateFor(cx, hid);
            ResizeHandleState* h = hs.Get(cx);
            bool active = h && h->active;
            if (h) {
                h->nextDown = ListenerArg(down, i);
                h->nextUp = up;
            }
            ResizeHandleContext handleContext = {s->axis, active};
            El* line = handleAppearance ? handleAppearance(handleAppearanceUser,
                                                           &handleContext, cx)
                                        : nullptr;
            bool builtInLine = line == nullptr;
            if (!line) {
                // `flex_none()`: Rust's handle is HANDLE_SIZE wide but padded
                // by HANDLE_PADDING, so its content area is zero and a
                // shrinkable child collapses with it. The handle here centres
                // the line with `justify` rather than padding, so the crush
                // never happened; the line still says it cannot shrink, so
                // that stays true whatever the handle's box becomes.
                line = Div(a)
                           ->FlexNone()
                           ->Bg(active ? handleDragColor : handleColor);
            }
            El* handle =
                Div(a)
                    ->Absolute()
                    // `resize_handle(("resizable-handle", ix), axis)`, drawn
                    // from inside the panel it follows.
                    ->PathClick(hid)
                    ->OnMouseDown(ListenTo(hs, &ResizeHandleState::OnDown))
                    ->OnDrag(kResizeDrag, i)
                    ->OnDragMove(drag)
                    ->OnMouseUp(ListenTo(hs, &ResizeHandleState::OnUp))
                    ->OnMouseUpOut(ListenTo(hs, &ResizeHandleState::OnUp));
            // Placed by its leading edge rather than its trailing one: the
            // panel's own size is what the boundary is, and an offset from
            // the near edge is the one an absolute box takes everywhere here.
            float span = horiz ? boxW : boxH;
            float at = span - kResizeHandlePadding;
            if (horiz) {
                handle->Cursor(CursorKind::ColResize)
                    ->Top(0)
                    ->Left(at)
                    ->W(kResizeHandleSize + kResizeHandlePadding * 2)
                    ->H(boxH)
                    ->JustifyCenter();
                if (builtInLine) line->W(kResizeHandleSize)->H(kFill);
            } else {
                handle->Cursor(CursorKind::RowResize)
                    ->Left(0)
                    ->Top(at)
                    ->H(kResizeHandleSize + kResizeHandlePadding * 2)
                    ->W(boxW)
                    ->ItemsCenter();
                if (builtInLine) line->H(kResizeHandleSize)->W(kFill);
            }
            handle->Child(line);
            box->Child(handle);
        }
        root->Child(box);
    }
    return root;
}

ResizablePanel* ResizablePanel::New(Ctx* cx) {
    ResizablePanel* out = ArenaNew<ResizablePanel>(cx->a);
    out->cx = cx;
    return out;
}

ResizablePanel* ResizablePanel::Child(El* value) {
    content = value;
    return this;
}

ResizablePanel* ResizablePanel::Size(float value) {
    size = value;
    return this;
}

ResizablePanel* ResizablePanel::SizeRange(float minValue, float maxValue) {
    min = minValue;
    max = maxValue;
    return this;
}

ResizablePanel* ResizablePanel::FlexNone() {
    grow = false;
    return this;
}

ResizablePanel* ResizablePanel::Visible(bool value) {
    visible = value;
    return this;
}

ResizablePanelGroup* h_resizable(Ctx* cx, Str id,
                                 Entity<ResizableState> state) {
    return ResizablePanelGroup::New(cx, id, state, Axis::Horizontal);
}

ResizablePanelGroup* v_resizable(Ctx* cx, Str id,
                                 Entity<ResizableState> state) {
    return ResizablePanelGroup::New(cx, id, state, Axis::Vertical);
}

ResizablePanel* resizable_panel(Ctx* cx) {
    return ResizablePanel::New(cx);
}
} // namespace gpui
