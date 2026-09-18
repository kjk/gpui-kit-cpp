#include "base/text_selection.h"
#include "base/auto_scroll.h"
#include "base/element_ext.h"
#include "base/global_state.h"
#include "gpui/paint.h"

namespace gpui {

struct TextSelectionParticipantState {
    Entity<TextSelectionParticipantState> self = {};
    Str fallbackCopyText = {};
    Str projectedCopyText = {};
    Vec<TextSelectionRun> runs;
    Vec<Bounds> textBounds;
    TextSelectionRegistration registration = {};
    Window* window = nullptr;
    TextSelectionSnapshot snapshot = {};
    bool hasSnapshot = false;
    bool localSelection = false;
    bool registered = false;
    bool hasProjectedCopyText = false;
    uint64_t registrationGeneration = 0;
    TextSelectionFocusFn focus = nullptr;
    void* focusUser = nullptr;
    TextSelectionClearFn clear = nullptr;
    void* clearUser = nullptr;
    TextSelectionCopyFn copy = nullptr;
    void* copyUser = nullptr;
    TextSelectionContentKeyFn resolveContentKey = nullptr;
    void* resolveContentKeyUser = nullptr;

    static void Refresh(TextSelectionParticipantState*, Ctx* cx,
                        const TextSelectionEvent* event) {
        if (event->kind == TextSelectionEventKind::SelectionChanged &&
            cx->win) {
            AppInvalidate(cx->win);
        }
    }

