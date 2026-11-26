#include "base/text_selection.h"
#include "base/auto_scroll.h"
#include "base/element_ext.h"
#include "base/global_state.h"
#include "base/text.h"
#include "gpui/paint.h"
#include "gpui/platform.h"

#include <utility>

namespace gpui {

static bool AwaitingSelectionReflow(const WindowSelection* selection) {
    const TextSelectionReflow& saved = selection->pendingReflow;
    return saved.owner.IsValid() && saved.scope == selection->scope &&
           saved.offsets[0] == selection->anchor &&
           saved.offsets[1] == selection->cursor;
}

static void ClearCopyProjection(TextSelectionCopyProjection* copy) {
    StrFree(copy->plain);
    StrFree(copy->source);
    *copy = {};
}

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
    // Set when Register stored a new snapshot without emitting. The next
    // WindowSelectionPublish delivers that SelectionChanged. Rust's
    // draw_changed map, kept on the participant because this window has no
    // separate changed-id table.
    bool drawChanged = false;
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
    for (int i = 0; i < len(selection->participants); i++) {
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
    if (AwaitingSelectionReflow(selection) ||
        !TextSelectionPublishes(&selection->gesture) || selection->anchor < 0 ||
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

static bool ParticipantSnapshotSame(const TextSelectionParticipantState* state,
                                    bool hasSnapshot,
                                    const TextSelectionSnapshot& snapshot) {
    return state->hasSnapshot == hasSnapshot &&
           (!hasSnapshot || SameSnapshot(state->snapshot, snapshot));
}

static void ParticipantStoreSnapshot(TextSelectionParticipantState* state,
                                     bool hasSnapshot,
                                     const TextSelectionSnapshot& snapshot) {
    state->hasSnapshot = hasSnapshot;
    if (hasSnapshot) state->snapshot = snapshot;
    StrFree(state->projectedCopyText);
    state->projectedCopyText = {};
    state->hasProjectedCopyText = false;
}

static void ParticipantEmitChanged(TextSelectionParticipantState* state,
                                   App* app,
                                   const TextSelectionSnapshot& snapshot) {
    state->drawChanged = false;
    TextSelectionEvent event;
    event.kind = TextSelectionEventKind::SelectionChanged;
    event.hasSnapshot = state->hasSnapshot;
    event.snapshot = snapshot;
    EntityEmit(app, state->window, state->self, &event);
}

static void ParticipantSetSnapshot(TextSelectionParticipantState* state,
                                   App* app, bool hasSnapshot,
                                   const TextSelectionSnapshot& snapshot) {
    if (!state) return;
    bool same = ParticipantSnapshotSame(state, hasSnapshot, snapshot);
    // A registration stores its snapshot before the event. The next publish
    // still notifies, including when the stored value already matches.
    if (same && !state->drawChanged) return;
    if (!same) ParticipantStoreSnapshot(state, hasSnapshot, snapshot);
    ParticipantEmitChanged(state, app, snapshot);
}

// TextSelectionHandle::register's quiet half: the snapshot is readable
// immediately, and SelectionChanged waits for WindowSelectionPublish.
static void ParticipantRememberSnapshot(TextSelectionParticipantState* state,
                                        bool hasSnapshot,
                                        const TextSelectionSnapshot& snapshot) {
    if (!state || ParticipantSnapshotSame(state, hasSnapshot, snapshot)) {
        return;
    }
    ParticipantStoreSnapshot(state, hasSnapshot, snapshot);
    state->drawChanged = true;
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
    if (!state || !state->registration.selfScroll) return;
    Bounds box = state->registration.bounds;
    Bounds mask = state->registration.hitbox;
    float x = box.x > mask.x ? box.x : mask.x;
    float y = box.y > mask.y ? box.y : mask.y;
    float right = box.Right() < mask.Right() ? box.Right() : mask.Right();
    float bottom = box.Bottom() < mask.Bottom() ? box.Bottom() : mask.Bottom();
    Bounds visible = {x, y, right - x, bottom - y};
    float delta = 0;
    bool has = !stopping && visible.w > 0 && visible.h > 0 &&
               AutoScrollComputeDelta(point.y, visible, &delta);
    TextSelectionEvent event;
    event.kind = TextSelectionEventKind::AutoScroll;
    event.autoScroll = delta;
    event.hasAutoScroll = has;
    EntityEmit(window->app, window, Entity<TextSelectionParticipantState>{id},
               &event);
}

TextSelectionRegistration TextSelectionRegistration::WithSelfScroll(
    bool value) const {
    TextSelectionRegistration out = *this;
    out.selfScroll = value;
    return out;
}

struct SelectionScrollAnchor {
    Bounds bounds = {};
    Bounds mask = {};
    Point offset = {};
    bool selfScroll = false;
};

static bool SelectionScrollAnchorOf(Window* win, SelectionScrollAnchor* out) {
    WindowSelection* selection = win->sel;
    auto* participant = (TextSelectionParticipantState*)EntityGet(
        win->app, selection->anchorParticipant);
    if (participant && participant->window == win && participant->registered) {
        const auto& registration = participant->registration;
        *out = {registration.bounds, registration.hitbox,
                registration.scrollOffset, registration.selfScroll};
        return true;
    }
    for (const TextHit& hit : win->paint.texts) {
        if (hit.scope == selection->scope && hit.docOff <= selection->anchor &&
            hit.docOff + len(hit.text) >= selection->anchor && !hit.atom) {
            *out = {hit.bounds, hit.contentMask};
            return true;
        }
    }
    return false;
}

static void SelectionScrollEdges(Bounds bounds, Bounds mask, Bounds anchorMask,
                                 float* top, float* bottom) {
    if (mask.w <= 0 || mask.h <= 0) return;
    if (bounds.y < mask.y && mask.y <= anchorMask.y && mask.y > *top) {
        *top = mask.y;
    }
    float edge = mask.Bottom();
    if (bounds.Bottom() > edge && edge >= anchorMask.Bottom() &&
        edge < *bottom) {
        *bottom = edge;
    }
}

static void WindowSelectionUpdateAutoScroll(Window* win, Point at) {
    WindowSelection* selection = win->sel;
    selection->autoScroll.Stop();
    selection->autoScrollStalled = 0;
    if (win->activeDrag.IsValid()) return;
    SelectionScrollAnchor anchor;
    if (!SelectionScrollAnchorOf(win, &anchor)) return;
    if (anchor.selfScroll) {
        ParticipantAutoScroll(win, at, false);
        return;
    }
    // Find the closest cuts above/below the anchor; the anchor's own
    // clip can be a rounded card that hides no text at all.
    constexpr float kNoEdge = 1e7f;
    float top = -kNoEdge;
    float bottom = kNoEdge;
    for (const TextHit& hit : win->paint.texts) {
        if (hit.scope == selection->scope && !hit.atom) {
            SelectionScrollEdges(hit.bounds, hit.contentMask, anchor.mask, &top,
                                 &bottom);
        }
    }
    for (EntityId id : selection->participants) {
        auto* p = (TextSelectionParticipantState*)EntityGet(win->app, id);
        if (p && p->window == win && p->registered &&
            p->registration.scope == selection->activeScope) {
            SelectionScrollEdges(p->registration.bounds, p->registration.hitbox,
                                 anchor.mask, &top, &bottom);
        }
    }
    const float kInset = 1.f;
    if ((top == -kNoEdge && bottom == kNoEdge) || anchor.mask.w < 2 * kInset ||
        anchor.mask.h < 2 * kInset || bottom - top < 2 * kInset) {
        return;
    }
    Bounds viewport = {anchor.mask.x, top, anchor.mask.w, bottom - top};
    float delta = 0;
    if (!AutoScrollComputeDelta(at.y, viewport, &delta)) return;
    selection->autoScrollPointer = at;
    selection->autoScroll.lastDrag = {
        ClampF(at.x, viewport.x + kInset, viewport.Right() - kInset),
        ClampF(at.y, top + kInset, bottom - kInset),
    };
    selection->autoScroll.hasLastDrag = true;
    selection->autoScroll.Set(delta);
    selection->autoScrollLastBounds = anchor.bounds;
    selection->autoScrollLastOffset = anchor.offset;
    WindowRequestAnimationFrame(win);
}

void WindowSelectionAutoScrollTick(Window* win) {
    WindowSelection* selection = win->sel;
    if (!selection || !selection->gesture.selecting ||
        !selection->autoScroll.IsActive()) {
        return;
    }
    SelectionScrollAnchor anchor;
    if (!SelectionScrollAnchorOf(win, &anchor) || win->activeDrag.IsValid()) {
        selection->autoScroll.Stop();
        return;
    }
    const Bounds& last = selection->autoScrollLastBounds;
    if (anchor.bounds.x == last.x && anchor.bounds.y == last.y &&
        anchor.bounds.w == last.w && anchor.bounds.h == last.h &&
        SamePoint(anchor.offset, selection->autoScrollLastOffset)) {
        constexpr int kStalledTicks = 3;
        if (++selection->autoScrollStalled >= kStalledTicks) {
            selection->autoScroll.Stop();
            return;
        }
    } else {
        selection->autoScrollStalled = 0;
        selection->autoScrollLastBounds = anchor.bounds;
        selection->autoScrollLastOffset = anchor.offset;
    }
    selection->autoScrollRelayout = true;
    Point at = selection->autoScroll.lastDrag;
    PlatformInput wheel =
        InputScrollWheel(at.x, at.y, 0, -selection->autoScroll.delta, true, {},
                         TouchPhase::Moved);
    WindowDispatchInput(win, &wheel);
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
    // WindowSelectionState::existing: with no layer keeping the window's
    // selection alive, there is nothing to register with.
    if (!WindowSelectionLayerLive(window)) return;
    // A registration made while a text_selection_scope paints takes it.
    TextSelectionScopeId current = {};
    if (CurrentTextSelectionScope(window, &current)) {
        value.scope = current;
    }
    participant->window = window;
    participant->registered = true;
    participant->registration = value;
    VecReset(participant->textBounds);
    for (int i = 0; i < value.textBoundsCount; i++) {
        VecAppend(participant->textBounds, value.textBounds[i]);
    }
    participant->registration.textBounds = participant->textBounds.els;
    participant->registration.textBoundsCount = len(participant->textBounds);
    WindowSelection* selection = WindowSelectionOf(window);
    participant->registrationGeneration = selection->frameGeneration;
    bool found = false;
    for (int i = 0; i < len(selection->participants); i++) {
        if (selection->participants[i] == state.id) {
            found = true;
            break;
        }
    }
    if (!found) VecAppend(selection->participants, state.id);
    // Full publication belongs to input handling or the end of the frame,
    // never to each participant's registration. This participant's snapshot
    // is readable now; observers run on the next WindowSelectionPublish.
    TextSelectionSnapshot snapshot;
    bool has = ComputeParticipantSnapshot(participant, app, &snapshot);
    participant = ParticipantState(*this, app);
    ParticipantRememberSnapshot(participant, has, snapshot);
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

TextSelectionBand TextSelectionBandFor(float rowsTop, float rowsBottom,
                                       Point selectionStart,
                                       Point selectionEnd) {
    float bandTop = std::min(selectionStart.y, selectionEnd.y);
    float bandBottom = std::max(selectionStart.y, selectionEnd.y);
    if (rowsBottom <= bandTop || rowsTop > bandBottom) {
        return TextSelectionBand::Misses;
    }
    if (bandTop < rowsTop && bandBottom >= rowsBottom) {
        return TextSelectionBand::Covers;
    }
    return TextSelectionBand::Partial;
}

// text_rows_extent: the top of the run's first row and the bottom of its
// last, in window coordinates, measured the way the walk below measures each
// character. Rows go down the layout in order, so the first and the last
// character bound all of them. False when the layout has nothing to say.
static bool RunRowsExtent(const TextSelectionRun& run, float* top,
                          float* bottom) {
    int n = len(run.text);
    uint32_t cp = 0;
    int firstBytes = Utf8At(run.text, 0, &cp);
    int last = n - 1;
    while (last > 0 && ((uint8_t)run.text.s[last] & 0xC0) == 0x80) {
        last--;
    }
    Bounds a, b;
    if (TextLayoutRangeRects(run.layout, run.text, 0, firstBytes, &a, 1) < 1 ||
        TextLayoutRangeRects(run.layout, run.text, last, n, &b, 1) < 1) {
        return false;
    }
    float ah = a.h > 0 ? a.h : run.bounds.h;
    float bh = b.h > 0 ? b.h : run.bounds.h;
    *top = run.bounds.y + std::min(a.y, b.y);
    *bottom = run.bounds.y + std::max(a.y + ah, b.y + bh);
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
    // A layout shaped from other text than the run says it holds cannot map
    // the run's offsets: `run.text.len() != run.layout.len()`.
    if (TextLayoutTextLen(run.layout) != len(run.text)) return out;
    // selection_range_for_run: each character is tested with its row's top
    // and height, so a run whose rows all miss the band, or all lie strictly
    // inside it with no endpoint on any row, has the same answer for every
    // character. Decide those without the walk below, which asks the layout
    // for a rect per character: one long code block alone made every paint
    // of a held selection cost tens of milliseconds.
    float rowsTop = 0, rowsBottom = 0;
    if (RunRowsExtent(run, &rowsTop, &rowsBottom)) {
        TextSelectionBand band = TextSelectionBandFor(
            rowsTop, rowsBottom, snapshot.windowPoints.anchor,
            snapshot.windowPoints.cursor);
        if (band == TextSelectionBand::Misses) {
            return out;
        }
        if (band == TextSelectionBand::Covers) {
            out.end = len(run.text);
            out.selected = true;
            return out;
        }
    }
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

TextSelectionProjection TextSelectionProjectRanges(
    const TextSelectionSnapshot* snapshot, const TextSelectionRun* runs,
    int count) {
    TextSelectionProjection out;
    out.active = snapshot != nullptr;
    for (int i = 0; i < count; i++) {
        VecAppend(out.ranges, snapshot ? ProjectRun(runs[i], *snapshot)
                                       : TextSelectionRange{});
    }
    return out;
}

void TextSelectionHandleSetSnapshot(const TextSelectionHandle& handle, App* app,
                                    const TextSelectionSnapshot* snapshot) {
    ParticipantSetSnapshot(ParticipantState(handle, app), app,
                           snapshot != nullptr,
                           snapshot ? *snapshot : TextSelectionSnapshot{});
}

TextSelectionProjection TextSelectionHandle::UpdateRuns(
    const TextSelectionRun* values, int count, App* app) const {
    TextSelectionParticipantState* participant = ParticipantState(*this, app);
    if (!participant) {
        TextSelectionProjection none;
        return none;
    }
    for (int i = 0; i < len(participant->runs); i++) {
        if (participant->runs[i].layout) {
            TextLayoutRelease(participant->runs[i].layout);
        }
    }
    VecReset(participant->runs);
    for (int i = 0; i < count; i++) {
        VecAppend(participant->runs, values[i]);
        if (values[i].layout) TextLayoutAddRef(values[i].layout);
    }
    TextSelectionProjection out = TextSelectionProjectRanges(
        participant->hasSnapshot ? &participant->snapshot : nullptr, values,
        count);

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
    if (cx->win) {
        WindowSelectionOf(cx->win)->layerFrame = cx->win->frameSeq + 1;
    }
    return Div(cx->a)->Id(StrL("window-text-selection"))->W(0)->H(0);
}

El* TextSelectionScope(El* element, TextSelectionScopeId scope) {
    if (element) {
        element->selectionScope = (uint32_t)scope.RuntimeScope();
    }
    return element;
}

bool CurrentTextSelectionScope(const Window* win, TextSelectionScopeId* out) {
    if (!win || len(win->textSelectionScopes) == 0) {
        return false;
    }
    if (out) {
        *out = TextSelectionScopeId::FromRaw(
            win->textSelectionScopes[len(win->textSelectionScopes) - 1]);
    }
    return true;
}

TextSelectionScopeGuard::TextSelectionScopeGuard(Window* window,
                                                 TextSelectionScopeId scope)
    : win(window) {
    if (win) {
        VecAppend(win->textSelectionScopes, scope.Value());
    }
}

TextSelectionScopeGuard::~TextSelectionScopeGuard() {
    if (win && len(win->textSelectionScopes) > 0) {
        win->textSelectionScopes.len--;
    }
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

// A participant loses its part of the selection: its snapshot and projected
// copy text go, it hears Cleared and SelectionChanged, and its own clear hook
// runs last (read before the events, which may touch the participant).
static void ParticipantDropSelection(
    Window* win, TextSelectionParticipantState* participant,
    Entity<TextSelectionParticipantState> self) {
    TextSelectionClearFn clear = participant->clear;
    void* clearUser = participant->clearUser;
    participant->hasSnapshot = false;
    participant->drawChanged = false;
    participant->localSelection = false;
    StrFree(participant->projectedCopyText);
    participant->projectedCopyText = {};
    participant->hasProjectedCopyText = false;
    TextSelectionEvent cleared;
    cleared.kind = TextSelectionEventKind::Cleared;
    EntityEmit(win->app, win, self, &cleared);
    TextSelectionEvent changed;
    changed.kind = TextSelectionEventKind::SelectionChanged;
    EntityEmit(win->app, win, self, &changed);
    if (clear) clear(clearUser, win->app);
}

void WindowSelectionClear(Window* win) {
    WindowSelection* s = win ? win->sel : nullptr;
    if (!s || s->clearing) {
        return;
    }
    s->clearing = true;
    TouchSelectionSource source = s->touchSource;
    s->touchSource = {};
    if (source.clear) {
        if (void* receiver = EntityGet(win->app, source.entity)) {
            source.clear(receiver, win);
        }
    }
    s->autoScroll.Stop();
    s->autoScrollRelayout = false;
    ParticipantAutoScroll(win, s->cursorPoint, true);
    TextSelectionClear(&s->gesture);
    s->anchor = -1;
    s->cursor = -1;
    s->pendingReflow = {};
    ClearCopyProjection(&s->copyProjection);
    s->selectAllOwner = {};
    s->scope = 0;
    s->hasWindowPoints = false;
    s->hasAnchorCell = false;
    s->touchActive = false;
    s->touchGeometryStart = -1;
    s->touchGeometryEnd = -1;
    s->touchMenuOpen = false;
    s->hasTouchEdgeDrag = false;
    s->unitHeld = false;
    s->unitClickCount = 0;
    s->unitStart = 0;
    s->unitEnd = 0;
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
            ParticipantDropSelection(win, participant, participant->self);
        }
        VecReset(participants);
    }
    s->clearing = false;
    WindowSelectionApply(win);
}

bool WindowSelectionHas(const Window* win) {
    const WindowSelection* s = win ? win->sel : nullptr;
    if (s && s->touchSource.snapshot) {
        void* receiver = EntityGet(win->app, s->touchSource.entity);
        return receiver && s->touchSource.snapshot(receiver, win, nullptr);
    }
    if (s && s->selectAllOwner.IsValid()) {
        return EntityGet(win->app, s->selectAllOwner) != nullptr;
    }
    if (!s || s->anchor < 0 || s->cursor < 0 || s->anchor == s->cursor ||
        AwaitingSelectionReflow(s)) {
        return false;
    }
    return TextSelectionPublishes(&s->gesture);
}

// SelectionEndpoint::resolve for the anchor: true unless the participant it
// resolved to is gone from the window. An anchor in plain runs with no
// participant resolves for as long as there is text to hold it.
static bool AnchorResolves(Window* win, const WindowSelection* s) {
    if (!s->anchorParticipant.IsValid()) {
        return true;
    }
    TextSelectionParticipantState* participant =
        (TextSelectionParticipantState*)EntityGet(win->app,
                                                  s->anchorParticipant);
    return participant && participant->registered && participant->window == win;
}

static void ClampDragToAnchorCell(const WindowSelection* s, float* x,
                                  float* y) {
    if (!s || !s->hasAnchorCell || !x || !y) {
        return;
    }
    Point anchorOut;
    Point cursorOut;
    if (!ConfineToCell(true, s->anchorPoint, s->anchorCell, true, s->anchorCell,
                       s->anchorPoint, {*x, *y}, &anchorOut, &cursorOut)) {
        return;
    }
    *x = cursorOut.x;
    *y = cursorOut.y;
}

static const Bounds* AnchorCellOf(const WindowSelection* s) {
    return s && s->hasAnchorCell ? &s->anchorCell : nullptr;
}

static void SelectionUnitHold(WindowSelection* s, int clickCount, int a,
                              int b) {
    s->unitHeld = true;
    s->unitClickCount = clickCount < 2 ? 2 : clickCount;
    s->unitStart = a < b ? a : b;
    s->unitEnd = a < b ? b : a;
}

// Moves the cursor to the edge of the word or line under the pointer, and
// the anchor to the far edge of the unit the click selected. Inside that
// unit the click's selection stands. Over no text the cursor stays put.
static void SelectionUnitExtend(Window* win, float x, float y) {
    WindowSelection* s = win ? win->sel : nullptr;
    if (!s || !s->unitHeld) {
        return;
    }
    PaintCtx* ctx = &win->paint;
    float qx = x;
    float qy = y;
    ClampDragToAnchorCell(s, &qx, &qy);
    const Bounds* cell = AnchorCellOf(s);
    TextSelectionExtend(
        &s->gesture,
        TextHitOffsetIn(ctx, qx, qy, false, s->scope, nullptr, 0, cell) >= 0);
    int off = TextHitOffsetIn(ctx, qx, qy, true, s->scope, nullptr, 0, cell);
    s->cursorPoint = {qx, qy};
    s->hasWindowPoints = true;
    if (off < 0) {
        return;
    }
    if (off >= s->unitStart && off < s->unitEnd) {
        s->anchor = s->unitStart;
        s->cursor = s->unitEnd;
        s->gesture.didHitText = true;
        return;
    }
    int edgeA = 0;
    int edgeB = 0;
    bool before = off < s->unitStart;
    bool snapped = TextMultiClickRangeIn(ctx, qx, qy, s->unitClickCount,
                                         s->scope, &edgeA, &edgeB, nullptr);
    if (before) {
        s->anchor = s->unitEnd;
        s->cursor = snapped ? edgeA : off;
    } else {
        s->anchor = s->unitStart;
        s->cursor = snapped ? edgeB : off;
    }
    s->gesture.didHitText = true;
}

void WindowSelectionPress(Window* win, float x, float y, int clickCount,
                          bool extend) {
    if (WindowKeepsTextSelectionAt(win, {x, y})) return;
    if (win && BaseIsTextSelectionSuppressed(win->app)) {
        WindowSelectionClear(win);
        return;
    }
    WindowSelection* s = WindowSelectionOf(win);
    if (!s) {
        return;
    }
    // prepare_for_mouse_down: every press — a shift-click too — clears what
    // the participants hold (each hears Cleared and its clear handler runs,
    // and a local selection ends), keeping only the anchor a shift-click
    // extends from.
    {
        bool keep = extend && s->anchor >= 0 && !AwaitingSelectionReflow(s);
        int anchor = s->anchor;
        int scope = s->scope;
        Point anchorPoint = s->anchorPoint;
        bool anchorInsideText = s->anchorInsideText;
        EntityId anchorParticipant = s->anchorParticipant;
        bool hasAnchorCell = s->hasAnchorCell;
        Bounds anchorCell = s->anchorCell;
        // A shift-click keeps the word or line a double or triple click
        // selected. Any other press drops it.
        bool keepUnit = extend && s->unitHeld;
        int unitClickCount = s->unitClickCount;
        int unitStart = s->unitStart;
        int unitEnd = s->unitEnd;
        WindowSelectionClear(win);
        if (keep) {
            s->anchor = anchor;
            s->cursor = anchor;
            s->scope = scope;
            s->anchorPoint = anchorPoint;
            s->cursorPoint = anchorPoint;
            s->hasWindowPoints = true;
            s->anchorInsideText = anchorInsideText;
            s->anchorParticipant = anchorParticipant;
            s->hasAnchorCell = hasAnchorCell;
            s->anchorCell = anchorCell;
            if (keepUnit) {
                s->unitHeld = true;
                s->unitClickCount = unitClickCount;
                s->unitStart = unitStart;
                s->unitEnd = unitEnd;
            }
        }
    }
    PaintCtx* ctx = &win->paint;
    // A shift-click keeps the anchor and moves the cursor — Rust's
    // `begin_in_window(.., extend)` — and stays in the scope it began in.
    // It begins a gesture of its own, so a drag after it carries the cursor
    // on, and it has hit text if either end did. An anchor whose participant
    // has since been swept no longer resolves, and the press starts afresh.
    if (extend && s->anchor >= 0 && AnchorResolves(win, s)) {
        float qx = x;
        float qy = y;
        ClampDragToAnchorCell(s, &qx, &qy);
        int off = TextHitOffsetIn(ctx, qx, qy, true, s->scope, nullptr, 0,
                                  AnchorCellOf(s));
        if (off >= 0) {
            s->cursor = off;
            s->cursorPoint = {qx, qy};
            s->hasWindowPoints = true;
            bool inside =
                TextHitOffsetIn(ctx, x, y, false, s->scope, nullptr) >= 0;
            TextSelectionBegin(&s->gesture, s->anchorInsideText || inside);
            if (s->unitHeld) {
                SelectionUnitExtend(win, x, y);
            }
        }
        WindowSelectionPublish(win);
        return;
    }
    // Two clicks take the word under the pointer, three its logical line.
    // The gesture stays down so a drag, and a shift-click after it, extend
    // by that unit. A touch selects the unit and stops, the way a double
    // tap grows handles instead of a word-by-word drag.
    int scope = 0;
    int a = 0;
    int b = 0;
    int activeScope =
        s->activeScope.raw != 0 ? s->activeScope.RuntimeScope() : -1;
    Point ends[] = {{x - 0.5f, y}, {x + 0.5f, y}};
    if (TextMultiClickRangeIn(ctx, x, y, clickCount, activeScope, &a, &b,
                              &scope, ends)) {
        s->scope = scope;
        s->anchor = a;
        s->cursor = b;
        if (s->activeScope.raw == 0) {
            s->activeScope = TextSelectionScopeId::FromRaw((uint64_t)scope);
        }
        s->anchorPoint = ends[0];
        s->cursorPoint = ends[1];
        s->hasWindowPoints = true;
        s->anchorInsideText = true;
        s->anchorParticipant = {};
        ParticipantAt(win, s->anchorPoint, &s->anchorParticipant);
        s->hasAnchorCell = TextTableCellAt(ctx, x, y, &s->anchorCell);
        TextSelectionBegin(&s->gesture, true);
        if (BaseIsTouchPress(win->app)) {
            TextSelectionEnd(&s->gesture);
        } else {
            SelectionUnitHold(s, clickCount, a, b);
        }
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
        s->anchorInsideText =
            TextHitOffsetIn(ctx, x, y, false, scope, nullptr) >= 0;
        TextSelectionBegin(&s->gesture, s->anchorInsideText);
        EntityId participantId = {};
        TextSelectionParticipantState* participant =
            ParticipantAt(win, s->anchorPoint, &participantId);
        s->anchorParticipant = participant ? participantId : EntityId{};
        s->hasAnchorCell = TextTableCellAt(ctx, x, y, &s->anchorCell);
        if (participant && participant->focus) {
            participant->focus(participant->focusUser, win, win->app);
        }
        WindowSelectionPublish(win);
        WindowSelectionApply(win);
        return;
    }
    WindowSelectionClear(win);
}

bool TextSelectionIsSelectableAt(Window* win, float x, float y) {
    // A touch handle is not text. The selection object itself may not exist
    // yet: painted runs are the live geometry, and the state is created on
    // the first press.
    if (!win || WindowKeepsTextSelectionAt(win, {x, y})) {
        return false;
    }
    // The default scope is raw 0. -1 would match every scope, including a
    // dialog's text behind the active one.
    const WindowSelection* s = win->sel;
    int scope =
        s && s->activeScope.raw != 0 ? s->activeScope.RuntimeScope() : 0;
    return TextHitOffsetIn(&win->paint, x, y, false, scope, nullptr) >= 0;
}

bool WindowSelectionLongPressStart(Window* win, float x, float y) {
    WindowSelectionPress(win, x, y, 2, false);
    WindowSelection* selection = win ? win->sel : nullptr;
    if (!selection || !WindowSelectionHas(win)) return false;
    // The word is selected, then the drag is by characters. A double-click
    // drag is the one that keeps the word as its unit.
    selection->unitHeld = false;
    selection->gesture.selecting = true;
    selection->gesture.didHitText = true;
    selection->touchActive = true;
    selection->touchMenuOpen = false;
    selection->hasTouchEdgeDrag = false;
    return true;
}

bool WindowSelectionTouchSnapshot(Window* win, TouchSelectionSnapshot* out) {
    if (!out || !win || !win->sel) return false;
    WindowSelection* s = win->sel;
    if (s->touchSource.snapshot) {
        void* receiver = EntityGet(win->app, s->touchSource.entity);
        if (!receiver || !s->touchSource.snapshot(receiver, win, out)) {
            return false;
        }
    } else {
        if (!s->touchActive || !s->hasWindowPoints || !WindowSelectionHas(win))
            return false;
        int start = std::min(s->anchor, s->cursor);
        int end = std::max(s->anchor, s->cursor);
        if (start == s->touchGeometryStart && end == s->touchGeometryEnd) {
            *out = s->touchGeometry;
        } else {
            const float lineH = 16.f;
            bool reversed = s->cursor < s->anchor;
            *out = TouchSelectionSnapshot::New(
                TouchCaretLineBox(reversed ? s->cursorPoint : s->anchorPoint,
                                  lineH),
                TouchCaretLineBox(reversed ? s->anchorPoint : s->cursorPoint,
                                  lineH));
        }
    }
    if (s->hasTouchEdgeDrag) {
        *out = out->WithDragging(s->touchEdgeDrag.edge);
    }
    *out = out->WithMenuOpen(s->touchMenuOpen && !s->hasTouchEdgeDrag);
    return true;
}

static const TextHit* TouchEndpointHit(const PaintCtx* paint,
                                       const WindowSelection& selection,
                                       int offset, bool trailing) {
    const TextHit* adjacent = nullptr;
    for (int i = 0; i < len(paint->texts); i++) {
        const TextHit& hit = paint->texts[i];
        int local = offset - hit.docOff;
        if (local < 0) break;
        if (hit.scope != selection.scope || hit.atom || hit.blockPlugin ||
            !len(hit.text) || local > len(hit.text)) {
            continue;
        }
        if (trailing ? local > 0 : local < len(hit.text)) return &hit;
        adjacent = &hit;
    }
    return adjacent;
}

static Bounds TouchEndpointCaret(PaintCtx* paint, const TextHit& hit,
                                 int offset, bool trailing) {
    int local = offset - hit.docOff;
    if (hit.inlinePlugin) {
        float x = local ? hit.bounds.Right() : hit.bounds.x;
        return TouchCaretLineBox({x, hit.bounds.y}, hit.bounds.h);
    }
    return TextHitCaretBounds(paint, hit, local, trailing);
}

static TouchSelectionSnapshot PaintedTouchGeometry(
    PaintCtx* paint, const WindowSelection& selection, EntityId owner,
    Bounds clips[2]) {
    int offsets[] = {std::min(selection.anchor, selection.cursor),
                     std::max(selection.anchor, selection.cursor)};
    Bounds carets[2] = {};
    bool visible[2] = {};
    for (int i = 0; i < 2; i++) {
        const TextHit* hit =
            TouchEndpointHit(paint, selection, offsets[i], i == 1);
        if (!hit || (owner.IsValid() && hit->owner != owner)) continue;
        carets[i] = TouchEndpointCaret(paint, *hit, offsets[i], i == 1);
        clips[i] = hit->contentMask;
        visible[i] = TouchCaretInView(carets[i], clips[i]);
    }
    return TouchSelectionSnapshot::New(carets[0], carets[1])
        .WithEdgeVisible(SelectionEdge::Start, visible[0])
        .WithEdgeVisible(SelectionEdge::End, visible[1]);
}

void WindowSelectionPaintTouchHandles(PaintCtx* paint, EntityId owner,
                                      Rgba color) {
    Window* win = paint->window;
    const WindowSelection* selection = win ? win->sel : nullptr;
    if (!selection || !selection->touchActive ||
        selection->touchSource.snapshot || !WindowSelectionHas(win)) {
        return;
    }
    Bounds clips[2] = {};
    TouchSelectionSnapshot snapshot =
        PaintedTouchGeometry(paint, *selection, owner, clips);
    color.a = 255;
    for (int i = 0; i < 2; i++) {
        SelectionEdge edge = i == 0 ? SelectionEdge::Start : SelectionEdge::End;
        if (!snapshot.IsEdgeVisible(edge)) continue;
        const Bounds& clip = clips[i];
        CanvasPushClip(paint, clip.x, clip.y, clip.w, clip.h);
        PaintTouchHandles(
            paint,
            snapshot.WithEdgeVisible(
                i == 0 ? SelectionEdge::End : SelectionEdge::Start, false),
            color);
        CanvasPopClip(paint);
    }
}

static void RefreshTouchGeometry(Window* win, WindowSelection* selection) {
    if (!selection->touchActive || selection->touchSource.snapshot ||
        !WindowSelectionHas(win)) {
        return;
    }
    Bounds clips[2] = {};
    TouchSelectionSnapshot snapshot =
        PaintedTouchGeometry(&win->paint, *selection, {}, clips);
    bool changed = false;
    for (int i = 0; i < 2; i++) {
        SelectionEdge edge = i == 0 ? SelectionEdge::Start : SelectionEdge::End;
        bool visible = snapshot.IsEdgeVisible(edge);
        Bounds a = snapshot.Edge(edge);
        Bounds b = selection->touchGeometry.Edge(edge);
        changed |= visible != selection->touchGeometry.IsEdgeVisible(edge) ||
                   (visible && (a.x != b.x || a.y != b.y || a.h != b.h));
    }
    selection->touchGeometry = snapshot;
    selection
        ->touchGeometryStart = std::min(selection->anchor, selection->cursor);
    selection
        ->touchGeometryEnd = std::max(selection->anchor, selection->cursor);
    if (changed && selection->touchMenuOpen) WindowRequestAnimationFrame(win);
}

void WindowSelectionBeginTouchEdgeDrag(Window* win, SelectionEdge edge,
                                       Bounds caret, Point finger) {
    WindowSelection* s = win ? win->sel : nullptr;
    if (!s) return;
    s->hasTouchEdgeDrag = true;
    s->touchEdgeDrag = TouchEdgeDrag::Begin(edge, caret, finger);
    s->touchMenuOpen = false;
    if (s->touchSource.extend) return;

    // The gesture's anchor is the held end, even when that end is later in
    // the text. It stays there as the moving caret passes it.
    bool anchorIsStart = s->anchor < s->cursor;
    if (anchorIsStart == (edge == SelectionEdge::Start)) {
        std::swap(s->anchor, s->cursor);
        std::swap(s->anchorPoint, s->cursorPoint);
    }
    s->anchorParticipant = {};
    ParticipantAt(win, s->anchorPoint, &s->anchorParticipant);
    s->anchorInsideText = true;
    s->selectAllOwner = {};
    s->unitHeld = false;
    TextSelectionBegin(&s->gesture, true);
    WindowSelectionPublish(win);
    WindowSelectionApply(win);
}

void WindowSelectionAttachTouchSource(Window* win,
                                      TouchSelectionSource source) {
    if (WindowSelection* s = WindowSelectionOf(win)) {
        s->touchSource = source;
        s->touchSourceFrame = win->frameSeq;
        s->touchSourceEdge = SelectionEdge::End;
    }
}

void WindowSelectionTouchSourcePainted(Window* win, EntityId source) {
    if (win && win->sel && win->sel->touchSource.entity == source) {
        win->sel->touchSourceFrame = win->frameSeq + 1;
    }
}

void WindowSelectionCloseEditMenu(Window* win) {
    if (win && win->sel) {
        win->sel->touchMenuOpen = false;
    }
}

void WindowSelectionRegisterTouchUi(Window* win, Bounds bounds) {
    WindowSelection* s = WindowSelectionOf(win);
    if (!s) {
        return;
    }
    s->touchUi = bounds;
    s->hasTouchUi = true;
    s->touchUiThisFrame = true;
}

bool WindowSelectionTouchUiContains(const Window* win, Point at) {
    const WindowSelection* s = win ? win->sel : nullptr;
    return s && s->hasTouchUi && s->touchUi.Contains(at);
}

static void WindowSelectionDragAt(Window* win, float x, float y) {
    WindowSelection* s = win ? win->sel : nullptr;
    if (!s || !s->gesture.selecting || AwaitingSelectionReflow(s)) {
        return;
    }
    PaintCtx* ctx = &win->paint;
    // did_hit_text is the strict hit — whether *this* point is on a glyph —
    // while the offset the selection runs to is the nearest one either way,
    // so a drag through the margin keeps going along the line.
    float qx = x;
    float qy = y;
    ClampDragToAnchorCell(s, &qx, &qy);
    const Bounds* cell = AnchorCellOf(s);
    if (s->unitHeld) {
        SelectionUnitExtend(win, x, y);
    } else {
        TextSelectionExtend(
            &s->gesture, TextHitOffsetIn(ctx, qx, qy, false, s->scope, nullptr,
                                         0, cell) >= 0);
        int off =
            TextHitOffsetIn(ctx, qx, qy, true, s->scope, nullptr, 0, cell);
        if (off >= 0 && (!s->hasTouchEdgeDrag || off != s->anchor)) {
            s->cursor = off;
            s->cursorPoint = {qx, qy};
            s->hasWindowPoints = true;
            if (s->hasTouchEdgeDrag) {
                s->touchEdgeDrag.edge =
                    off < s->anchor ? SelectionEdge::Start : SelectionEdge::End;
            }
        }
    }
}

void WindowSelectionDrag(Window* win, float x, float y) {
    if (win && win->sel && win->sel->touchSource.extend) {
        WindowSelection* s = win->sel;
        if (void* receiver = EntityGet(win->app, s->touchSource.entity)) {
            SelectionEdge edge = s->hasTouchEdgeDrag ? s->touchEdgeDrag.edge
                                                     : s->touchSourceEdge;
            edge = s->touchSource.extend(receiver, win, edge, {x, y});
            if (s->hasTouchEdgeDrag)
                s->touchEdgeDrag.edge = edge;
            else
                s->touchSourceEdge = edge;
        }
        return;
    }
    if (!win || !win->sel || !win->sel->gesture.selecting) return;
    WindowSelectionDragAt(win, x, y);
    WindowSelectionUpdateAutoScroll(win, {x, y});
    WindowSelectionPublish(win);
    // Copy reads this frame's runs. The points have to match the gesture
    // before the next paint, or a block still answers for the previous one.
    WindowSelectionApply(win);
}

void WindowSelectionRelease(Window* win) {
    WindowSelection* s = win ? win->sel : nullptr;
    if (s) {
        TouchSelectionSource source = s->touchSource;
        if (source.endDrag) {
            if (void* receiver = EntityGet(win->app, source.entity)) {
                source.endDrag(receiver, win);
            }
        }
        s->autoScroll.Stop();
        s->autoScrollRelayout = false;
        ParticipantAutoScroll(win, s->cursorPoint, true);
        TextSelectionEnd(&s->gesture);
        WindowSelectionPublish(win);
        WindowSelectionApply(win);
    }
}

static const TextViewState* TextViewForSelectionOwner(App* app,
                                                      EntityId owner) {
    if (!EntityHasDrop(app, owner, &EntityDropT<TextViewState>)) {
        return nullptr;
    }
    return (const TextViewState*)EntityGet(app, owner);
}

static const TextViewState* RetainedSelectionView(const Window* win) {
    const WindowSelection* selection = win ? win->sel : nullptr;
    if (!selection || !AwaitingSelectionReflow(selection)) return nullptr;
    EntityId owner = selection->pendingReflow.owner;
    if (selection->copyProjection.owner != owner) return nullptr;
    const TextViewState* view = TextViewForSelectionOwner(win->app, owner);
    return view &&
                   view->fullUpdateRevision == selection->pendingReflow
                                                   .replacement &&
                   view->CopyDocumentReady()
               ? view
               : nullptr;
}

static Str RetainedSelectionText(const WindowSelection* selection,
                                 SelectionFormat format) {
    const TextSelectionCopyProjection& copy = selection->copyProjection;
    return format == SelectionFormat::Source ||
                   (format == SelectionFormat::SourceAcrossStyles &&
                    copy.mixedStyles)
               ? copy.source
               : copy.plain;
}

static const TextViewState* SelectAllTextView(Window* win) {
    if (!win || !win->sel || win->sel->touchSource.copy) return nullptr;
    return TextViewForSelectionOwner(win->app, win->sel->selectAllOwner);
}

static bool SelectionContainsHit(const TextHit& hit, int lo, int hi,
                                 int scope) {
    int size = hit.atom || hit.blockPlugin ? 1 : len(hit.text);
    return size > 0 && (scope < 0 || hit.scope == scope) && hit.docOff < hi &&
           hit.docOff + size > lo;
}

static bool SameSourcePoint(TextSelectionSourcePoint a,
                            TextSelectionSourcePoint b) {
    return a.source == b.source && a.entityEnd == b.entityEnd &&
           a.entityByte == b.entityByte;
}

static void CaptureSelectionCopy(Window* win, const TextViewState* view,
                                 const TextSelectionReflow& selection) {
    auto& copy = win->sel->copyProjection;
    int lo = std::min(selection.offsets[0], selection.offsets[1]);
    int hi = std::max(selection.offsets[0], selection.offsets[1]);
    for (const TextHit& hit : win->paint.texts) {
        if (hit.docOff >= hi) break;
        if (hit.owner != view->self &&
            SelectionContainsHit(hit, lo, hi, selection.scope)) {
            ClearCopyProjection(&copy);
            return;
        }
    }
    if (copy.owner == view->self && copy.revision == view->committedRevision &&
        copy.selectionRevision == view->selectionRevision &&
        SameSourcePoint(copy.endpoints[0], selection.endpoints[0]) &&
        SameSourcePoint(copy.endpoints[1], selection.endpoints[1])) {
        return;
    }

    ClearCopyProjection(&copy);
    copy.owner = view->self;
    copy.revision = view->committedRevision;
    copy.selectionRevision = view->selectionRevision;
    copy.endpoints[0] = selection.endpoints[0];
    copy.endpoints[1] = selection.endpoints[1];
    const Bounds* cell = AnchorCellOf(win->sel);
    StrBuilder plain;
    StrBuilder source;
    CopyTextHitsIn(&win->paint, lo, hi, selection.scope, &plain,
                   SelectionFormat::Plain, cell);
    CopyTextHitsIn(&win->paint, lo, hi, selection.scope, &source,
                   SelectionFormat::Source, cell);
    copy.plain = plain.TakeStr();
    copy.source = source.TakeStr();
    copy.mixedStyles = TextHitsUseSourceAcrossStyles(
        &win->paint, lo, hi, selection.scope, view->self);
    Span range;
    if (TextHitsSourceRange(&win->paint, lo, hi, selection.scope, view->self)
            .IntoRange(&range)) {
        copy.sourceStart = range.start;
        copy.sourceEnd = range.end;
    }
}

static int FirstMapSegmentAt(const SelSourceMap& map, int offset) {
    int lo = 0;
    int hi = map.count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (map.segments[mid].renderedEnd < offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static const TextHit* CaptureSourcePoint(const PaintCtx& paint, int offset,
                                         int scope, bool trailing,
                                         TextSelectionSourcePoint* out) {
    for (const TextHit& hit : paint.texts) {
        if (hit.docOff > offset) break;
        const SelSourceMap* map = hit.map;
        if (hit.scope != scope || !hit.owner.IsValid() || !map || map->atomic ||
            hit.atom || hit.blockPlugin ||
            offset > hit.docOff + len(hit.text)) {
            continue;
        }
        int local = map->offset + offset - hit.docOff;
        for (int i = FirstMapSegmentAt(*map, local); i < map->count; i++) {
            const SourceSegment& segment = map->segments[i];
            if (local < segment.renderedStart) break;
            if (local > segment.renderedEnd) continue;
            if (!trailing && local == segment.renderedEnd &&
                i + 1 < map->count &&
                map->segments[i + 1].renderedStart == local &&
                offset < hit.docOff + len(hit.text)) {
                continue;
            }
            if (segment.linear) {
                out->source =
                    segment.sourceStart + local - segment.renderedStart;
            } else if (local == segment.renderedEnd) {
                out->source = segment.sourceEnd;
            } else {
                out->source = segment.sourceStart;
                if (local > segment.renderedStart) {
                    out->entityEnd = segment.sourceEnd;
                    out->entityByte = local - segment.renderedStart;
                }
            }
            return &hit;
        }
    }
    return nullptr;
}

static const TextHit* RestoreSourcePoint(const PaintCtx& paint,
                                         const TextSelectionReflow& old,
                                         const TextSelectionSourcePoint& key,
                                         bool trailing, int* offset) {
    const TextHit* fallback = nullptr;
    for (const TextHit& hit : paint.texts) {
        const SelSourceMap* map = hit.map;
        if (hit.owner != old.owner || hit.scope != old.scope || !map ||
            map->atomic || hit.atom || hit.blockPlugin)
            continue;
        for (int i = FirstMapSegmentAt(*map, map->offset); i < map->count;
             i++) {
            const SourceSegment& segment = map->segments[i];
            if (segment.renderedStart > map->offset + len(hit.text)) break;
            if (key.source < segment.sourceStart || key.source > segment
                                                                     .sourceEnd)
                continue;
            int at = segment.renderedStart;
            if (key.entityEnd >= 0) {
                if (segment.linear || key.source != segment.sourceStart ||
                    key.entityEnd != segment.sourceEnd ||
                    key.entityByte >= segment.renderedEnd - at)
                    continue;
                at += key.entityByte;
            } else if (segment.linear) {
                at += key.source - segment.sourceStart;
            } else if (key.source == segment.sourceEnd) {
                at = segment.renderedEnd;
            } else if (key.source != segment.sourceStart) {
                continue;
            }
            int local = at - map->offset;
            if (local < 0 || local > len(hit.text)) continue;
            int absolute = hit.docOff + local;
            if (trailing ? local > 0 : local < len(hit.text)) {
                *offset = absolute;
                return &hit;
            }
            if (!fallback) {
                fallback = &hit;
                *offset = absolute;
            }
        }
    }
    return fallback;
}

TextSelectionReflow WindowSelectionCaptureReflow(Window* win) {
    TextSelectionReflow out;
    const WindowSelection* s = win->sel;
    if (!s || s->touchSource.entity.IsValid() || s->selectAllOwner.IsValid())
        return out;
    if (AwaitingSelectionReflow(s)) return s->pendingReflow;
    if (!WindowSelectionHas(win)) return out;
    out.offsets[0] = s->anchor;
    out.offsets[1] = s->cursor;
    out.scope = s->scope;
    const TextHit* endpoints[2] = {};
    for (int i = 0; i < 2; i++) {
        endpoints[i] = CaptureSourcePoint(win->paint, out.offsets[i], s->scope,
                                          out.offsets[i] > out.offsets[1 - i],
                                          &out.endpoints[i]);
        if (!endpoints[i]) return {};
        out.bounds[i] = endpoints[i]->bounds;
    }
    if (endpoints[0]->owner != endpoints[1]->owner) return {};
    const TextViewState* view =
        TextViewForSelectionOwner(win->app, endpoints[0]->owner);
    if (!view || !view->CopyDocumentReady()) return {};
    out.owner = view->self;
    out.replacement = view->fullUpdateRevision;
    CaptureSelectionCopy(win, view, out);
    if (s->unitHeld) {
        const TextHit* start = CaptureSourcePoint(
            win->paint, s->unitStart, s->scope, false, &out.unit[0]);
        const TextHit* end = CaptureSourcePoint(win->paint, s->unitEnd,
                                                s->scope, true, &out.unit[1]);
        if (!start || !end || start->owner != out.owner ||
            end->owner != out.owner)
            out.unit[0] = {};
    }
    return out;
}

void WindowSelectionRestoreReflow(Window* win, const TextSelectionReflow& old) {
    if (!old.owner.IsValid()) return;
    WindowSelection* s = win->sel;
    if (!s || s->scope != old.scope || s->anchor != old.offsets[0] ||
        s->cursor != old.offsets[1] || s->selectAllOwner.IsValid())
        return;
    const TextViewState* view = TextViewForSelectionOwner(win->app, old.owner);
    bool wasPending = AwaitingSelectionReflow(s);
    if (!view || view->fullUpdateRevision != old.replacement) {
        if (wasPending) WindowSelectionClear(win);
        return;
    }
    int offsets[2] = {};
    const TextHit* endpoints[2] = {};
    for (int i = 0; i < 2; i++) {
        endpoints[i] = RestoreSourcePoint(win->paint, old, old.endpoints[i],
                                          old.offsets[i] > old.offsets[1 - i],
                                          &offsets[i]);
        if (!endpoints[i]) {
            // The offsets from the old frame could now name a different
            // run. Keep the source endpoints until their geometry returns.
            s->pendingReflow = old;
            WindowSelectionApply(win);
            if (!wasPending) WindowRequestAnimationFrame(win);
            return;
        }
    }
    bool remapped = offsets[0] != s->anchor || offsets[1] != s->cursor;
    s->anchor = offsets[0];
    s->cursor = offsets[1];
    s->pendingReflow = {};
    if (s->hasAnchorCell && endpoints[0]->inCell)
        s->anchorCell = endpoints[0]->cell;
    if (s->hasWindowPoints) {
        Point* points[] = {&s->anchorPoint, &s->cursorPoint};
        for (int i = 0; i < 2; i++) {
            const Bounds& now = endpoints[i]->bounds;
            const Bounds& was = old.bounds[i];
            if (remapped || now.x != was.x || now.y != was.y ||
                now.w != was.w || now.h != was.h) {
                *points[i] =
                    TextHitCaretPoint(&win->paint, *endpoints[i],
                                      offsets[i] - endpoints[i]->docOff,
                                      offsets[i] > offsets[1 - i]);
            }
        }
    }
    if (remapped && s->unitHeld) {
        s->unitHeld =
            old.unit[0].source >= 0 &&
            RestoreSourcePoint(win->paint, old, old.unit[0], false,
                               &s->unitStart) &&
            RestoreSourcePoint(win->paint, old, old.unit[1], true, &s->unitEnd);
    }
    WindowSelectionApply(win);
    if (remapped || wasPending) WindowRequestAnimationFrame(win);
}

int WindowSelectionTextAs(Window* win, char* out, int cap,
                          SelectionFormat fmt) {
    if (win && win->sel && win->sel->touchSource.copy && out && cap > 0) {
        const TouchSelectionSource& source = win->sel->touchSource;
        if (void* receiver = EntityGet(win->app, source.entity)) {
            return StrCopyCapped(out, cap,
                                 source.copy(receiver, win, GetTempArena()));
        }
    }
    if (const TextViewState* view = SelectAllTextView(win)) {
        return StrCopyCapped(out, cap, view->SelectAllText(fmt));
    }
    if (RetainedSelectionView(win)) {
        return StrCopyCapped(out, cap, RetainedSelectionText(win->sel, fmt));
    }
    if (!WindowSelectionHas(win)) {
        if (out && cap > 0) {
            out[0] = 0;
        }
        return 0;
    }
    WindowSelection* s = win->sel;
    return CopyTextHitsIn(&win->paint, s->anchor, s->cursor, s->scope, out, cap,
                          fmt, AnchorCellOf(s));
}

int WindowSelectionText(Window* win, char* out, int cap) {
    return WindowSelectionTextAs(win, out, cap, WindowSelectionFormat(win));
}

bool WindowSelectionHasEntity(const Window* win, EntityId owner) {
    if (const TextViewState* view = RetainedSelectionView(win))
        return view->self == owner;
    if (!owner.IsValid() || !WindowSelectionHas(win)) return false;
    const WindowSelection* selection = win->sel;
    if (selection->selectAllOwner.IsValid()) {
        return selection->selectAllOwner == owner;
    }
    int a = selection->anchor;
    int b = selection->cursor;
    if (a > b) {
        int swap = a;
        a = b;
        b = swap;
    }
    const PaintCtx* paint = &win->paint;
    for (int i = 0; i < len(paint->texts); i++) {
        const TextHit& hit = paint->texts[i];
        if (hit.owner != owner || hit.scope != selection->scope) continue;
        int n = hit.atom ? 1 : len(hit.text);
        if (a < hit.docOff + n && b > hit.docOff) return true;
    }
    return false;
}

bool WindowSelectionRetainedSourceRange(const Window* win, EntityId owner,
                                        Span* out) {
    const TextViewState* view = RetainedSelectionView(win);
    if (!view || view->self != owner || !out) return false;
    const auto& copy = win->sel->copyProjection;
    if (copy.sourceStart < 0) return false;
    *out = {copy.sourceStart, copy.sourceEnd};
    return true;
}

int WindowSelectionTextForEntity(Window* win, EntityId owner, char* out,
                                 int cap, SelectionFormat fmt) {
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!WindowSelectionHasEntity(win, owner)) return 0;
    WindowSelection* selection = win->sel;
    if (AwaitingSelectionReflow(selection))
        return StrCopyCapped(out, cap, RetainedSelectionText(selection, fmt));
    return CopyTextHitsInEntity(&win->paint, selection->anchor,
                                selection->cursor, selection->scope, owner, out,
                                cap, fmt, AnchorCellOf(selection));
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
    for (int i = 0; i < len(win->paint.texts); i++) {
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
    bool textView = EntityHasDrop(win->app, owner, &EntityDropT<TextViewState>);
    if ((!found || first == last) && !textView) return;
    if (!found && activeScope >= 0) scope = activeScope;
    selection->scope = scope;
    if (selection->activeScope.raw == 0) {
        selection->activeScope = TextSelectionScopeId::FromRaw((uint64_t)scope);
    }
    selection->selectAllOwner = textView ? owner : EntityId{};
    selection->anchor = found ? first : -1;
    selection->cursor = found ? last : -1;
    selection->anchorPoint = {firstBounds.x, firstBounds.y};
    selection->cursorPoint = {lastBounds.x + lastBounds.w,
                              lastBounds.y + lastBounds.h};
    selection->hasWindowPoints = found;
    selection->anchorInsideText = found;
    selection->anchorParticipant = {};
    selection->hasAnchorCell = false;
    selection->unitHeld = false;
    TextSelectionBegin(&selection->gesture, true);
    TextSelectionEnd(&selection->gesture);
    WindowSelectionPublish(win);
}

void WindowSelectionSelectAllTouched(Window* win) {
    if (!win || !WindowSelectionHas(win) || !win->sel) {
        return;
    }
    WindowSelection* s = win->sel;
    EntityId owner = {};
    int lo = s->anchor < s->cursor ? s->anchor : s->cursor;
    int hi = s->anchor < s->cursor ? s->cursor : s->anchor;
    for (int i = 0; i < len(win->paint.texts); i++) {
        const TextHit& hit = win->paint.texts[i];
        if (s->scope && hit.scope != s->scope) {
            continue;
        }
        int hitEnd = hit.docOff + (hit.atom ? 1 : len(hit.text));
        if (hitEnd <= lo || hit.docOff >= hi) {
            continue;
        }
        if (hit.owner.IsValid()) {
            owner = hit.owner;
            break;
        }
    }
    if (!owner.IsValid() && len(s->participants) > 0) {
        owner = s->participants[0];
    }
    WindowSelectionSelectAll(win, owner);
    if (win->sel) {
        win->sel->touchMenuOpen = true;
    }
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
    return StrCopyCapped(out, cap, item.text);
}

static int SelectedTextInto(Window* window, App* app, char* out, int cap,
                            StrBuilder* paintedText) {
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    WindowSelection* selection = window ? window->sel : nullptr;
    if (!selection || !app) return 0;
    if (const TextViewState* view = SelectAllTextView(window)) {
        return StrCopyCapped(out, cap,
                             view->SelectAllText(view->selectionFormat));
    }
    Vec<ParticipantCopyItem> active;
    for (int i = 0; i < len(selection->participants); i++) {
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
    // A TextView full replacement leaves the previous runs painted. Copy
    // must not read them until the replacement's parse has committed.
    if (selection && app && WindowSelectionHas(window)) {
        int lo = selection->anchor < selection->cursor ? selection->anchor
                                                       : selection->cursor;
        int hi = selection->anchor < selection->cursor ? selection->cursor
                                                       : selection->anchor;
        for (int i = 0; i < len(window->paint.texts); i++) {
            const TextHit& hit = window->paint.texts[i];
            if (!SelectionContainsHit(hit, lo, hi, selection->scope)) {
                continue;
            }
            const TextViewState* view =
                TextViewForSelectionOwner(app, hit.owner);
            if (view && !view->CopyDocumentReady()) {
                out[0] = 0;
                return 0;
            }
        }
    }
    if (paintedText && !selection->touchSource.copy &&
        !selection->selectAllOwner.IsValid() && WindowSelectionHas(window)) {
        return CopyTextHitsIn(&window->paint, selection->anchor,
                              selection->cursor, selection->scope, paintedText,
                              WindowSelectionFormat(window),
                              AnchorCellOf(selection));
    }
    return WindowSelectionText(window, out, cap);
}

int TextSelection::SelectedText(Window* window, App* app, char* out, int cap) {
    return SelectedTextInto(window, app, out, cap, nullptr);
}

bool TextSelection::HasSelection(Window* window, const App* app) {
    if (WindowSelectionHas(window) || RetainedSelectionView(window))
        return true;
    const WindowSelection* selection = window ? window->sel : nullptr;
    if (!selection || !app) return false;
    for (int i = 0; i < len(selection->participants); i++) {
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
    const WindowSelection* s = win ? win->sel : nullptr;
    if (!s) return SelectionFormat::Plain;
    if (const TextViewState* view = RetainedSelectionView(win))
        return view->selectionFormat;
    if (s->touchSource.copy || !WindowSelectionHas(win)) return s->format;

    EntityId owner = s->selectAllOwner;
    if (!owner.IsValid()) {
        int lo = std::min(s->anchor, s->cursor);
        int hi = std::max(s->anchor, s->cursor);
        for (const TextHit& hit : win->paint.texts) {
            if (hit.docOff >= hi) break;
            if (!SelectionContainsHit(hit, lo, hi, s->scope)) continue;
            if (!hit.owner.IsValid() ||
                (owner.IsValid() && hit.owner != owner)) {
                return s->format;
            }
            owner = hit.owner;
        }
    }
    if (const TextViewState* view =
            TextViewForSelectionOwner(win->app, owner)) {
        return view->format == TextViewFormat::Markdown
                   ? view->selectionFormat
                   : SelectionFormat::Plain;
    }
    return s->format;
}

bool WindowSelectionCopy(Window* win, bool trim) {
    if (!TextSelection::HasSelection(win, win ? win->app : nullptr)) {
        return false;
    }
    auto write = [&](Str text) {
        if (trim) text = StrTrim(text);
        if (!len(text)) return false;
        ClipboardSetText(win, text);
        return true;
    };
    const WindowSelection* selection = win->sel;
    if (selection->touchSource.copy) {
        TouchSelectionSource source = selection->touchSource;
        if (void* receiver = EntityGet(win->app, source.entity)) {
            return write(source.copy(receiver, win, GetTempArena()));
        }
    }
    if (const TextViewState* view = SelectAllTextView(win)) {
        return write(view->SelectAllText(view->selectionFormat));
    }
    if (const TextViewState* view = RetainedSelectionView(win)) {
        return write(RetainedSelectionText(selection, view->selectionFormat));
    }
    // Participant copy callbacks still use the bounded copy API.
    bool usesCallbacks = len(selection->participants) > 0;
    int cap = usesCallbacks ? 64 * 1024 : 1;
    char scratch[1];
    char* buf = usesCallbacks ? (char*)Alloc(nullptr, cap) : scratch;
    if (!buf) {
        return false;
    }
    StrBuilder paintedText;
    int n = SelectedTextInto(win, win->app, buf, cap, &paintedText);
    bool copied =
        n > 0 && write(Str(len(paintedText) ? paintedText.els : buf, n));
    if (usesCallbacks) Free(nullptr, buf);
    return copied;
}

void WindowSelectionApply(Window* win) {
    if (!win) {
        return;
    }
    WindowSelection* s = win->sel;
    // did_hit_text gates the whole thing: a gesture that never touched a
    // glyph shows nothing, however far it dragged.
    bool publishes =
        s && TextSelectionPublishes(&s->gesture) && !AwaitingSelectionReflow(s);
    win->paint.selA = publishes ? s->anchor : -1;
    win->paint.selB = publishes ? s->cursor : -1;
    win->paint.selScope = publishes ? s->scope : -1;
    win->paint.selPoints = publishes && s->hasWindowPoints;
    if (win->paint.selPoints) {
        win->paint.selX0 = s->anchorPoint.x;
        win->paint.selY0 = s->anchorPoint.y;
        win->paint.selX1 = s->cursorPoint.x;
        win->paint.selY1 = s->cursorPoint.y;
    }
}

// Rust keeps a participant that missed the generation while its element's
// retained state (TextSelectionRegistration::with_rendered_element) is alive,
// because Entity::cached replays a frame without painting it. This runtime has
// no cached-view replay: every element on screen paints, and re-registers,
// each frame, so a missed generation always means the element is gone.
bool WindowSelectionLayerLive(const Window* win) {
    // Rendered in the frame being built (layerFrame == frameSeq + 1) or in
    // the one before it, whose frame has since ended.
    return win && win->sel && win->sel->layerFrame != 0 &&
           win->sel->layerFrame >= win->frameSeq;
}

static bool SelectionIdListed(const Vec<EntityId>& ids, EntityId id) {
    if (!id.IsValid()) {
        return false;
    }
    for (int i = 0; i < len(ids); i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

void WindowSelectionFinishFrame(Window* win) {
    WindowSelection* selection = win ? win->sel : nullptr;
    if (selection) {
        if (selection->touchSource.entity.IsValid() &&
            selection->touchSourceFrame <= win->frameSeq) {
            WindowSelectionClear(win);
        }
        if (!selection->touchUiThisFrame) {
            selection->hasTouchUi = false;
        }
        selection->touchUiThisFrame = false;
    }
    if (!selection || !win->app) return;
    if (selection->autoScrollRelayout) {
        selection->autoScrollRelayout = false;
        Point at = selection->autoScrollPointer;
        WindowSelectionDragAt(win, at.x, at.y);
        WindowSelectionApply(win);
    }
    if (selection->autoScroll.IsActive()) WindowRequestAnimationFrame(win);
    // Rust collects stale ids before clearing their entities. Event and
    // cleanup callbacks can re-enter selection code, so do not walk the live
    // participant vector while invoking them.
    Vec<EntityId> participants;
    Vec<EntityId> stale;
    for (int i = 0; i < len(selection->participants); i++) {
        EntityId id = selection->participants[i];
        VecAppend(participants, id);
        TextSelectionParticipantState* participant =
            (TextSelectionParticipantState*)EntityGet(win->app, id);
        if (participant && participant->window == win &&
            participant->registrationGeneration == selection->frameGeneration) {
            continue;
        }
        VecAppend(stale, id);
    }
    // An endpoint that left this frame owns the gesture. Clearing only that
    // participant's snapshot would republish the window's anchor and cursor
    // when the same retained entity registered again.
    bool endpointDeparted =
        SelectionIdListed(stale, selection->anchorParticipant);
    if (!endpointDeparted && selection->hasWindowPoints) {
        EntityId cursorId = {};
        ParticipantAt(win, selection->cursorPoint, &cursorId);
        endpointDeparted = SelectionIdListed(stale, cursorId);
    }
    if (endpointDeparted) {
        WindowSelectionClear(win);
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
            participant->registered = false;
            participant->window = nullptr;
            ParticipantDropSelection(win, participant,
                                     Entity<TextSelectionParticipantState>{id});
        }
    }
    VecReset(participants);
    selection->frameGeneration++;
    RefreshTouchGeometry(win, selection);
    WindowSelectionPublish(win);
}
} // namespace gpui