    ~TextSelectionParticipantState() {
        for (int i = 0; i < len(runs); i++) {
            if (runs[i].layout) TextLayoutRelease(runs[i].layout);
        }
        VecReset(runs);
        VecReset(textBounds);
        StrFree(fallbackCopyText);
        StrFree(projectedCopyText);
    }
};

static TextSelectionParticipantState* ParticipantState(
    const TextSelectionHandle& handle, const App* app) {
    return app ? handle.state.Get((App*)app) : nullptr;
}

static bool SamePoint(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

static bool SameEndpoint(const TextSelectionEndpoint& a,
                         const TextSelectionEndpoint& b) {
    return a.entity == b.entity && a.hasEntity == b.hasEntity &&
           SamePoint(a.point, b.point) && a.contentKey == b.contentKey &&
           a.hasContentKey == b.hasContentKey;
}

static bool SameSnapshot(const TextSelectionSnapshot& a,
                         const TextSelectionSnapshot& b) {
    return SameEndpoint(a.anchor, b.anchor) &&
           SameEndpoint(a.cursor, b.cursor) && a.selecting == b.selecting &&
           a.hasWindowPoints == b.hasWindowPoints &&
           (!a.hasWindowPoints ||
            (SamePoint(a.windowPoints.anchor, b.windowPoints.anchor) &&
             SamePoint(a.windowPoints.cursor, b.windowPoints.cursor))) &&
           a.coverage == b.coverage;
}

TextSelectionScopeId TextSelectionScopeId::New() {
    // The source uses a relaxed AtomicU64. Selection scopes are created and
    // consumed on the UI thread here, so the same monotonic lifetime needs no
    // cross-thread synchronization in this runtime.
    static uint64_t next = 1;
    TextSelectionScopeId out = {next};
    next++;
    if (next == 0) next = 1;
    return out;
}

TextSelectionEndpoint TextSelectionEndpoint::New(EntityId value, Point p) {
    TextSelectionEndpoint out;
    out.entity = value;
    out.point = p;
    out.hasEntity = value.IsValid();
    return out;
}

TextSelectionEndpoint TextSelectionEndpoint::At(Point p) {
    TextSelectionEndpoint out;
    out.point = p;
    return out;
}

TextSelectionEndpoint TextSelectionEndpoint::WithContentKey(
    TextSelectionContentKey value) const {
    TextSelectionEndpoint out = *this;
    out.contentKey = value;
    out.hasContentKey = true;
    return out;
}

TextSelectionSnapshot TextSelectionSnapshot::New(TextSelectionEndpoint a,
                                                 TextSelectionEndpoint c) {
    TextSelectionSnapshot out;
    out.anchor = a;
    out.cursor = c;
    return out;
}

TextSelectionSnapshot TextSelectionSnapshot::WithSelecting(bool value) const {
    TextSelectionSnapshot out = *this;
    out.selecting = value;
    return out;
}

TextSelectionSnapshot TextSelectionSnapshot::WithWindowPoints(
    TextSelectionWindowPoints value) const {
    TextSelectionSnapshot out = *this;
    out.windowPoints = value;
    out.hasWindowPoints = true;
    return out;
}

TextSelectionSnapshot TextSelectionSnapshot::WithCoverage(
    TextSelectionCoverage value) const {
    TextSelectionSnapshot out = *this;
    out.coverage = value;
    return out;
}

TextSelectionRegistration TextSelectionRegistration::New(Bounds hit,
                                                         Bounds value) {
    TextSelectionRegistration out;
    out.hitbox = hit;
    out.bounds = value;
    return out;
}

TextSelectionRegistration TextSelectionRegistration::WithScrollOffset(
    Point value) const {
    TextSelectionRegistration out = *this;
    out.scrollOffset = value;
    return out;
}

TextSelectionRegistration TextSelectionRegistration::WithScope(
    TextSelectionScopeId value) const {
    TextSelectionRegistration out = *this;
    out.scope = value;
    return out;
}

TextSelectionRegistration TextSelectionRegistration::WithDocumentOrder(
    uint64_t value) const {
    TextSelectionRegistration out = *this;
    out.documentOrder = value;
    return out;
}

TextSelectionRegistration TextSelectionRegistration::WithTextBounds(
    const Bounds* values, int count) const {
    TextSelectionRegistration out = *this;
    out.textBounds = values;
    out.textBoundsCount = count > 0 ? count : 0;
    return out;
}

TextSelectionRun TextSelectionRun::New(Str value, TextLayout* shaped,
                                       Bounds valueBounds) {
    TextSelectionRun out;
    out.text = value;
    out.layout = shaped;
    out.bounds = valueBounds;
    return out;
}

TextSelectionRun TextSelectionRun::WithDocumentOrder(uint64_t value) const {
    TextSelectionRun out = *this;
    out.documentOrder = value;
    return out;
}

static bool ParticipantContains(const TextSelectionParticipantState* state,
                                Point point) {
    return state && state->registered &&
           (state->registration.hitbox.Contains(point) ||
            state->registration.bounds.Contains(point));
}

static TextSelectionParticipantState* ParticipantAt(Window* window, Point point,
                                                    EntityId* outId) {
    WindowSelection* selection = window ? window->sel : nullptr;
    if (!selection || !window->app) return nullptr;
    TextSelectionParticipantState* hit = nullptr;
    EntityId hitId = {};
    float hitArea = 3.4e38f;
    TextSelectionParticipantState* predecessor = nullptr;
    EntityId predecessorId = {};
    TextSelectionParticipantState* first = nullptr;
    EntityId firstId = {};
    for (int i = 0; i < selection->participants.len; i++) {
        EntityId id = selection->participants[i];
        TextSelectionParticipantState* state =
            (TextSelectionParticipantState*)EntityGet(window->app, id);
        if (!state || state->window != window || !state->registered ||
            state->registration.scope != selection->activeScope) {
            continue;
        }
        Bounds bounds = state->registration.bounds;
        if (!first || bounds.y < first->registration.bounds.y ||
            (bounds.y == first->registration.bounds.y &&
             state->registration.documentOrder < first->registration
                                                     .documentOrder)) {
            first = state;
            firstId = id;
        }
        if (bounds.y <= point.y &&
            (!predecessor || bounds.y > predecessor->registration.bounds.y ||
             (bounds.y == predecessor->registration.bounds.y &&
              state->registration.documentOrder < predecessor->registration
                                                      .documentOrder))) {
            predecessor = state;
            predecessorId = id;
        }
        if (ParticipantContains(state, point)) {
            float area = bounds.w * bounds.h;
            if (!hit || area < hitArea ||
                (area == hitArea && state->registration.documentOrder <
                                        hit->registration.documentOrder)) {
                hit = state;
                hitId = id;
                hitArea = area;
            }
        }
    }
    TextSelectionParticipantState* result = hit           ? hit
                                            : predecessor ? predecessor
                                                          : first;
    if (result && outId) {
        *outId = hit ? hitId : predecessor ? predecessorId : firstId;
    }
    return result;
}

static TextSelectionEndpoint ParticipantEndpoint(
    const TextSelectionRegistration& registration,
    TextSelectionContentKeyFn resolver, void* resolverUser, EntityId id,
    Point windowPoint, const App* app) {
    Point content = {
        windowPoint.x - registration.bounds.x - registration.scrollOffset.x,
        windowPoint.y - registration.bounds.y - registration.scrollOffset.y,
    };
    TextSelectionEndpoint out = TextSelectionEndpoint::New(id, content);
    if (resolver) {
        TextSelectionContentKey key;
        if (resolver(resolverUser, content, app, &key)) {
            out = out.WithContentKey(key);
        }
    }
    return out;
}

static bool ComputeParticipantSnapshot(TextSelectionParticipantState* receiver,
                                       App* app, TextSelectionSnapshot* out) {
    if (!receiver || !receiver->window || !receiver->window->sel ||
        !receiver->registered || !out) {
        return false;
    }
    WindowSelection* selection = receiver->window->sel;
    if (!TextSelectionPublishes(&selection->gesture) || selection->anchor < 0 ||
        selection->cursor < 0 || selection->anchor == selection->cursor ||
        !selection->hasWindowPoints ||
        receiver->registration.scope != selection->activeScope) {
        return false;
    }
    EntityId anchorId = {}, cursorId = {};
    TextSelectionParticipantState* anchor =
        ParticipantAt(receiver->window, selection->anchorPoint, &anchorId);
    TextSelectionParticipantState* cursor =
        ParticipantAt(receiver->window, selection->cursorPoint, &cursorId);
    if (!anchor || !cursor) return false;
    uint64_t first = anchor->registration.documentOrder < cursor->registration
                                                              .documentOrder
                         ? anchor->registration.documentOrder
                         : cursor->registration.documentOrder;
    uint64_t last = anchor->registration.documentOrder > cursor->registration
                                                             .documentOrder
                        ? anchor->registration.documentOrder
                        : cursor->registration.documentOrder;
    uint64_t order = receiver->registration.documentOrder;
    if (order < first || order > last) return false;

    TextSelectionCoverage coverage = TextSelectionCoverage::Bounded;
    if (anchorId != cursorId) {
        if (receiver->self.id != anchorId && receiver->self.id != cursorId) {
            coverage = TextSelectionCoverage::Full;
        } else if ((receiver->self.id == anchorId) ==
                   (anchor->registration.documentOrder < cursor->registration
                                                             .documentOrder)) {
            coverage = TextSelectionCoverage::ToEnd;
        } else {
            coverage = TextSelectionCoverage::FromStart;
        }
    } else if (receiver->self.id != anchorId) {
        return false;
    }
    TextSelectionRegistration anchorRegistration = anchor->registration;
    TextSelectionRegistration cursorRegistration = cursor->registration;
    TextSelectionContentKeyFn anchorResolver = anchor->resolveContentKey;
    TextSelectionContentKeyFn cursorResolver = cursor->resolveContentKey;
    void* anchorResolverUser = anchor->resolveContentKeyUser;
    void* cursorResolverUser = cursor->resolveContentKeyUser;
    Point anchorPoint = selection->anchorPoint;
    Point cursorPoint = selection->cursorPoint;
    bool selecting = selection->gesture.selecting;
    TextSelectionEndpoint anchorEndpoint =
        ParticipantEndpoint(anchorRegistration, anchorResolver,
                            anchorResolverUser, anchorId, anchorPoint, app);
    TextSelectionEndpoint cursorEndpoint =
        ParticipantEndpoint(cursorRegistration, cursorResolver,
                            cursorResolverUser, cursorId, cursorPoint, app);
    *out = TextSelectionSnapshot::New(anchorEndpoint, cursorEndpoint)
               .WithSelecting(selecting)
               .WithWindowPoints(
                   TextSelectionWindowPoints::New(anchorPoint, cursorPoint))
               .WithCoverage(coverage);
    return true;
}

static void ParticipantSetSnapshot(TextSelectionParticipantState* state,
                                   App* app, bool hasSnapshot,
                                   const TextSelectionSnapshot& snapshot) {
    if (!state) return;
    if (state->hasSnapshot == hasSnapshot &&
        (!hasSnapshot || SameSnapshot(state->snapshot, snapshot))) {
        return;
    }
    state->hasSnapshot = hasSnapshot;
    if (hasSnapshot) state->snapshot = snapshot;
    StrFree(state->projectedCopyText);
    state->projectedCopyText = {};
    state->hasProjectedCopyText = false;
    TextSelectionEvent event;
    event.kind = TextSelectionEventKind::SelectionChanged;
    event.hasSnapshot = hasSnapshot;
    event.snapshot = snapshot;
    EntityEmit(app, state->window, state->self, &event);
}

static void WindowSelectionPublish(Window* window) {
    WindowSelection* selection = window ? window->sel : nullptr;
    if (!selection || !window->app || selection->publishing) return;
    selection->publishing = true;
    Vec<EntityId> participants;
    for (int i = 0; i < len(selection->participants); i++) {
        VecAppend(participants, selection->participants[i]);
    }
    for (int i = 0; i < len(participants); i++) {
        EntityId id = participants[i];
        TextSelectionParticipantState* state =
            (TextSelectionParticipantState*)EntityGet(window->app, id);
        if (!state || state->window != window) {
            for (int j = 0; j < len(selection->participants); j++) {
                if (selection->participants[j] != id) continue;
                for (int k = j; k < len(selection->participants) - 1; k++) {
                    selection->participants[k] = selection->participants[k + 1];
                }
                selection->participants.len--;
                break;
            }
            continue;
        }
        TextSelectionSnapshot snapshot;
        bool has = ComputeParticipantSnapshot(state, window->app, &snapshot);
        state = (TextSelectionParticipantState*)EntityGet(window->app, id);
        if (!state || state->window != window) continue;
        ParticipantSetSnapshot(state, window->app, has, snapshot);
    }
    VecReset(participants);
    selection->publishing = false;
}

static void ParticipantAutoScroll(Window* window, Point point, bool stopping) {
    if (!window || !window->sel || !window->app ||
        !window->sel->hasWindowPoints) {
        return;
    }
    EntityId id = {};
    TextSelectionParticipantState* state =
        ParticipantAt(window, window->sel->anchorPoint, &id);
    if (!state) return;
    // The visible region a drag scrolls is the nearest clipping viewport, not
    // the participant's own box: it stays put while the anchor text itself
    // scrolls out of view. Rust reads it off the registration's content mask;
    // the registration's hitbox is that here, since the runtime intersects
    // every hit rectangle with the content mask on the way down the tree.
    Bounds visible = state->registration.hitbox;
    if (visible.w <= 0 || visible.h <= 0) {
        visible = state->registration.bounds;
    }
    // A collapsed mask — a scrollable ancestor clipped away mid-drag — leaves
    // an empty clamp range, so the drag stops scrolling rather than running
    // at whatever the last delta was. `HIT_TEST_INSET` is Rust's, the margin
    // its synthesized wheel event has to stay inside.
    const float kHitTestInset = 1.f;
    bool collapsed =
        visible.w < kHitTestInset * 2.f || visible.h < kHitTestInset * 2.f;
    float delta = 0;
    bool has = !stopping && !collapsed &&
               AutoScrollComputeDelta(point.y, visible, &delta);
    TextSelectionEvent event;
    event.kind = TextSelectionEventKind::AutoScroll;
    event.autoScroll = delta;
    event.hasAutoScroll = has;
    EntityEmit(window->app, window, Entity<TextSelectionParticipantState>{id},
               &event);
}

TextSelectionHandle TextSelectionHandle::New(Str fallbackCopyText, App* app) {
    TextSelectionHandle out;
    out.state = EntityNewState<TextSelectionParticipantState>(app);
    if (TextSelectionParticipantState* state = out.state.Get(app)) {
        state->self = out.state;
        state->fallbackCopyText = StrDup(fallbackCopyText);
    }
    return out;
}

bool TextSelectionHandle::Snapshot(const App* app,
                                   TextSelectionSnapshot* out) const {
    TextSelectionParticipantState* participant = ParticipantState(*this, app);
    if (!participant || !participant->hasSnapshot) return false;
    if (out) *out = participant->snapshot;
    return true;
}

void TextSelectionHandle::SetFallbackCopyText(Str text, App* app) const {
    TextSelectionParticipantState* participant = ParticipantState(*this, app);
    if (!participant) return;
    StrFree(participant->fallbackCopyText);
    participant->fallbackCopyText = StrDup(text);
    StrFree(participant->projectedCopyText);
    participant->projectedCopyText = {};
    participant->hasProjectedCopyText = false;
}

void TextSelectionHandle::SetLocalSelection(bool active, App* app) const {
    TextSelectionParticipantState* participant = ParticipantState(*this, app);
    if (!participant || participant->localSelection == active) return;
    participant->localSelection = active;
    NotifyEntity(app, participant->self.id, participant->window);
}

bool TextSelectionHandle::HasLocalSelection(const App* app) const {
    TextSelectionParticipantState* participant = ParticipantState(*this, app);
    return participant && participant->localSelection;
}

void TextSelectionHandle::Register(TextSelectionRegistration value,
                                   Window* window, App* app) const {
    TextSelectionParticipantState* participant = ParticipantState(*this, app);
    if (!participant || !window) return;
    participant->window = window;
    participant->registered = true;
    participant->registration = value;
    VecReset(participant->textBounds);
    for (int i = 0; i < value.textBoundsCount; i++) {
        VecAppend(participant->textBounds, value.textBounds[i]);
    }
    participant->registration.textBounds = participant->textBounds.els;
    participant->registration.textBoundsCount = participant->textBounds.len;
    WindowSelection* selection = WindowSelectionOf(window);
    participant->registrationGeneration = selection->frameGeneration;
    bool found = false;
    for (int i = 0; i < selection->participants.len; i++) {
        if (selection->participants[i] == state.id) {
            found = true;
            break;
        }
    }
    if (!found) VecAppend(selection->participants, state.id);
    WindowSelectionPublish(window);
}

static bool PointInSelectionBand(Point position, float charWidth,
                                 Point selectionStart, Point selectionEnd,
                                 float lineHeight) {
    float top = std::min(selectionStart.y, selectionEnd.y);
    float bottom = std::max(selectionStart.y, selectionEnd.y);
    if (position.y + lineHeight <= top || position.y > bottom) return false;
    bool startInLine = selectionStart.y >= position.y &&
                       selectionStart.y < position.y + lineHeight;
    bool endInLine = selectionEnd.y >= position.y &&
                     selectionEnd.y < position.y + lineHeight;
    float x = position.x + charWidth * 0.5f;
    if (startInLine && endInLine) {
        return x >= std::min(selectionStart.x, selectionEnd.x) &&
               x <= std::max(selectionStart.x, selectionEnd.x);
    }
    Point topPoint =
        selectionStart.y < selectionEnd.y ? selectionStart : selectionEnd;
    Point bottomPoint =
        selectionStart.y < selectionEnd.y ? selectionEnd : selectionStart;
    if (topPoint.y >= position.y && topPoint.y < position.y + lineHeight) {
        return x >= topPoint.x;
    }
    if (bottomPoint.y >= position.y && bottomPoint
                                               .y < position.y + lineHeight) {
        return x <= bottomPoint.x;
    }
    return true;
}

static TextSelectionRange ProjectRun(const TextSelectionRun& run,
                                     const TextSelectionSnapshot& snapshot) {
    TextSelectionRange out;
    if (!run.layout || len(run.text) <= 0) return out;
    if (snapshot.coverage == TextSelectionCoverage::Full) {
        out.end = len(run.text);
        out.selected = true;
        return out;
    }
    if (!snapshot.hasWindowPoints) return out;
    int at = 0;
    while (at < len(run.text)) {
        uint32_t cp = 0;
        int bytes = Utf8At(run.text, at, &cp);
        (void)cp;
        Bounds rects[2];
        int n = TextLayoutRangeRects(run.layout, run.text, at, at + bytes,
                                     rects, 2);
        bool selected = false;
        for (int i = 0; i < n; i++) {
            Point position = {run.bounds.x + rects[i].x, run.bounds.y + rects[i]
                                                                            .y};
            float width = rects[i].w > 0 ? rects[i].w : rects[i].h * 0.5f;
            float height = rects[i].h > 0 ? rects[i].h : run.bounds.h;
            if (PointInSelectionBand(position, width,
                                     snapshot.windowPoints.anchor,
                                     snapshot.windowPoints.cursor, height)) {
                selected = true;
                break;
            }
        }
        if (selected) {
            if (!out.selected) out.start = at;
            out.end = at + bytes;
            out.selected = true;
        }
        at += bytes;
    }
    return out;
}

TextSelectionProjection TextSelectionHandle::UpdateRuns(
    const TextSelectionRun* values, int count, App* app) const {
    TextSelectionProjection out;
    TextSelectionParticipantState* participant = ParticipantState(*this, app);
    if (!participant) return out;
    for (int i = 0; i < participant->runs.len; i++) {
        if (participant->runs[i].layout) {
            TextLayoutRelease(participant->runs[i].layout);
        }
    }
    VecReset(participant->runs);
    for (int i = 0; i < count; i++) {
        VecAppend(participant->runs, values[i]);
        if (values[i].layout) TextLayoutAddRef(values[i].layout);
    }
    out.active = participant->hasSnapshot;
    for (int i = 0; i < count; i++) {
        VecAppend(out.ranges, participant->hasSnapshot
                                  ? ProjectRun(values[i], participant->snapshot)
                                  : TextSelectionRange{});
    }

    Vec<int> order;
    for (int i = 0; i < count; i++) {
        if (!out.ranges[i].selected) continue;
        int insert = len(order);
        while (insert > 0 && values[order[insert - 1]]
                                     .documentOrder > values[i].documentOrder) {
            insert--;
        }
        VecAppend(order, 0);
        for (int j = len(order) - 1; j > insert; j--) {
            order[j] = order[j - 1];
        }
        order[insert] = i;
    }
    StrBuilder selected;
    for (int i = 0; i < len(order); i++) {
        int ix = order[i];
        const TextSelectionRange& range = out.ranges[ix];
        selected.Append(
            Str(values[ix].text.s + range.start, range.end - range.start));
    }
    VecReset(order);
    StrFree(participant->projectedCopyText);
    participant->projectedCopyText = selected.TakeStr();
    participant->hasProjectedCopyText = true;
    return out;
}

Subscription TextSelectionHandle::RefreshWindowOnChange(App* app) const {
    return SubscribeTo(app, state, state,
                       &TextSelectionParticipantState::Refresh);
}

void TextSelectionHandle::FocusWith(TextSelectionFocusFn fn, void* user,
                                    App* app) const {
    if (TextSelectionParticipantState* participant =
            ParticipantState(*this, app)) {
        participant->focus = fn;
        participant->focusUser = user;
    }
}

void TextSelectionHandle::ClearWith(TextSelectionClearFn fn, void* user,
                                    App* app) const {
    if (TextSelectionParticipantState* participant =
            ParticipantState(*this, app)) {
        participant->clear = fn;
        participant->clearUser = user;
    }
}

void TextSelectionHandle::CopyWith(TextSelectionCopyFn fn, void* user,
                                   App* app) const {
    if (TextSelectionParticipantState* participant =
            ParticipantState(*this, app)) {
        participant->copy = fn;
        participant->copyUser = user;
    }
}

void TextSelectionHandle::ResolveContentKeyWith(TextSelectionContentKeyFn fn,
                                                void* user, App* app) const {
    if (TextSelectionParticipantState* participant =
            ParticipantState(*this, app)) {
        participant->resolveContentKey = fn;
        participant->resolveContentKeyUser = user;
    }
}

void TextSelectionBegin(TextSelectionGesture* g, bool insideText) {
    g->selecting = true;
    // A fresh gesture starts over: Rust assigns rather than ORs here, so the
    // previous drag's hit does not carry into this one.
    g->didHitText = insideText;
}

void TextSelectionExtend(TextSelectionGesture* g, bool insideText) {
    if (!g->selecting) {
        return;
    }
    // |=, never cleared: once any point has landed on text the selection
    // stands, even as the pointer wanders back into the margin.
    g->didHitText = g->didHitText || insideText;
}

void TextSelectionEnd(TextSelectionGesture* g) {
    g->selecting = false;
}

bool TextSelectionPublishes(const TextSelectionGesture* g) {
    return g->didHitText;
}

void TextSelectionClear(TextSelectionGesture* g) {
    g->selecting = false;
    g->didHitText = false;
}

El* TextSelection::New(Ctx* cx, Str id, int clickId) {
    Arena* a = cx->a;
    return UiRoot(a, id, clickId);
}

El* TextSelectionLayer::New(Ctx* cx) {
    return Div(cx->a)->Id(StrL("window-text-selection"))->W(0)->H(0);
}

El* TextSelectionScope(El* element, TextSelectionScopeId scope) {
    return element ? element->TrapId(scope.RuntimeScope()) : nullptr;
}

// ─── the window's selection ───────────────────────────────────────────────

WindowSelection* WindowSelectionOf(Window* win) {
    if (!win) {
        return nullptr;
    }
    if (!win->sel) {
        win->sel = new WindowSelection();
    }
    return win->sel;
}

void WindowSelectionFree(Window* win) {
    if (win && win->sel) {
        WindowSelectionClear(win);
        VecReset(win->sel->participants);
        delete win->sel;
        win->sel = nullptr;
    }
}

void WindowSelectionClear(Window* win) {
    WindowSelection* s = win ? win->sel : nullptr;
    if (!s || s->clearing) {
        return;
    }
    s->clearing = true;
    ParticipantAutoScroll(win, s->cursorPoint, true);
    TextSelectionClear(&s->gesture);
    s->anchor = -1;
    s->cursor = -1;
    s->scope = 0;
    s->hasWindowPoints = false;
    if (win->app) {
        Vec<EntityId> participants;
        for (int i = 0; i < len(s->participants); i++) {
            VecAppend(participants, s->participants[i]);
        }
        for (int i = 0; i < len(participants); i++) {
            TextSelectionParticipantState* participant =
                (TextSelectionParticipantState*)EntityGet(win->app,
                                                          participants[i]);
            if (!participant) continue;
            TextSelectionClearFn clear = participant->clear;
            void* clearUser = participant->clearUser;
            participant->hasSnapshot = false;
            participant->localSelection = false;
            StrFree(participant->projectedCopyText);
            participant->projectedCopyText = {};
            participant->hasProjectedCopyText = false;
            TextSelectionEvent cleared;
            cleared.kind = TextSelectionEventKind::Cleared;
            EntityEmit(win->app, win, participant->self, &cleared);
            TextSelectionEvent selectionChanged;
            selectionChanged.kind = TextSelectionEventKind::SelectionChanged;
            EntityEmit(win->app, win, participant->self, &selectionChanged);
            if (clear) clear(clearUser, win->app);
        }
        VecReset(participants);
    }
    s->clearing = false;
}

bool WindowSelectionHas(const Window* win) {
    const WindowSelection* s = win ? win->sel : nullptr;
    if (!s || s->anchor < 0 || s->cursor < 0 || s->anchor == s->cursor) {
        return false;
    }
    return TextSelectionPublishes(&s->gesture);
}

void WindowSelectionPress(Window* win, float x, float y, int clickCount,
                          bool extend) {
    if (win && BaseIsTextSelectionSuppressed(win->app)) {
        WindowSelectionClear(win);
        return;
    }
    WindowSelection* s = WindowSelectionOf(win);
    if (!s) {
        return;
    }
    PaintCtx* ctx = &win->paint;
    // A shift-click keeps the anchor and moves the cursor — Rust's
    // `begin_in_window(.., extend)` — and stays in the scope it began in.
    if (extend && s->anchor >= 0) {
        int off = TextHitOffsetIn(ctx, x, y, true, s->scope, nullptr);
        if (off >= 0) {
            s->cursor = off;
            s->cursorPoint = {x, y};
            s->hasWindowPoints = true;
            TextSelectionExtend(
                &s->gesture,
                TextHitOffsetIn(ctx, x, y, false, s->scope, nullptr) >= 0);
        }
        WindowSelectionPublish(win);
        return;
    }
    // Two clicks take the word under the pointer, three the whole run —
    // points_for_multi_click. A multi-click lands on a glyph by definition,
    // so the gesture has hit text; the drag does not extend it, because the
    // selection is already the unit that was asked for.
    int scope = 0;
    int a = 0;
    int b = 0;
    int activeScope =
        s->activeScope.raw != 0 ? s->activeScope.RuntimeScope() : -1;
    if (TextMultiClickRangeIn(ctx, x, y, clickCount, activeScope, &a, &b,
                              &scope)) {
        s->scope = scope;
        s->anchor = a;
        s->cursor = b;
        if (s->activeScope.raw == 0) {
            s->activeScope = TextSelectionScopeId::FromRaw((uint64_t)scope);
        }
        s->anchorPoint = {x - 0.5f, y};
        s->cursorPoint = {x + 0.5f, y};
        s->hasWindowPoints = true;
        TextSelectionBegin(&s->gesture, true);
        TextSelectionEnd(&s->gesture);
        WindowSelectionPublish(win);
        return;
    }
    // A press in the margin still begins a gesture, so a drag from beside a
    // paragraph into it selects; whether it was on a glyph is what decides
    // if anything is ever published.
    int anchor = TextHitOffsetIn(ctx, x, y, true, activeScope, &scope);
    if (anchor >= 0) {
        s->scope = scope;
        if (s->activeScope.raw == 0) {
            s->activeScope = TextSelectionScopeId::FromRaw((uint64_t)scope);
        }
        s->anchor = anchor;
        s->cursor = anchor;
        s->anchorPoint = {x, y};
        s->cursorPoint = {x, y};
        s->hasWindowPoints = true;
        TextSelectionBegin(&s->gesture, TextHitOffsetIn(ctx, x, y, false, scope,
                                                        nullptr) >= 0);
        EntityId participantId = {};
        TextSelectionParticipantState* participant =
            ParticipantAt(win, s->anchorPoint, &participantId);
        if (participant && participant->focus) {
            participant->focus(participant->focusUser, win, win->app);
        }
        WindowSelectionPublish(win);
        return;
    }
    WindowSelectionClear(win);
}

bool WindowSelectionLongPressStart(Window* win, float x, float y) {
    WindowSelectionPress(win, x, y, 2, false);
    WindowSelection* selection = win ? win->sel : nullptr;
    if (!selection || !WindowSelectionHas(win)) return false;
    selection->gesture.selecting = true;
    selection->gesture.didHitText = true;
    return true;
}

void WindowSelectionDrag(Window* win, float x, float y) {
    WindowSelection* s = win ? win->sel : nullptr;
    if (!s || !s->gesture.selecting) {
        return;
    }
    PaintCtx* ctx = &win->paint;
    // did_hit_text is the strict hit — whether *this* point is on a glyph —
    // while the offset the selection runs to is the nearest one either way,
    // so a drag through the margin keeps going along the line.
    TextSelectionExtend(
        &s->gesture, TextHitOffsetIn(ctx, x, y, false, s->scope, nullptr) >= 0);
    int off = TextHitOffsetIn(ctx, x, y, true, s->scope, nullptr);
    if (off >= 0) {
        s->cursor = off;
        s->cursorPoint = {x, y};
        s->hasWindowPoints = true;
    }
    ParticipantAutoScroll(win, {x, y}, false);
    WindowSelectionPublish(win);
}

void WindowSelectionRelease(Window* win) {
    WindowSelection* s = win ? win->sel : nullptr;
    if (s) {
        ParticipantAutoScroll(win, s->cursorPoint, true);
        TextSelectionEnd(&s->gesture);
        WindowSelectionPublish(win);
    }
}

int WindowSelectionTextAs(Window* win, char* out, int cap,
                          SelectionFormat fmt) {
    if (!WindowSelectionHas(win)) {
        if (out && cap > 0) {
            out[0] = 0;
        }
        return 0;
    }
    WindowSelection* s = win->sel;
    return CopyTextHitsIn(&win->paint, s->anchor, s->cursor, s->scope, out, cap,
                          fmt);
}

int WindowSelectionText(Window* win, char* out, int cap) {
    return WindowSelectionTextAs(win, out, cap, WindowSelectionFormat(win));
}

bool WindowSelectionHasEntity(const Window* win, EntityId owner) {
    if (!owner.IsValid() || !WindowSelectionHas(win)) return false;
    const WindowSelection* selection = win->sel;
    int a = selection->anchor;
    int b = selection->cursor;
    if (a > b) {
        int swap = a;
        a = b;
        b = swap;
    }
    const PaintCtx* paint = &win->paint;
    for (int i = 0; i < paint->texts.len; i++) {
        const TextHit& hit = paint->texts[i];
        if (hit.owner != owner || hit.scope != selection->scope) continue;
        int n = hit.atom ? 1 : len(hit.text);
        if (a < hit.docOff + n && b > hit.docOff) return true;
    }
    return false;
}

int WindowSelectionTextForEntity(Window* win, EntityId owner, char* out,
                                 int cap, SelectionFormat fmt) {
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!WindowSelectionHasEntity(win, owner)) return 0;
    WindowSelection* selection = win->sel;
    return CopyTextHitsInEntity(&win->paint, selection->anchor,
                                selection->cursor, selection->scope, owner, out,
                                cap, fmt);
}

void WindowSelectionSelectAll(Window* win, EntityId owner) {
    if (!win || !owner.IsValid()) return;
    WindowSelection* selection = WindowSelectionOf(win);
    if (!selection) return;
    int scope = 0;
    bool found = false;
    int first = 0;
    int last = 0;
    Bounds firstBounds = {};
    Bounds lastBounds = {};
    int activeScope = selection->activeScope.raw != 0
                          ? selection->activeScope.RuntimeScope()
                          : -1;
    for (int i = 0; i < win->paint.texts.len; i++) {
        const TextHit& hit = win->paint.texts[i];
        if (hit.owner != owner ||
            (activeScope >= 0 && hit.scope != activeScope)) {
            continue;
        }
        if (!found) {
            found = true;
            scope = hit.scope;
            first = hit.docOff;
            firstBounds = hit.bounds;
        }
        if (hit.scope != scope) continue;
        last = hit.docOff + (hit.atom ? 1 : len(hit.text));
        lastBounds = hit.bounds;
    }
    if (!found || first == last) return;
    selection->scope = scope;
    if (selection->activeScope.raw == 0) {
        selection->activeScope = TextSelectionScopeId::FromRaw((uint64_t)scope);
    }
    selection->anchor = first;
    selection->cursor = last;
    selection->anchorPoint = {firstBounds.x, firstBounds.y};
    selection->cursorPoint = {lastBounds.x + lastBounds.w,
                              lastBounds.y + lastBounds.h};
    selection->hasWindowPoints = true;
    TextSelectionBegin(&selection->gesture, true);
    TextSelectionEnd(&selection->gesture);
    WindowSelectionPublish(win);
}

static bool HasNonWhitespace(Str text) {
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return true;
    }
    return false;
}

struct ParticipantCopyItem {
    uint64_t documentOrder = 0;
    TextSelectionCopyFn copy = nullptr;
    void* copyUser = nullptr;
    Str text = {};
};

static int CopyParticipantItem(const ParticipantCopyItem& item, App* app,
                               char* out, int cap) {
    if (!out || cap <= 0) return 0;
    if (item.copy) {
        int n = item.copy(item.copyUser, app, out, cap);
        if (n < 0) return 0;
        return n < cap ? n : cap - 1;
    }
    int n = len(item.text) < cap - 1 ? len(item.text) : cap - 1;
    if (n > 0) memcpy(out, item.text.s, (size_t)n);
    out[n] = 0;
    return n;
}

int TextSelection::SelectedText(Window* window, App* app, char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    WindowSelection* selection = window ? window->sel : nullptr;
    if (!selection || !app) return 0;
    Vec<ParticipantCopyItem> active;
    for (int i = 0; i < selection->participants.len; i++) {
        TextSelectionParticipantState* participant =
            (TextSelectionParticipantState*)EntityGet(
                app, selection->participants[i]);
        if (!participant ||
            (!participant->hasSnapshot && !participant->localSelection)) {
            continue;
        }
        ParticipantCopyItem item;
        item.documentOrder = participant->registration.documentOrder;
        item.copy = participant->copy;
        item.copyUser = participant->copyUser;
        item.text = StrDup(participant->hasProjectedCopyText
                               ? participant->projectedCopyText
                               : participant->fallbackCopyText);
        int insert = len(active);
        while (insert > 0 && active[insert - 1]
                                     .documentOrder > item.documentOrder) {
            insert--;
        }
        VecAppend(active, {});
        for (int j = len(active) - 1; j > insert; j--) {
            active[j] = active[j - 1];
        }
        active[insert] = item;
    }
    int written = 0;
    bool emitted = false;
    for (int i = 0; i < len(active) && written < cap - 1; i++) {
        const ParticipantCopyItem& item = active[i];
        if (!item.copy && !HasNonWhitespace(item.text)) {
            continue;
        }
        int before = written;
        if (emitted && written < cap - 1) out[written++] = '\n';
        int textStart = written;
        int n = CopyParticipantItem(item, app, out + written, cap - written);
        if (n <= 0 || !HasNonWhitespace(Str(out + textStart, n))) {
            written = before;
            continue;
        }
        written += n;
        emitted = true;
    }
    for (int i = 0; i < len(active); i++) StrFree(active[i].text);
    VecReset(active);
    out[written] = 0;
    if (written > 0) return written;
    return WindowSelectionText(window, out, cap);
}

bool TextSelection::HasSelection(Window* window, const App* app) {
    if (WindowSelectionHas(window)) return true;
    const WindowSelection* selection = window ? window->sel : nullptr;
    if (!selection || !app) return false;
    for (int i = 0; i < selection->participants.len; i++) {
        TextSelectionParticipantState* participant =
            (TextSelectionParticipantState*)EntityGet(
                (App*)app, selection->participants[i]);
        if (participant && participant->localSelection) return true;
    }
    return false;
}

void TextSelection::Clear(Window* window, App*) {
    WindowSelectionClear(window);
}

void TextSelection::ClearForWindow(Window* window, App* app) {
    Clear(window, app);
}

void TextSelection::End(Window* window, App*) {
    WindowSelectionRelease(window);
}

void TextSelection::ActivateScope(TextSelectionScopeId scope, Window* window,
                                  App*) {
    WindowSelection* selection = WindowSelectionOf(window);
    if (!selection || selection->activeScope == scope) return;
    WindowSelectionClear(window);
    selection->activeScope = scope;
    selection->scope = scope.RuntimeScope();
    WindowSelectionPublish(window);
}

void WindowSelectionSetFormat(Window* win, SelectionFormat fmt) {
    if (WindowSelection* s = WindowSelectionOf(win)) {
        s->format = fmt;
    }
}

SelectionFormat WindowSelectionFormat(Window* win) {
    WindowSelection* s = win ? win->sel : nullptr;
    return s ? s->format : SelectionFormat::Plain;
}

bool WindowSelectionCopy(Window* win) {
    if (!TextSelection::HasSelection(win, win ? win->app : nullptr)) {
        return false;
    }
    // One frame's worth of selectable text; a document longer than this
    // copies its first 64 KB rather than nothing.
    const int kCap = 64 * 1024;
    char* buf = (char*)Alloc(nullptr, kCap);
    if (!buf) {
        return false;
    }
    int n = TextSelection::SelectedText(win, win->app, buf, kCap);
    if (n > 0) {
        ClipboardSetText(win, Str(buf, n));
    }
    Free(nullptr, buf);
    return n > 0;
}

void WindowSelectionApply(Window* win) {
    if (!win) {
        return;
    }
    WindowSelection* s = win->sel;
    // did_hit_text gates the whole thing: a gesture that never touched a
    // glyph shows nothing, however far it dragged.
    bool publishes = s && TextSelectionPublishes(&s->gesture);
    win->paint.selA = publishes ? s->anchor : -1;
    win->paint.selB = publishes ? s->cursor : -1;
    win->paint.selScope = publishes ? s->scope : -1;
}

void WindowSelectionFinishFrame(Window* win) {
    WindowSelection* selection = win ? win->sel : nullptr;
    if (!selection || !win->app) return;
    // Rust collects stale ids before clearing their entities. Event and
    // cleanup callbacks can re-enter selection code, so do not walk the live
    // participant vector while invoking them.
    Vec<EntityId> participants;
    for (int i = 0; i < len(selection->participants); i++) {
        VecAppend(participants, selection->participants[i]);
    }
    for (int i = 0; i < len(participants); i++) {
        EntityId id = participants[i];
        TextSelectionParticipantState* participant =
            (TextSelectionParticipantState*)EntityGet(win->app, id);
        if (participant && participant->window == win &&
            participant->registrationGeneration == selection->frameGeneration) {
            continue;
        }
        for (int j = 0; j < len(selection->participants); j++) {
            if (selection->participants[j] != id) continue;
            for (int k = j; k < len(selection->participants) - 1; k++) {
                selection->participants[k] = selection->participants[k + 1];
            }
            selection->participants.len--;
            break;
        }
        if (participant && participant->window == win) {
            TextSelectionClearFn clear = participant->clear;
            void* clearUser = participant->clearUser;
            participant->registered = false;
            participant->window = nullptr;
            participant->hasSnapshot = false;
            participant->localSelection = false;
            StrFree(participant->projectedCopyText);
            participant->projectedCopyText = {};
            participant->hasProjectedCopyText = false;
            TextSelectionEvent cleared;
            cleared.kind = TextSelectionEventKind::Cleared;
            EntityEmit(win->app, win, Entity<TextSelectionParticipantState>{id},
                       &cleared);
            TextSelectionEvent changed;
            changed.kind = TextSelectionEventKind::SelectionChanged;
            EntityEmit(win->app, win, Entity<TextSelectionParticipantState>{id},
                       &changed);
            if (clear) clear(clearUser, win->app);
        }
    }
    VecReset(participants);
    selection->frameGeneration++;
    WindowSelectionPublish(win);
}
} // namespace gpui
