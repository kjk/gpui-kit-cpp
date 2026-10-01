/* Ported from crates/base/src/text_selection.rs.
 *
 * did_hit_text is the rule the module's mouse handling turns on, and
 * `blank_only_drag_never_publishes_or_copies_selection` is the case that pins
 * it. The rest of that module is participant registration and cross-view
 * projection, which the runtime here does with one document order over the
 * frame's text runs. */

#include "Test.h"

static void ADragThatNeverTouchesTextPublishesNothing() {
    TextSelectionGesture g;
    TextSelectionBegin(&g, false);
    TextSelectionExtend(&g, false);
    TextSelectionExtend(&g, false);
    TextSelectionEnd(&g);
    utassert(!TextSelectionPublishes(&g));
}

static void StartingInTheMarginAndDraggingOntoTextSelects() {
    // Rust takes the flag from `anchor.inside_text || endpoint.inside_text`,
    // so a press beside a paragraph still begins something.
    TextSelectionGesture g;
    TextSelectionBegin(&g, false);
    utassert(!TextSelectionPublishes(&g));
    TextSelectionExtend(&g, true);
    utassert(TextSelectionPublishes(&g));
}

static void OnceItHasTouchedTextItStays() {
    // |=, never cleared mid-gesture: dragging back off into the margin does
    // not throw away what was selected.
    TextSelectionGesture g;
    TextSelectionBegin(&g, true);
    TextSelectionExtend(&g, false);
    TextSelectionExtend(&g, false);
    utassert(TextSelectionPublishes(&g));
    // And it outlives the release, so it can still be copied.
    TextSelectionEnd(&g);
    utassert(!g.selecting);
    utassert(TextSelectionPublishes(&g));
}

static void AFreshGestureStartsOver() {
    TextSelectionGesture g;
    TextSelectionBegin(&g, true);
    TextSelectionEnd(&g);
    // Rust assigns rather than ORs on the press, so the last drag's hit does
    // not carry into this one.
    TextSelectionBegin(&g, false);
    utassert(!TextSelectionPublishes(&g));
}

static void ExtendingWithoutAGestureDoesNothing() {
    TextSelectionGesture g;
    // A move with no button down is not part of a selection.
    TextSelectionExtend(&g, true);
    utassert(!TextSelectionPublishes(&g));
}

static void ClearingDropsBoth() {
    TextSelectionGesture g;
    TextSelectionBegin(&g, true);
    TextSelectionClear(&g);
    utassert(!g.selecting);
    utassert(!TextSelectionPublishes(&g));
}

// ─── the window's selection ───────────────────────────────────────────────
//
// WindowSelectionState over the frame's registered runs. A window is a plain
// struct, so a test can stand one up with hand-built TextHits — the
// registrations a real frame collects as it paints — and drive the same
// press / drag / release the runtime calls.

// Register a run: `y` is its row, and the text is one line 100 wide.
static void AddRun(Window* win, float y, const char* text, int scope) {
    TextHit h;
    h.bounds = {20, y, 100, 20};
    h.text = Str((char*)text);
    h.font = 14;
    h.maxW = 100;
    h.docOff = win->paint.textDocLen;
    h.scope = scope;
    VecAppend(win->paint.texts, h);
    // The gap of one, which is where CopyTextHits puts the newline between
    // two runs.
    win->paint.textDocLen += len(h.text) + 1;
}

static void AWindowWithNoTextSelectsNothing() {
    Window win;
    WindowSelectionPress(&win, 5, 5, 1, false);
    utassert(!WindowSelectionHas(&win));
    WindowSelectionFree(&win);
}

// A press that lands on no run at all drops what was selected: the outside
// click that clears a selection.
static void APressOffTextClearsIt() {
    Window win;
    AddRun(&win, 0, "hello", 0);
    AddRun(&win, 40, "world", 0);
    WindowSelectionPress(&win, 30, 5, 1, false);
    WindowSelectionDrag(&win, 30, 45);
    WindowSelectionRelease(&win);
    utassert(WindowSelectionHas(&win));
    // Far below both runs, and not nearest-clamped: nothing is there.
    WindowSelectionPress(&win, 500, 500, 1, false);
    utassert(!WindowSelectionHas(&win));
    WindowSelectionFree(&win);
}

// The whole point of a window-wide selection: a drag that starts in one run
// and ends in another covers both, with a newline where the runs meet.
static void ADragAcrossTwoRunsCopiesBoth() {
    Window win;
    AddRun(&win, 0, "hello", 0);
    AddRun(&win, 40, "world", 0);
    WindowSelectionPress(&win, 25, 5, 1, false);
    WindowSelectionDrag(&win, 115, 45);
    WindowSelectionRelease(&win);
    utassert(WindowSelectionHas(&win));
    TempStr buf = AllocStrTemp(63);
    int n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(n > 0);
    // Without a text backend a hit resolves to the start of its run, so what
    // is pinned here is the span and the join, not the glyph the drag ended
    // on: the first run, the newline between them, and into the second.
    utassert(StrEq(Str(buf.s, 6), StrL("hello\n")));
    WindowSelectionFree(&win);
}

// points_for_multi_click: two clicks take the word under the pointer, three
// the whole run. The gesture is over when the press returns — the unit was
// asked for outright — so a drag after one does not extend it.
static void TwoClicksTakeTheWordAndThreeTheLine() {
    Window win;
    AddRun(&win, 0, "hello brave world", 0);
    AddRun(&win, 40, "second", 0);
    TempStr buf = AllocStrTemp(63);

    // Without a text backend a hit resolves to the start of its run, so the
    // word this lands on is the first one.
    WindowSelectionPress(&win, 25, 5, 2, false);
    utassert(WindowSelectionHas(&win));
    int n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(StrEq(Str(buf.s, n), StrL("hello")));
    // The press ended the gesture, so a drag does not grow it.
    WindowSelectionDrag(&win, 115, 45);
    n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(StrEq(Str(buf.s, n), StrL("hello")));

    WindowSelectionPress(&win, 25, 5, 3, false);
    n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(StrEq(Str(buf.s, n), StrL("hello brave world")));
    // And it stops at the run: the line is this run's, not the document's.
    WindowSelectionFree(&win);
}

static void ALongPressTakesAWordAndKeepsDragging() {
    Window win;
    AddRun(&win, 0, "quick select value", 0);
    AddRun(&win, 40, "later", 0);
    Point start = {25, 5};
    PlatformInput began = InputLongPress(TouchPhase::Started, start, start);
    WindowDispatchInput(&win, &began);
    TempStr buf = AllocStrTemp(63);
    int n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(StrEq(Str(buf.s, n), StrL("quick")));
    utassert(win.longPressSelection);

    PlatformInput moved =
        InputLongPress(TouchPhase::Moved, start, Point{115, 45});
    WindowDispatchInput(&win, &moved);
    PlatformInput ended =
        InputLongPress(TouchPhase::Ended, start, Point{115, 45});
    WindowDispatchInput(&win, &ended);
    n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(n > 5 && StrStartsWith(Str(buf.s, n), StrL("quick")));
    utassert(!win.longPressSelection);
    TouchSelectionSnapshot snap = {};
    utassert(WindowSelectionTouchSnapshot(&win, &snap) && snap.menuOpen);
    WindowSelectionCloseEditMenu(&win);
    utassert(WindowSelectionTouchSnapshot(&win, &snap) && !snap.menuOpen);
    WindowSelectionFree(&win);
}

struct TouchHostWheel {
    int n = 0;
    TouchPhase last = TouchPhase::Moved;
    static void OnWheel(TouchHostWheel* self, Ctx*,
                        const ScrollWheelEvent* ev) {
        if (!ev) {
            return;
        }
        self->n++;
        self->last = ev->phase;
    }
};

static void AHostFingerSwipeEmitsPhasedScroll() {
    App app;
    Window win;
    win.app = &app;
    Entity<TouchHostWheel> probe = EntityNewState<TouchHostWheel>(&app);
    WindowOnScrollWheel(&win, ListenTo(probe, &TouchHostWheel::OnWheel));
    WindowTouchBegin(&win, 40, 10);
    utassert(win.touchHost == TouchHostKind::Pending);
    WindowTouchMove(&win, 40, 40);
    utassert(win.touchHost == TouchHostKind::Scroll);
    TouchHostWheel* p = probe.Get(&app);
    utassert(p && p->n >= 1);
    WindowTouchEnd(&win, 40, 50);
    utassert(p->last == TouchPhase::Ended);
    utassert(win.touchHost == TouchHostKind::None);
    EntityDrop(&app, probe.id);
}

static void AHostLongPressTimerSelectsAWord() {
    Window win;
    AddRun(&win, 0, "quick select value", 0);
    WindowTouchBegin(&win, 25, 5);
    WindowTouchPoll(&win, TimeNow() + 1);
    utassert(win.touchHost == TouchHostKind::LongPress);
    utassert(win.longPressSelection);
    TempStr buf = AllocStrTemp(31);
    int n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(StrEq(Str(buf.s, n), StrL("quick")));
    WindowTouchEnd(&win, 25, 5);
    TouchSelectionSnapshot snap = {};
    utassert(WindowSelectionTouchSnapshot(&win, &snap) && snap.menuOpen);
    WindowSelectionFree(&win);
}

static void AHostHandleDragStartsOnTheSelectionHandle() {
    Window win;
    AddRun(&win, 0, "quick select value", 0);
    WindowTouchBegin(&win, 25, 5);
    WindowTouchPoll(&win, TimeNow() + 1);
    WindowTouchEnd(&win, 25, 5);
    TouchSelectionSnapshot snap = {};
    utassert(WindowSelectionTouchSnapshot(&win, &snap) && snap.menuOpen);

    WindowTouchBegin(&win, 25, 5);
    utassert(win.touchHost == TouchHostKind::HandleDrag);
    utassert(win.sel && win.sel->hasTouchEdgeDrag);
    utassert(win.sel && !win.sel->touchMenuOpen);

    WindowTouchMove(&win, 80, 5);
    WindowTouchEnd(&win, 80, 5);
    utassert(win.touchHost == TouchHostKind::None);
    utassert(win.sel && !win.sel->hasTouchEdgeDrag);
    utassert(WindowSelectionTouchSnapshot(&win, &snap) && snap.menuOpen);
    WindowSelectionFree(&win);
}

static void ADoubleTapOnReadOnlyTextSelectsNothing() {
    App app;
    Window win;
    win.app = &app;
    AddRun(&win, 0, "quick select value", 0);
    Point at = {25, 5};
    PlatformInput touch = InputTouchDrag(TouchPhase::Started, at, at);
    WindowDispatchInput(&win, &touch);
    PlatformInput down =
        InputMouseDown(MouseButton::Left, at.x, at.y, {}, 2, false);
    WindowDispatchInput(&win, &down);
    Ctx cx = {&app, &win, nullptr, {}};
    utassert(WindowIsTouchPress(&cx));
    TempStr buf = AllocStrTemp(31);
    int n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(n == 0);
    WindowSelectionFree(&win);
}

// A multi-click off any run leaves what was selected alone rather than
// clearing it: `TextMultiClickRangeIn` answers false and the press falls
// through to the single-click path, which is a press in the margin.
static void AMultiClickOffTextTakesNothing() {
    Window win;
    AddRun(&win, 0, "hello", 0);
    WindowSelectionPress(&win, 500, 500, 2, false);
    utassert(!WindowSelectionHas(&win));
    WindowSelectionFree(&win);
}

// TextSelectionScopeId: a gesture that began inside a trap stays there, so a
// drag out of a dialog does not take the page behind it.
static void ADragOutOfAScopeStaysInIt() {
    Window win;
    const int kDialog = 7;
    AddRun(&win, 0, "page", 0);
    AddRun(&win, 40, "dialog", kDialog);
    WindowSelectionPress(&win, 25, 45, 1, false);
    utassert(win.sel->scope == kDialog);
    // Over the page's run, which is in another scope: the cursor does not
    // follow it there.
    WindowSelectionDrag(&win, 115, 5);
    WindowSelectionRelease(&win);
    TempStr buf = AllocStrTemp(63);
    int n = WindowSelectionText(&win, buf.s, len(buf) + 1);
    utassert(n == 0 || !StrEq(Str(buf.s, n), StrL("page")));
    // And the frame is told which scope the range belongs to, so a run
    // outside it does not paint one.
    WindowSelectionApply(&win);
    utassert(win.paint.selScope == kDialog);
    WindowSelectionFree(&win);
}

// did_hit_text again, this time through the window: a drag that only ever
// touched the margin publishes nothing, so there is nothing to copy.
static void AMarginOnlyDragPublishesNothing() {
    Window win;
    AddRun(&win, 0, "hello", 0);
    // Well below the run: found only because the press clamps to the
    // nearest, never because it was on a glyph.
    WindowSelectionPress(&win, 25, 200, 1, false);
    WindowSelectionDrag(&win, 40, 220);
    WindowSelectionRelease(&win);
    utassert(!WindowSelectionHas(&win));
    TempStr buf = AllocStrTemp(15);
    utassert(WindowSelectionText(&win, buf.s, len(buf) + 1) == 0);
    WindowSelectionApply(&win);
    utassert(win.paint.selA < 0);
    WindowSelectionFree(&win);
}

// A shift-click moves the cursor and keeps the anchor — Rust's
// begin_in_window(.., extend).
static void ShiftClickExtendsFromTheAnchor() {
    Window win;
    AddRun(&win, 0, "hello", 0);
    AddRun(&win, 40, "world", 0);
    WindowSelectionPress(&win, 25, 5, 1, false);
    WindowSelectionRelease(&win);
    int anchor = win.sel->anchor;
    WindowSelectionPress(&win, 25, 45, 1, true);
    utassert(win.sel->anchor == anchor);
    utassert(win.sel->cursor != anchor);
    WindowSelectionFree(&win);
}

static void AControlPressSuppressesWindowSelection() {
    App app = {};
    Window win;
    win.app = &app;
    AddRun(&win, 0, "hello", 0);
    AddRun(&win, 40, "world", 0);
    WindowSelectionPress(&win, 25, 5, 1, false);
    WindowSelectionDrag(&win, 25, 45);
    WindowSelectionRelease(&win);
    utassert(WindowSelectionHas(&win));

    BaseSuppressTextSelection(&app);
    WindowSelectionPress(&win, 25, 5, 1, false);
    utassert(!WindowSelectionHas(&win));

    BaseResetTextSelectionSuppression(&app);
    WindowSelectionPress(&win, 25, 5, 1, false);
    utassert(win.sel && win.sel->gesture.selecting);
    WindowSelectionFree(&win);
    AppGlobalClear(&app);
}

struct SelectionParticipantHarness {
    int changed = 0;
    int cleared = 0;
    int autoScroll = 0;
    int focused = 0;
    int clearCallbacks = 0;

    static El* Render(SelectionParticipantHarness*, Ctx* cx) {
        return Div(cx->a);
    }

    static void OnEvent(SelectionParticipantHarness* self, Ctx*,
                        const TextSelectionEvent* event) {
        if (event->kind == TextSelectionEventKind::SelectionChanged) {
            self->changed++;
        } else if (event->kind == TextSelectionEventKind::Cleared) {
            self->cleared++;
        } else if (event->kind == TextSelectionEventKind::AutoScroll) {
            self->autoScroll++;
        }
    }
};

static void ParticipantFocus(void* user, Window*, App*) {
    ((SelectionParticipantHarness*)user)->focused++;
}

static void ParticipantClear(void* user, App*) {
    ((SelectionParticipantHarness*)user)->clearCallbacks++;
}

static bool ParticipantContentKey(void*, Point point, const App*,
                                  TextSelectionContentKey* out) {
    *out = TextSelectionContentKey::New((uint64_t)(point.y + 100));
    return true;
}

static int ParticipantCopy(void*, App*, char* out, int cap) {
    const char* value = "custom";
    int n = std::min(6, cap > 0 ? cap - 1 : 0);
    if (n > 0) memcpy(out, value, (size_t)n);
    if (cap > 0) out[n] = 0;
    return n;
}

static void SourceParticipantContractsProjectAcrossAWindow() {
    App app = {};
    Window win;
    win.app = &app;
    AddRun(&win, 0, "first", 0);
    AddRun(&win, 40, "second", 0);
    Entity<SelectionParticipantHarness> harness =
        EntityNew<SelectionParticipantHarness>(&app);
    SelectionParticipantHarness* observed = harness.Get(&app);
    Arena* arena = ArenaNew();
    Ctx cx = {&app, &win, arena, harness.id};

    TextSelectionScopeId one = TextSelectionScopeId::New();
    TextSelectionScopeId two = TextSelectionScopeId::New();
    utassert(one != two && one.Value() != 0);
    TextSelectionContentKey key = TextSelectionContentKey::New(77);
    TextSelectionEndpoint endpoint =
        TextSelectionEndpoint::New(harness.id, {3, 4}).WithContentKey(key);
    TextSelectionSnapshot built =
        TextSelectionSnapshot::New(endpoint, TextSelectionEndpoint::At({8, 9}))
            .WithSelecting(true)
            .WithWindowPoints(
                TextSelectionWindowPoints::New({10, 11}, {12, 13}))
            .WithCoverage(TextSelectionCoverage::ToEnd);
    utassert(endpoint.hasEntity && endpoint.hasContentKey &&
             endpoint.contentKey.Value() == 77);
    utassert(built.IsSelecting() && built.hasWindowPoints &&
             built.Coverage() == TextSelectionCoverage::ToEnd);

    Bounds firstText[] = {{20, 0, 100, 20}};
    TextSelectionRegistration firstRegistration =
        TextSelectionRegistration::New({20, 0, 100, 20}, {20, 0, 100, 20})
            .WithDocumentOrder(10)
            .WithTextBounds(firstText, 1);
    TextSelectionRegistration secondRegistration =
        TextSelectionRegistration::New({20, 40, 100, 20}, {20, 40, 100, 20})
            .WithDocumentOrder(20)
            .WithScrollOffset({0, 2});
    utassert(firstRegistration.documentOrder == 10 &&
             firstRegistration.textBoundsCount == 1);
    utassert(secondRegistration.scrollOffset.y == 2);

    TextSelectionHandle first = TextSelectionHandle::New(StrL("first"), &app);
    TextSelectionHandle second = TextSelectionHandle::New(StrL("second"), &app);
    TextSelectionHandle outside =
        TextSelectionHandle::New(StrL("outside"), &app);
    first.Subscribe(&cx, &SelectionParticipantHarness::OnEvent);
    second.Subscribe(&cx, &SelectionParticipantHarness::OnEvent);
    outside.Subscribe(&cx, &SelectionParticipantHarness::OnEvent);
    Subscription refresh = first.RefreshWindowOnChange(&app);
    utassert(refresh.IsValid());
    first.FocusWith(&ParticipantFocus, observed, &app);
    first.ClearWith(&ParticipantClear, observed, &app);
    second.ClearWith(&ParticipantClear, observed, &app);
    outside.ClearWith(&ParticipantClear, observed, &app);
    first.ResolveContentKeyWith(&ParticipantContentKey, nullptr, &app);
    second.ResolveContentKeyWith(&ParticipantContentKey, nullptr, &app);
    first.Register(firstRegistration, &win, &app);
    second.Register(secondRegistration, &win, &app);
    outside.Register(
        TextSelectionRegistration::New({20, 80, 100, 20}, {20, 80, 100, 20})
            .WithDocumentOrder(30),
        &win, &app);

    WindowSelectionPress(&win, 25, 5, 1, false);
    WindowSelectionDrag(&win, 25, 45);
    TextSelectionSnapshot firstSnapshot;
    TextSelectionSnapshot secondSnapshot;
    utassert(first.Snapshot(&app, &firstSnapshot));
    utassert(second.Snapshot(&app, &secondSnapshot));
    utassert(!outside.Snapshot(&app, nullptr));
    utassert(firstSnapshot.Coverage() == TextSelectionCoverage::ToEnd);
    utassert(secondSnapshot.Coverage() == TextSelectionCoverage::FromStart);
    utassert(firstSnapshot.Anchor().entity == first.Entity());
    utassert(firstSnapshot.Cursor().entity == second.Entity());
    utassert(firstSnapshot.Anchor().hasContentKey && firstSnapshot.Cursor()
                                                         .hasContentKey);
    utassert(observed->focused == 1 && observed->autoScroll > 0);

    TempStr selected = AllocStrTemp(63);
    int selectedLen =
        TextSelection::SelectedText(&win, &app, selected.s, len(selected) + 1);
    utassert(StrEq(Str(selected.s, selectedLen), StrL("first\nsecond")));
    utassert(TextSelection::HasSelection(&win, &app));
    WindowSelectionRelease(&win);
    utassert(first.Snapshot(&app, &firstSnapshot) && !firstSnapshot
                                                          .IsSelecting());
    outside.CopyWith(&ParticipantCopy, nullptr, &app);
    outside.SetLocalSelection(true, &app);
    selectedLen =
        TextSelection::SelectedText(&win, &app, selected.s, len(selected) + 1);
    utassert(
        StrEq(Str(selected.s, selectedLen), StrL("first\nsecond\ncustom")));
    outside.SetLocalSelection(false, &app);

    TextSelectionRun run =
        TextSelectionRun::New(StrL("middle"), nullptr, {0, 0, 40, 20})
            .WithDocumentOrder(5);
    TextSelectionProjection projection = first.UpdateRuns(&run, 1, &app);
    utassert(projection.IsActive() && projection.Len() == 1);
    projection.Reset();

    TextSelection::Clear(&win, &app);
    utassert(!TextSelection::HasSelection(&win, &app));
    // Three participants, each cleared twice: by the press that began the
    // gesture (prepare_for_mouse_down) and by this Clear.
    utassert(observed->cleared == 6 && observed->clearCallbacks == 6);

    El* layer = TextSelectionLayer::New(&cx);
    El* scoped = TextSelectionScope(Div(arena), one);
    utassert(base::StrEq(layer->id, StrL("window-text-selection")));
    utassert(scoped->style.trapId == one.RuntimeScope());

    WindowSelectionFree(&win);
    ArenaDelete(arena);
    EntityDropAll(&app);
}

// window_selection.rs selection_inside_a_cached_view_survives_replayed_frames
// has no counterpart: it needs Entity::cached frame replay, which this runtime
// does not have (see WindowSelectionFinishFrame). The sweep below is the
// behaviour a non-cached participant gets in both trees.
static void FrameSweepDropsOnlyRegistrationsNotRenewed() {
    App app = {};
    Window win;
    win.app = &app;
    Entity<SelectionParticipantHarness> harness =
        EntityNew<SelectionParticipantHarness>(&app);
    SelectionParticipantHarness* observed = harness.Get(&app);
    Arena* arena = ArenaNew();
    Ctx cx = {&app, &win, arena, harness.id};

    TextSelectionHandle current =
        TextSelectionHandle::New(StrL("current"), &app);
    TextSelectionHandle stale = TextSelectionHandle::New(StrL("stale"), &app);
    current.Subscribe(&cx, &SelectionParticipantHarness::OnEvent);
    stale.Subscribe(&cx, &SelectionParticipantHarness::OnEvent);
    stale.ClearWith(&ParticipantClear, observed, &app);
    TextSelectionRegistration registration =
        TextSelectionRegistration::New({0, 0, 100, 20}, {0, 0, 100, 20});
    current.Register(registration.WithDocumentOrder(1), &win, &app);
    stale.Register(registration.WithDocumentOrder(2), &win, &app);
    stale.SetLocalSelection(true, &app);

    // The first post-paint sweep keeps registrations from that frame. Only
    // the participant painted again is present after the following frame.
    WindowSelectionFinishFrame(&win);
    utassert(win.sel->participants.len == 2);
    current.Register(registration.WithDocumentOrder(1), &win, &app);
    WindowSelectionFinishFrame(&win);
    utassert(win.sel->participants.len == 1 &&
             win.sel->participants[0] == current.Entity());
    utassert(!stale.HasLocalSelection(&app));
    utassert(observed->cleared == 1 && observed->changed == 1 &&
             observed->clearCallbacks == 1);

    WindowSelectionFree(&win);
    ArenaDelete(arena);
    EntityDropAll(&app);
}

// selectable_text.rs wrapped_selection_paints_full_width_middle_lines: a
// selection that spans lines is the tail of the first, the whole of the ones
// between and the head of the last.
static bool SameSelectionBounds(Bounds a, Bounds b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void WrappedSelectionPaintsFullWidthMiddleLines() {
    Bounds bounds = {10, 20, 100, 100};
    Bounds quads[3] = {};
    int n = SelectionQuadBounds({40, 20}, {30, 80}, bounds, 20, quads);
    utassert(n == 3);
    utassert(SameSelectionBounds(quads[0], {40, 20, 70, 20}));
    utassert(SameSelectionBounds(quads[1], {10, 40, 100, 40}));
    utassert(SameSelectionBounds(quads[2], {10, 80, 20, 20}));

    // One line is one quad, from the start to the end of the run.
    n = SelectionQuadBounds({40, 20}, {90, 20}, bounds, 20, quads);
    utassert(n == 1);
    utassert(SameSelectionBounds(quads[0], {40, 20, 50, 20}));

    // Two adjacent lines have no middle band between them.
    n = SelectionQuadBounds({40, 20}, {30, 40}, bounds, 20, quads);
    utassert(n == 2);
    utassert(SameSelectionBounds(quads[0], {40, 20, 70, 20}));
    utassert(SameSelectionBounds(quads[1], {10, 40, 20, 20}));
}

// selectable_text.rs explicit_handle_constructor_preserves_document_contract:
// a run built on a shared handle joins that document in the order it names,
// and a local one owns its own selection.
static void SelectableTextJoinsTheDocumentItsHandleOwns() {
    App app;
    Window win;
    win.app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, &win, arena, {}};

    TextSelectionHandle handle =
        TextSelectionHandle::New(StrL("alpha beta"), &app);
    SelectableText* shared = SelectableText::WithHandle(
        &cx, StrL("plain"), handle, StrL("alpha beta"));
    utassert(shared->hasHandle && shared->DocumentOrder(42)
                                          ->documentOrder == 42);
    El* joined = shared->IntoEl();
    utassert(joined && joined->selectable);
    utassert(joined && joined->selectionOwner == handle.Entity());

    SelectableText* local =
        SelectableText::New(&cx, StrL("local"), StrL("alpha beta"));
    utassert(!local->hasHandle);
    El* own = local->TextStyle(14, RgbaHex(0x171717))->IntoEl();
    utassert(own && own->selectable && !own->selectionOwner.IsValid());
    utassert(own && own->style.fontSize == 14);

    ArenaDelete(arena);
    WindowSelectionFree(&win);
    EntityDropAll(&app);
}

// text_selection.rs drag_auto_scroll_stops_when_the_content_mask_collapses:
// a scrollable ancestor clipped away mid-drag leaves an empty clamp range, so
// the drag must stop scrolling rather than run on the last delta.
struct AutoScrollObserver {
    int running = 0;
    int stopped = 0;

    static void OnEvent(AutoScrollObserver* self, Ctx*,
                        const TextSelectionEvent* event) {
        if (event->kind != TextSelectionEventKind::AutoScroll) {
            return;
        }
        if (event->hasAutoScroll) {
            self->running++;
        } else {
            self->stopped++;
        }
    }
};

static void DragAutoScrollStopsWhenTheContentMaskCollapses() {
    App app;
    Window win;
    win.app = &app;
    Arena* arena = ArenaNew();

    Entity<AutoScrollObserver> viewer =
        EntityNewState<AutoScrollObserver>(&app);
    AutoScrollObserver* observed = viewer.Get(&app);
    TextSelectionHandle handle = TextSelectionHandle::New(StrL("text"), &app);
    Ctx cx = {&app, &win, arena, viewer.id};
    Subscription sub = handle.Subscribe(&cx, &AutoScrollObserver::OnEvent);
    (void)sub;

    AddRun(&win, 0, "alpha beta", 0);
    Bounds visible = {0, 0, 100, 40};
    handle
        .Register(TextSelectionRegistration::New(visible, visible), &win, &app);
    WindowSelectionPress(&win, 1, 1, 1, false);
    WindowSelectionDrag(&win, 1, 60);
    utassert(observed->running > 0);

    // pointer_moves_after_a_click_do_not_auto_scroll: release keeps the
    // anchor for shift-click extension, but later movement must not scroll.
    WindowSelectionRelease(&win);
    int running = observed->running;
    WindowSelectionDrag(&win, 1, 60);
    utassert(observed->running == running);
    WindowSelectionPress(&win, 1, 1, 1, false);

    // The ancestor collapsed: the refreshed registration carries an empty
    // content mask, and the next drag stops the auto scroll.
    int before = observed->stopped;
    Bounds collapsed = {0, 0, 100, 0};
    handle.Register(TextSelectionRegistration::New(collapsed, collapsed), &win,
                    &app);
    WindowSelectionDrag(&win, 1, 60);
    utassert(observed->stopped > before);

    WindowSelectionFree(&win);
    ArenaDelete(arena);
    EntityDropAll(&app);
}

// text_selection.rs selection_range_for_run's fast paths (#3261), and the
// inline.rs layout_selections ones they share: a band that misses every row
// selects nothing, one strictly around all of them with no endpoint on a row
// selects everything, and anything else is left to the per-character walk.
// text_rows_extent_matches_the_character_walk needs shaped text; the extent
// here is the first and last character's rects, which bound every row.
static void SelectionBandDecidesWholeRuns() {
    // Rows from 100 to 140.
    utassert(TextSelectionBandFor(100, 140, {0, 20}, {50, 60}) ==
             TextSelectionBand::Misses);
    utassert(TextSelectionBandFor(100, 140, {0, 150}, {50, 200}) ==
             TextSelectionBand::Misses);
    // Touching the bottom edge is a miss; touching the top is not.
    utassert(TextSelectionBandFor(100, 140, {0, 140}, {50, 200}) ==
             TextSelectionBand::Misses);
    utassert(TextSelectionBandFor(100, 140, {0, 20}, {50, 100}) ==
             TextSelectionBand::Partial);
    utassert(TextSelectionBandFor(100, 140, {80, 99}, {10, 140}) ==
             TextSelectionBand::Covers);
    // Either order of the endpoints.
    utassert(TextSelectionBandFor(100, 140, {10, 300}, {80, 50}) ==
             TextSelectionBand::Covers);
    // An endpoint on the first row is decided per character.
    utassert(TextSelectionBandFor(100, 140, {10, 100}, {80, 300}) ==
             TextSelectionBand::Partial);
    utassert(TextSelectionBandFor(100, 140, {10, 120}, {80, 130}) ==
             TextSelectionBand::Partial);
}

// ─── text_selection.rs window tests ───────────────────────────────────────
//
// text_selection.rs mod tests, through the test platform (gpui/test_app.h).
//
// Rust's FakeParticipant is a 100×10 registration at (0, y) whose text bounds
// are the whole box and which copies its fallback text. Rust's gesture
// resolves points against the registrations; this window's gesture resolves
// them against the frame's selectable text runs, and the participants are
// what it publishes to. So a FakeParticipant here is both: a selectable run
// filling the same box, owned by the handle, and the handle registered with
// that box as the run renders. `begin` / `update` / `end` are the window's
// press, drag and release; `selected_text` and `has_selection` are
// TextSelection's, which read the participants first, as Rust's do.

namespace {
struct FakeParticipant {
    TextSelectionHandle selection = {};
    Str text = {};
    float y = 0;
    TextSelectionScopeId scope = {};
    uint64_t order = 0;
    bool shown = true;
};

// The view the participants render in. TextSelectionLayer is mounted unless
// a test takes it away.
struct ParticipantView {
    FakeParticipant parts[4];
    int n = 0;
    // FirstFrameScopedSelectionView: the scope this view activates while it
    // renders, 0 for none.
    uint64_t activateScope = 0;

    static El* Render(ParticipantView* self, Ctx* cx) {
        if (self->activateScope) {
            TextSelection::ActivateScope(
                TextSelectionScopeId::FromRaw(self->activateScope), cx->win,
                cx->app);
        }
        El* root = Div(cx->a)->SizeFull()->Child(TextSelectionLayer::New(cx));
        for (int i = 0; i < self->n; i++) {
            const FakeParticipant& p = self->parts[i];
            if (!p.shown) {
                continue;
            }
            Bounds box = {0, p.y, 100, 10};
            El* run = TextEl(cx->a, p.text)
                          ->Selectable()
                          ->SelectionOwner(p.selection.Entity())
                          ->W(100)
                          ->H(10);
            El* place = Div(cx->a)->Absolute()->Left(0)->Top(p.y)->Child(run);
            if (p.scope.raw) {
                TextSelectionScope(place, p.scope);
            }
            root->Child(place);
            p.selection.Register(TextSelectionRegistration::New(box, box)
                                     .WithScope(p.scope)
                                     .WithDocumentOrder(p.order)
                                     .WithTextBounds(&box, 1),
                                 cx->win, cx->app);
        }
        return root;
    }
};

// What a participant's subscription saw.
struct SelectionObserver {
    // SelectionChanged events, as whether each carried a snapshot.
    bool changed[16] = {};
    int nChanged = 0;
    int cleared = 0;
    // AutoScroll commands, as whether each carried a delta.
    bool scroll[64] = {};
    int nScroll = 0;
    // Set by a handler that reads the window's selection back while the
    // selection is publishing.
    bool reentered = false;
    bool sawSelectionFromHandler = false;

    static void OnEvent(SelectionObserver* self, Ctx* cx,
                        const TextSelectionEvent* event) {
        if (event->kind == TextSelectionEventKind::SelectionChanged) {
            if (self->nChanged < 16) {
                self->changed[self->nChanged++] = event->hasSnapshot;
            }
            if (event->hasSnapshot && cx->win) {
                // selection_state.update(cx, ..) from inside the callback.
                self->reentered = true;
                self->sawSelectionFromHandler =
                    TextSelection::HasSelection(cx->win, cx->app);
            }
        } else if (event->kind == TextSelectionEventKind::Cleared) {
            self->cleared++;
            if (cx->win) {
                (void)WindowSelectionHas(cx->win);
            }
        } else if (event->kind == TextSelectionEventKind::AutoScroll) {
            if (self->nScroll < 64) {
                self->scroll[self->nScroll++] = event->hasAutoScroll;
            }
        }
    }
    static El* Render(SelectionObserver*, Ctx* cx) { return Div(cx->a); }
};

struct SelectionFixture {
    App* app = nullptr;
    Window* win = nullptr;
    Entity<ParticipantView> view = {};
    char buf[256] = {};

    SelectionFixture() {
        app = TestAppNew();
        view = EntityNew<ParticipantView>(app);
        win = TestWindowOpen(app, view, 400, 300);
    }
    ~SelectionFixture() { TestAppFree(app); }
    ParticipantView* View() { return view.Get(app); }

    // FakeParticipant::new(text) and register(state, y, scope, order). The
    // registration happens as the run renders, so the frame is drawn.
    FakeParticipant* Add(const char* text, float y, uint64_t scope = 0,
                         uint64_t order = 0) {
        ParticipantView* v = View();
        FakeParticipant* p = &v->parts[v->n++];
        p->selection = TextSelectionHandle::New(Str(text), app);
        p->text = Str(text);
        p->y = y;
        p->scope = TextSelectionScopeId::FromRaw(scope);
        p->order = order;
        Draw();
        return p;
    }
    void Draw() {
        AppInvalidate(win);
        TestDraw(win);
    }
    void Begin(float x, float y, bool extend = false) {
        WindowSelectionPress(win, x, y, 1, extend);
    }
    void Update(float x, float y) { WindowSelectionDrag(win, x, y); }
    void End() { WindowSelectionRelease(win); }
    bool Has() { return TextSelection::HasSelection(win, app); }
    bool SelectedIs(const char* want) {
        int n = TextSelection::SelectedText(win, app, buf, (int)sizeof(buf));
        return StrEq(Str(buf, n), Str(want));
    }
    bool Snapshot(const FakeParticipant* p, TextSelectionSnapshot* out) {
        return p->selection.Snapshot(app, out);
    }
    Entity<SelectionObserver> Observe(const FakeParticipant* p) {
        Entity<SelectionObserver> observer = EntityNew<SelectionObserver>(app);
        SubscribeTo(app, p->selection.state, observer,
                    &SelectionObserver::OnEvent);
        return observer;
    }
};

// laid_out_runs: each text laid out as Rust's PlainRunLayoutView renders it,
// one run every 40px down the window, with the layout it was shaped into.
struct PlainRun {
    Str text = {};
    TextLayout* layout = nullptr;
    Bounds bounds = {};
};

const float kPlainRunFont = 16;

PlainRun LaidOutRun(Window* win, const char* text, int index) {
    PlainRun run;
    run.text = Str(text);
    Size size = {};
    run.layout = TextLayoutNew(&win->paint, run.text, kPlainRunFont, 0, false,
                               0, kLineHeight, &size);
    run.bounds = {0, (float)index * 40.f, size.w, size.h};
    return run;
}

void PlainRunFree(PlainRun* run) {
    if (run->layout) {
        TextLayoutRelease(run->layout);
    }
    run->layout = nullptr;
}

// TextLayout::position_for_index, in window coordinates: the top left of the
// character at `index`, or the end of the last one for the length.
Point PositionForIndex(const PlainRun& run, int index) {
    Bounds rect = {};
    int n = len(run.text);
    if (index < n) {
        uint32_t cp = 0;
        int bytes = Utf8At(run.text, index, &cp);
        if (TextLayoutRangeRects(run.layout, run.text, index, index + bytes,
                                 &rect, 1) < 1) {
            return {run.bounds.x, run.bounds.y};
        }
        return {run.bounds.x + rect.x, run.bounds.y + rect.y};
    }
    int last = Utf8Prev(run.text, n);
    if (n == 0 ||
        TextLayoutRangeRects(run.layout, run.text, last, n, &rect, 1) < 1) {
        return {run.bounds.x, run.bounds.y};
    }
    return {run.bounds.x + rect.x + rect.w, run.bounds.y + rect.y};
}

TextSelectionRun TextRun(uint64_t order, const PlainRun& run) {
    return TextSelectionRun::New(run.text, run.layout, run.bounds)
        .WithDocumentOrder(order);
}

TextSelectionSnapshot PlainSnapshot(Point anchor, Point cursor) {
    return TextSelectionSnapshot::New(TextSelectionEndpoint::At(anchor),
                                      TextSelectionEndpoint::At(cursor))
        .WithWindowPoints(TextSelectionWindowPoints::New(anchor, cursor));
}

bool RangeIs(const TextSelectionProjection& p, int i, int start, int end) {
    return i < p.Len() && p.ranges[i].selected && p.ranges[i].start == start &&
           p.ranges[i].end == end;
}

bool RangeIsNone(const TextSelectionProjection& p, int i) {
    return i < p.Len() && !p.ranges[i].selected;
}

// The plain runs rendered in a window, each selectable, the way
// PlainRunLayoutView lays them out; points_for_multi_click is the window's
// multi-click over them.
struct PlainRunView {
    const char* texts[4] = {};
    int n = 0;

    static El* Render(PlainRunView* self, Ctx* cx) {
        El* root = Div(cx->a)->SizeFull();
        for (int i = 0; i < self->n; i++) {
            root->Child(Div(cx->a)
                            ->Absolute()
                            ->Left(0)
                            ->Top((float)i * 40.f)
                            ->Child(TextEl(cx->a, Str(self->texts[i]))
                                        ->Font(kPlainRunFont)
                                        ->Selectable()));
        }
        return root;
    }
};

// The frame's run for the n-th plain run, by its text.
const TextHit* PlainHit(Window* win, const char* text) {
    for (int i = 0; i < win->paint.texts.len; i++) {
        if (StrEq(win->paint.texts[i].text, Str(text))) {
            return &win->paint.texts[i];
        }
    }
    return nullptr;
}

// A multi-click at the character `index` of the run `text` in a window of
// `texts`; answers the selected range relative to that run, or false.
bool MultiClickRange(const char* const* texts, int n, const char* text,
                     int index, int clickCount, int* start, int* end,
                     float* runTop = nullptr) {
    App* app = TestAppNew();
    Entity<PlainRunView> view = EntityNew<PlainRunView>(app);
    for (int i = 0; i < n; i++) {
        view.Get(app)->texts[i] = texts[i];
    }
    view.Get(app)->n = n;
    Window* win = TestWindowOpen(app, view, 400, 300);
    const TextHit* hit = PlainHit(win, text);
    bool ok = false;
    if (hit) {
        if (runTop) {
            *runTop = hit->bounds.y;
        }
        PlainRun run = LaidOutRun(win, text, 0);
        run.bounds = hit->bounds;
        Point click = PositionForIndex(run, index);
        PlainRunFree(&run);
        WindowSelectionPress(win, click.x, click.y, clickCount, false);
        if (WindowSelectionHas(win)) {
            int a = std::min(win->sel->anchor, win->sel->cursor);
            int b = std::max(win->sel->anchor, win->sel->cursor);
            *start = a - hit->docOff;
            *end = b - hit->docOff;
            ok = true;
        }
    }
    TestAppFree(app);
    return ok;
}
} // namespace

// text_selection.rs scope_stack_is_cleaned_after_panicking_subtree: not
// ported — there is no scope stack to unwind. A scope here is the element's
// trap id (TextSelectionScope), carried down the tree as the frame is built,
// and the tree builds with no exceptions, so no subtree can panic out of one.

// text_selection.rs reentrant_scope_from_one_window_does_not_pollute_another:
// not ported — with_text_selection_scope / current_text_selection_scope have
// no counterpart; the scope is a property of the element, read per window as
// that window's frame collects its runs, so there is no shared stack to leak
// across windows.

// text_selection.rs selection_callback_can_reenter_its_selection_state.
static void SelectionCallbackCanReenterItsSelectionState() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("participant", 0);
    Entity<SelectionObserver> observer = f.Observe(participant);
    TestRunUntilParked(f.app);
    f.Begin(1, 1);
    f.Update(20, 1);
    TestRunUntilParked(f.app);
    utassert(observer.Get(f.app)->reentered);
    utassert(observer.Get(f.app)->sawSelectionFromHandler);
}

// text_selection.rs selection_events_preserve_snapshot_then_clear_order.
static void SelectionEventsPreserveSnapshotThenClearOrder() {
    SelectionFixture f;
    FakeParticipant* selection = f.Add("selection", 0);
    Entity<SelectionObserver> observer = f.Observe(selection);
    TestRunUntilParked(f.app);
    TextSelectionSnapshot snapshot = PlainSnapshot({1, 1}, {8, 1});
    TextSelectionHandleSetSnapshot(selection->selection, f.app, &snapshot);
    // state.clear_state: the participant's own clear, which the window's
    // clear runs for each participant.
    WindowSelectionClear(f.win);
    TestRunUntilParked(f.app);
    SelectionObserver* o = observer.Get(f.app);
    utassert(o->nChanged == 2);
    utassert(o->nChanged == 2 && o->changed[0] && !o->changed[1]);
}

// text_selection.rs public_selection_data_uses_builders_and_readers.
static void PublicSelectionDataUsesBuildersAndReaders() {
    Bounds bounds = {1, 2, 30, 10};
    TextSelectionScopeId scope = TextSelectionScopeId::FromRaw(7);
    TextSelectionEndpoint endpoint =
        TextSelectionEndpoint::At({bounds.x, bounds.y})
            .WithContentKey(TextSelectionContentKey::New(11));
    Point bottomRight = {bounds.x + bounds.w, bounds.y + bounds.h};
    TextSelectionSnapshot snapshot =
        TextSelectionSnapshot::New(endpoint, endpoint)
            .WithSelecting(true)
            .WithWindowPoints(TextSelectionWindowPoints::New(
                {bounds.x, bounds.y}, bottomRight))
            .WithCoverage(TextSelectionCoverage::Full);
    TextSelectionRegistration registration =
        TextSelectionRegistration::New(bounds, bounds)
            .WithScrollOffset({3, 4})
            .WithScope(scope)
            .WithDocumentOrder(9)
            .WithTextBounds(&bounds, 1);

    utassert(!endpoint.hasEntity && !endpoint.Entity().IsValid());
    utassert(endpoint.ContentPoint().x == 1 && endpoint.ContentPoint().y == 2);
    utassert(endpoint.hasContentKey &&
             endpoint.contentKey == TextSelectionContentKey::New(11));
    utassert(snapshot.Anchor().ContentPoint().x == endpoint.point.x);
    utassert(snapshot.Cursor().contentKey == endpoint.contentKey);
    utassert(snapshot.IsSelecting());
    utassert(snapshot.Coverage() == TextSelectionCoverage::Full);
    utassert(snapshot.hasWindowPoints &&
             snapshot.windowPoints.Anchor().x == bounds.x &&
             snapshot.windowPoints.Cursor().y == bottomRight.y);
    utassert(registration.bounds.x == bounds.x && registration.bounds
                                                          .w == bounds.w);
    utassert(registration.scrollOffset.x == 3 && registration.scrollOffset
                                                         .y == 4);
    utassert(registration.scope == scope);
    utassert(registration.documentOrder == 9);
    utassert(registration.textBoundsCount == 1 && registration.textBounds[0]
                                                          .w == bounds.w);

    App* app = TestAppNew();
    Entity<PlainRunView> view = EntityNew<PlainRunView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300);
    PlainRun laid = LaidOutRun(win, "a\xC3\xA9", 0);
    TextSelectionRun textRun =
        TextSelectionRun::New(laid.text, laid.layout, laid.bounds)
            .WithDocumentOrder(3);
    utassert(textRun.documentOrder == 3);
    utassert(StrEq(textRun.text, laid.text));
    utassert(textRun.layout == laid.layout);
    utassert(textRun.bounds.w == laid.bounds.w && textRun.bounds
                                                          .y == laid.bounds.y);
    PlainRunFree(&laid);
    TestAppFree(app);

    TextSelectionProjection projection;
    VecAppend(projection.ranges, TextSelectionRange{1, 3, true});
    projection.active = true;
    utassert(projection.Len() == 1 && RangeIs(projection, 0, 1, 3));
    utassert(projection.IsActive());
    projection.Reset();
}

static void NoFocus(void*, Window*, App*) {}

static int CopiedText(void*, App*, char* out, int cap) {
    const char* value = "copied";
    int n = std::min(6, cap > 0 ? cap - 1 : 0);
    memcpy(out, value, (size_t)n);
    if (cap > 0) {
        out[n] = 0;
    }
    return n;
}

static bool ContentKey3(void*, Point, const App*,
                        TextSelectionContentKey* out) {
    *out = TextSelectionContentKey::New(3);
    return true;
}

// text_selection.rs selection_handle_is_the_public_adapter_seam.
static void SelectionHandleIsThePublicAdapterSeam() {
    App* app = TestAppNew();
    Entity<SelectionObserver> observer = EntityNew<SelectionObserver>(app);
    TextSelectionHandle selection =
        TextSelectionHandle::New(StrL("initial"), app);
    EntityId entity = selection.Entity();
    selection.SetFallbackCopyText(StrL("updated"), app);
    selection.SetLocalSelection(true, app);
    SubscribeTo(app, selection.state, observer, &SelectionObserver::OnEvent);
    selection.FocusWith(&NoFocus, nullptr, app);
    selection.CopyWith(&CopiedText, nullptr, app);
    selection.ResolveContentKeyWith(&ContentKey3, nullptr, app);

    utassert(selection.Entity() == entity);
    utassert(!selection.Snapshot(app, nullptr));
    TextSelectionProjection none = selection.UpdateRuns(nullptr, 0, app);
    utassert(none.Len() == 0 && !none.IsActive());
    none.Reset();
    TestRunUntilParked(app);
    SelectionObserver* o = observer.Get(app);
    utassert(o->nChanged == 0 || !o->changed[o->nChanged - 1]);
    TestAppFree(app);
}

// text_selection.rs selection_handle_can_subscribe_its_window_to_refresh.
static void SelectionHandleCanSubscribeItsWindowToRefresh() {
    SelectionFixture f;
    TextSelectionHandle selection =
        TextSelectionHandle::New(StrL("refresh"), f.app);
    Subscription refresh = selection.RefreshWindowOnChange(f.app);
    utassert(refresh.IsValid());
}

// text_selection.rs plain_projection_preserves_forward_reversed_and_unicode_
// ranges.
static void PlainProjectionPreservesForwardReversedAndUnicodeRanges() {
    SelectionFixture f;
    PlainRun laid = LaidOutRun(f.win, "a\xC3\xA9\xF0\x9F\x99\x82z", 0);
    TextSelectionRun run = TextRun(0, laid);
    Point start = PositionForIndex(laid, 1);
    Point end = PositionForIndex(laid, 7);

    TextSelectionSnapshot forwardSnapshot = PlainSnapshot(start, end);
    TextSelectionSnapshot reversedSnapshot = PlainSnapshot(end, start);
    TextSelectionProjection forward =
        TextSelectionProjectRanges(&forwardSnapshot, &run, 1);
    TextSelectionProjection reversed =
        TextSelectionProjectRanges(&reversedSnapshot, &run, 1);

    utassert(forward.Len() == 1 && RangeIs(forward, 0, 1, 7));
    utassert(reversed.Len() == 1 && RangeIs(reversed, 0, 1, 7));
    utassert(forward.IsActive());
    utassert(reversed.IsActive());
    forward.Reset();
    reversed.Reset();
    PlainRunFree(&laid);
}

// text_selection.rs double_click_expands_a_plain_run_to_the_input_word_
// boundary.
static void DoubleClickExpandsAPlainRunToTheInputWordBoundary() {
    const char* texts[] = {"one caf\xC3\xA9, three"};
    int start = -1, end = -1;
    utassert(MultiClickRange(texts, 1, texts[0], 6, 2, &start, &end));
    utassert(start == 4 && end == 9);
}

// text_selection.rs multi_click_uses_text_layout_window_coordinates_at_a_
// nonzero_origin.
static void MultiClickUsesTextLayoutWindowCoordinatesAtANonzeroOrigin() {
    const char* texts[] = {"above", "alpha beta"};
    int start = -1, end = -1;
    float top = 0;
    utassert(MultiClickRange(texts, 2, texts[1], 7, 2, &start, &end, &top));
    utassert(top > 0);
    utassert(start == 6 && end == 10);
}

// text_selection.rs triple_click_expands_to_the_input_logical_line_not_the_
// visual_row.
static void TripleClickExpandsToTheInputLogicalLineNotTheVisualRow() {
    const char* texts[] = {"second line"};
    int start = -1, end = -1;
    utassert(MultiClickRange(texts, 1, texts[0], 4, 4, &start, &end));
    utassert(start == 0 && end == 11);
    int a = -1, b = -1;
    TextLineRangeAt(StrL("first line\nsecond line\nthird"), 15, &a, &b);
    utassert(a == 11 && b == 22);
}

// text_selection.rs plain_projection_spans_multiple_runs_and_leaves_empty_
// gutters_unselected.
static void PlainProjectionSpansMultipleRunsAndLeavesEmptyGuttersUnselected() {
    SelectionFixture f;
    PlainRun first = LaidOutRun(f.win, "first", 0);
    PlainRun gutter = LaidOutRun(f.win, "", 1);
    PlainRun second = LaidOutRun(f.win, "second", 2);
    Point start = PositionForIndex(first, 2);
    Point end = PositionForIndex(second, 3);
    TextSelectionRun runs[3] = {TextRun(2, second), TextRun(1, gutter),
                                TextRun(0, first)};
    TextSelectionSnapshot snapshot = PlainSnapshot(start, end);
    TextSelectionProjection states =
        TextSelectionProjectRanges(&snapshot, runs, 3);

    utassert(states.Len() == 3);
    utassert(RangeIs(states, 0, 0, 3));
    utassert(RangeIsNone(states, 1));
    utassert(RangeIs(states, 2, 2, 5));
    utassert(states.IsActive());
    states.Reset();
    PlainRunFree(&first);
    PlainRunFree(&gutter);
    PlainRunFree(&second);
}

// text_selection.rs plain_projection_caches_multiple_participant_copies_in_
// document_order.
static void PlainProjectionCachesMultipleParticipantCopiesInDocumentOrder() {
    SelectionFixture f;
    PlainRun one = LaidOutRun(f.win, "one", 0);
    PlainRun two = LaidOutRun(f.win, "two", 1);
    TextSelectionSnapshot snapshot =
        PlainSnapshot(PositionForIndex(one, 1), PositionForIndex(two, 2));
    FakeParticipant* first = f.Add("", 0, 0, 1);
    FakeParticipant* second = f.Add("", 20, 0, 0);

    TextSelectionHandleSetSnapshot(first->selection, f.app, &snapshot);
    TextSelectionRun firstRun = TextRun(0, one);
    TextSelectionProjection projection = first->selection
                                             .UpdateRuns(&firstRun, 1, f.app);
    utassert(projection.Len() == 1 && RangeIs(projection, 0, 1, 3));
    utassert(projection.IsActive());
    projection.Reset();
    TextSelectionHandleSetSnapshot(second->selection, f.app, &snapshot);
    TextSelectionRun secondRun = TextRun(0, two);
    projection = second->selection.UpdateRuns(&secondRun, 1, f.app);
    utassert(projection.Len() == 1 && RangeIs(projection, 0, 0, 2));
    utassert(projection.IsActive());
    projection.Reset();

    utassert(f.SelectedIs("tw\nne"));
    PlainRunFree(&one);
    PlainRunFree(&two);
}

// text_selection.rs plain_projection_invalidates_cached_copy_when_the_
// snapshot_changes.
static void PlainProjectionInvalidatesCachedCopyWhenTheSnapshotChanges() {
    SelectionFixture f;
    PlainRun laid = LaidOutRun(f.win, "first", 0);
    TextSelectionSnapshot firstSnapshot =
        PlainSnapshot(PositionForIndex(laid, 1), PositionForIndex(laid, 3));
    TextSelectionSnapshot changedSnapshot =
        PlainSnapshot(PositionForIndex(laid, 3), PositionForIndex(laid, 5));
    TextSelectionRun run = TextRun(0, laid);
    FakeParticipant* participant = f.Add("", 0);

    TextSelectionHandleSetSnapshot(participant->selection, f.app,
                                   &firstSnapshot);
    participant->selection.UpdateRuns(&run, 1, f.app).Reset();
    utassert(f.SelectedIs("ir"));

    TextSelectionHandleSetSnapshot(participant->selection, f.app,
                                   &changedSnapshot);
    utassert(f.SelectedIs(""));

    participant->selection.UpdateRuns(&run, 1, f.app).Reset();
    utassert(f.SelectedIs("st"));
    WindowSelectionClear(f.win);
    participant->selection.SetLocalSelection(true, f.app);
    utassert(f.SelectedIs(""));
    PlainRunFree(&laid);
}

// text_selection.rs plain_projection_orders_cached_runs_by_frame_order_not_
// input_order.
static void PlainProjectionOrdersCachedRunsByFrameOrderNotInputOrder() {
    SelectionFixture f;
    PlainRun one = LaidOutRun(f.win, "one", 0);
    PlainRun two = LaidOutRun(f.win, "two", 1);
    TextSelectionSnapshot snapshot =
        PlainSnapshot(PositionForIndex(one, 1), PositionForIndex(two, 2));
    FakeParticipant* participant = f.Add("", 0);
    TextSelectionHandleSetSnapshot(participant->selection, f.app, &snapshot);
    TextSelectionRun runs[2] = {TextRun(1, one), TextRun(0, two)};
    participant->selection.UpdateRuns(runs, 2, f.app).Reset();

    utassert(f.SelectedIs("twne"));
    PlainRunFree(&one);
    PlainRunFree(&two);
}

// text_selection.rs plain_projection_safely_rejects_a_text_layout_length_
// mismatch: not ported — the platform's TextLayout does not report the
// length of the text it was shaped from (paint.h has no such query), so a run
// cannot compare it with its own text the way selection_range_for_run's
// `run.text.len() != run.layout.len()` guard does.

// text_selection.rs begin_update_and_end_publish_a_cross_participant_
// selection.
static void BeginUpdateAndEndPublishACrossParticipantSelection() {
    SelectionFixture f;
    f.Add("first", 0, 0, 0);
    f.Add("second", 20, 0, 1);
    f.Begin(1, 1);
    f.Update(1, 25);
    utassert(f.Has());
    utassert(f.SelectedIs("first\nsecond"));
    f.End();
    utassert(!f.win->sel->gesture.selecting);
}

// text_selection.rs shift_extension_keeps_its_original_anchor_when_reversed.
// Rust reads the endpoints off the window state's snapshot; the window here
// keeps them as its anchor and cursor points, which are the same window
// coordinates (the participant sits at the origin).
static void ShiftExtensionKeepsItsOriginalAnchorWhenReversed() {
    SelectionFixture f;
    f.Add("participant", 0);
    f.Begin(2, 2);
    f.End();
    f.Begin(8, 2, true);
    f.End();
    Point firstAnchor = f.win->sel->anchorPoint;

    f.Begin(0, 2, true);
    f.End();
    Point anchor = f.win->sel->anchorPoint;
    Point cursor = f.win->sel->cursorPoint;
    utassert(anchor.x == firstAnchor.x && anchor.y == firstAnchor.y);
    utassert(cursor.x < anchor.x);
}

static bool ContentKey7(void* user, Point, const App*,
                        TextSelectionContentKey* out) {
    // The window state, read from inside the resolver.
    Window* win = (Window*)user;
    (void)WindowSelectionHas(win);
    *out = TextSelectionContentKey::New(7);
    return true;
}

// text_selection.rs content_key_resolver_runs_outside_the_window_state_lease.
static void ContentKeyResolverRunsOutsideTheWindowStateLease() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("virtual", 0);
    participant->selection.ResolveContentKeyWith(&ContentKey7, f.win, f.app);
    f.Begin(1, 1);
    f.Update(8, 1);
    TextSelectionSnapshot snapshot;
    utassert(f.Snapshot(participant, &snapshot));
    utassert(snapshot.Cursor().hasContentKey &&
             snapshot.Cursor().contentKey == TextSelectionContentKey::New(7));
}

// text_selection.rs active_dnd_does_not_move_a_text_selection_cursor: a
// pointer move while a drag and drop is in flight leaves the selection's
// cursor where it was.
static void ActiveDndDoesNotMoveATextSelectionCursor() {
    SelectionFixture f;
    f.Add("participant", 0);
    f.Begin(1, 1);
    Point before = f.win->sel->cursorPoint;
    f.win->activeDrag.kind = StrL("test-drag");
    f.win->mouseDown = true;
    PlatformInput move =
        InputMouseMove(80, 1, true, MouseButton::Left, Modifiers{});
    WindowDispatchInput(f.win, &move);
    utassert(f.win->sel->cursorPoint.x == before.x && f.win->sel->cursorPoint
                                                              .y == before.y);
    f.win->activeDrag = {};
    f.win->mouseDown = false;
}

// text_selection.rs shift_extension_falls_back_when_the_anchor_participant_
// was_swept.
static void ShiftExtensionFallsBackWhenTheAnchorParticipantWasSwept() {
    SelectionFixture f;
    FakeParticipant* first = f.Add("first", 0, 0, 0);
    f.Begin(1, 1);
    f.Update(8, 1);
    f.End();

    // finish_frame twice with the first participant not painted again.
    first->shown = false;
    f.Draw();
    f.Draw();
    f.Add("second", 20, 0, 1);
    f.Begin(1, 21, true);
    f.Update(8, 21);
    f.End();

    utassert(f.SelectedIs("second"));
}

// text_selection.rs scope_and_suppression_prevent_unrelated_participants_
// from_participating.
static void ScopeAndSuppressionPreventUnrelatedParticipantsFromParticipating() {
    SelectionFixture f;
    f.Add("base", 0, 0, 0);
    f.Add("modal", 20, 1, 1);

    TextSelection::ActivateScope(TextSelectionScopeId::FromRaw(1), f.win,
                                 f.app);
    f.Begin(1, 21);
    f.Update(8, 21);
    f.End();
    utassert(f.SelectedIs("modal"));

    WindowSelectionClear(f.win);
    BaseSuppressTextSelection(f.app);
    f.Begin(1, 21);
    f.Update(8, 21);
    utassert(!f.Has());
}

// text_selection.rs dead_participants_are_pruned_and_empty_selection_falls_
// back_safely: the participant's handle is dropped, and with it the text it
// painted.
static void DeadParticipantsArePrunedAndEmptySelectionFallsBackSafely() {
    SelectionFixture f;
    FakeParticipant* gone = f.Add("gone", 0);
    EntityDrop(f.app, gone->selection.Entity());
    gone->shown = false;
    f.Draw();

    f.Begin(1, 1);
    f.Update(8, 1);
    f.End();
    utassert(f.SelectedIs(""));
    utassert(!f.Has());
}

// text_selection.rs text_selection_namespace_reports_copies_ends_and_clears_
// selection.
static void TextSelectionNamespaceReportsCopiesEndsAndClearsSelection() {
    SelectionFixture f;
    f.Add("copied", 0);
    f.Begin(1, 1);
    f.Update(8, 1);

    utassert(TextSelection::HasSelection(f.win, f.app));
    utassert(f.SelectedIs("copied"));
    TextSelection::End(f.win, f.app);
    utassert(TextSelection::HasSelection(f.win, f.app));
    TextSelection::Clear(f.win, f.app);
    utassert(!TextSelection::HasSelection(f.win, f.app));
    utassert(f.SelectedIs(""));
}

// WindowOwnedSelectionView: a participant registered over the whole window.
struct WindowOwnedSelectionView {
    TextSelectionHandle selection = {};

    static El* Render(WindowOwnedSelectionView* self, Ctx* cx) {
        Bounds box = {0, 0, cx->win->paint.viewW, cx->win->paint.viewH};
        self->selection.Register(TextSelectionRegistration::New(box, box)
                                     .WithTextBounds(&box, 1),
                                 cx->win, cx->app);
        return Div(cx->a)->SizeFull()->Child(TextSelectionLayer::New(cx));
    }
};

static Window* OpenOwned(App* app, const char* text, TextSelectionHandle* out) {
    Entity<WindowOwnedSelectionView> view =
        EntityNew<WindowOwnedSelectionView>(app);
    view.Get(app)->selection = TextSelectionHandle::New(Str(text), app);
    *out = view.Get(app)->selection;
    return TestWindowOpen(app, view, 400, 300);
}

static bool WindowSelectedIs(Window* win, App* app, const char* want) {
    char buf[64];
    int n = TextSelection::SelectedText(win, app, buf, (int)sizeof(buf));
    return StrEq(Str(buf, n), Str(want));
}

// text_selection.rs two_windows_isolate_selection_copy_clear_and_release_
// ownership.
static void TwoWindowsIsolateSelectionCopyClearAndReleaseOwnership() {
    App* app = TestAppNew();
    TextSelectionHandle firstSelection, secondSelection;
    Window* first = OpenOwned(app, "first", &firstSelection);
    Window* second = OpenOwned(app, "second", &secondSelection);

    TestDraw(first);
    firstSelection.SetLocalSelection(true, app);
    utassert(WindowSelectedIs(first, app, "first"));
    utassert(first->sel != nullptr);
    TestDraw(second);
    secondSelection.SetLocalSelection(true, app);
    utassert(WindowSelectedIs(second, app, "second"));

    TextSelection::Clear(first, app);
    utassert(WindowSelectedIs(first, app, ""));
    utassert(WindowSelectedIs(second, app, "second"));

    // window.remove_window(): the window's selection state goes with it.
    WindowClosed(first);
    TestRunUntilParked(app);
    utassert(first->sel == nullptr);
    utassert(WindowSelectedIs(second, app, "second"));
    TestAppFree(app);
}

struct ReentrantCopy {
    Window* win = nullptr;
    App* app = nullptr;
    TextSelectionHandle selection = {};
    bool sawWindow = false;
    bool sawHandle = false;
};

static int ReentrantCopyText(void* user, App*, char* out, int cap) {
    ReentrantCopy* c = (ReentrantCopy*)user;
    c->sawWindow = WindowSelectionHas(c->win);
    c->sawHandle = c->selection.Snapshot(c->app, nullptr);
    c->selection.SetFallbackCopyText(StrL("reentered"), c->app);
    const char* value = "reentrant copy";
    int n = std::min((int)strlen(value), cap > 0 ? cap - 1 : 0);
    memcpy(out, value, (size_t)n);
    if (cap > 0) {
        out[n] = 0;
    }
    return n;
}

// text_selection.rs copy_callback_can_reenter_window_and_handle_selection.
static void CopyCallbackCanReenterWindowAndHandleSelection() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("fallback", 0);
    ReentrantCopy copy;
    copy.win = f.win;
    copy.app = f.app;
    copy.selection = participant->selection;
    participant->selection.CopyWith(&ReentrantCopyText, &copy, f.app);
    f.Begin(1, 1);
    f.Update(8, 1);
    f.End();

    utassert(f.SelectedIs("reentrant copy"));
    utassert(copy.sawWindow && copy.sawHandle);
}

// text_selection.rs cross_participant_selection_excludes_participants_
// outside_its_document_interval.
static void
CrossParticipantSelectionExcludesParticipantsOutsideItsDocumentInterval() {
    SelectionFixture f;
    f.Add("first", 0, 0, 0);
    f.Add("second", 20, 0, 1);
    FakeParticipant* third = f.Add("third", 40, 0, 2);
    f.Begin(1, 1);
    f.Update(1, 25);
    f.End();

    utassert(f.SelectedIs("first\nsecond"));
    utassert(!f.Snapshot(third, nullptr));
}

// text_selection.rs changing_scope_clears_the_previous_scope_selection.
static void ChangingScopeClearsThePreviousScopeSelection() {
    SelectionFixture f;
    FakeParticipant* base = f.Add("base", 0, 0, 0);
    f.Add("modal", 20, 1, 1);
    f.Begin(1, 1);
    f.Update(8, 1);
    f.End();
    TextSelection::ActivateScope(TextSelectionScopeId::FromRaw(1), f.win,
                                 f.app);

    utassert(!f.Has());
    utassert(!f.Snapshot(base, nullptr));
}

// text_selection.rs blank_only_drag_never_publishes_or_copies_selection.
static void BlankOnlyDragNeverPublishesOrCopiesSelection() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("participant", 0);
    f.Begin(200, 1);
    f.Update(200, 8);
    f.End();

    utassert(!f.Has());
    utassert(f.SelectedIs(""));
    utassert(!f.Snapshot(participant, nullptr));
}

// text_selection.rs stale_live_participants_are_removed_when_the_next_frame_
// begins: alive, but not painted again.
static void StaleLiveParticipantsAreRemovedWhenTheNextFrameBegins() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("stale", 0);
    f.Begin(1, 1);
    f.Update(8, 1);
    f.End();

    participant->shown = false;
    f.Draw();
    f.Draw();
    utassert(f.SelectedIs(""));
    utassert(!f.Snapshot(participant, nullptr));
}

// text_selection.rs clear_stops_anchor_auto_scroll_before_discarding_the_
// anchor.
static void ClearStopsAnchorAutoScrollBeforeDiscardingTheAnchor() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("scroll", 0);
    Entity<SelectionObserver> observer = f.Observe(participant);
    TestRunUntilParked(f.app);
    f.Begin(1, 1);
    f.Update(1, 25);
    WindowSelectionClear(f.win);
    TestRunUntilParked(f.app);
    SelectionObserver* o = observer.Get(f.app);
    bool any = false;
    for (int i = 0; i < o->nScroll; i++) {
        any = any || o->scroll[i];
    }
    utassert(any);
    utassert(o->nScroll > 0 && !o->scroll[o->nScroll - 1]);
}

// text_selection.rs drag_auto_scroll_stops_when_the_content_mask_collapses:
// the drag's auto scroll is a command to the anchor participant here, so a
// stopped one is the last command being "none".
static void DragAutoScrollStopsWhenTheContentMaskCollapsesInAWindow() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("participant", 0);
    Entity<SelectionObserver> observer = f.Observe(participant);
    f.Begin(1, 1);
    // The scrollable ancestor got clipped away mid-drag, so the refreshed
    // registration carries a collapsed content mask.
    Bounds collapsed = {0, 0, 100, 0};
    participant->selection
        .Register(TextSelectionRegistration::New(collapsed, collapsed)
                      .WithTextBounds(&collapsed, 1),
                  f.win, f.app);
    f.Update(1, 50);
    TestRunUntilParked(f.app);
    SelectionObserver* o = observer.Get(f.app);
    utassert(o->nScroll > 0 && !o->scroll[o->nScroll - 1]);
}

// text_selection.rs pointer_moves_after_a_click_do_not_auto_scroll.
static void PointerMovesAfterAClickDoNotAutoScroll() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("participant", 0);
    Entity<SelectionObserver> observer = f.Observe(participant);
    // A click on text keeps its anchor so shift-click can extend it.
    f.Begin(1, 1);
    f.End();
    utassert(f.win->sel->anchor >= 0);
    TestRunUntilParked(f.app);
    int before = observer.Get(f.app)->nScroll;

    f.Update(1, 50);
    TestRunUntilParked(f.app);
    SelectionObserver* o = observer.Get(f.app);
    bool running = false;
    for (int i = before; i < o->nScroll; i++) {
        running = running || o->scroll[i];
    }
    utassert(!running);
}

// text_selection.rs proxy_endpoints_break_equal_position_ties_by_document_
// order.
static void ProxyEndpointsBreakEqualPositionTiesByDocumentOrder() {
    SelectionFixture f;
    f.Add("later", 0, 0, 2);
    FakeParticipant* earlier = f.Add("earlier", 0, 0, 1);
    f.Begin(1, 1);
    f.Update(200, 25);
    TextSelectionSnapshot snapshot;
    utassert(f.Snapshot(earlier, &snapshot));
    utassert(snapshot.Cursor().entity == earlier->selection.Entity());
}

// text_selection.rs equal_area_hovered_participants_break_ties_by_document_
// order.
static void EqualAreaHoveredParticipantsBreakTiesByDocumentOrder() {
    for (int i = 0; i < 64; i++) {
        SelectionFixture f;
        f.Add("later", 0, 0, 30);
        FakeParticipant* earliest = f.Add("earliest", 0, 0, 10);
        f.Add("middle", 0, 0, 20);
        f.Begin(1, 1);
        f.Update(8, 1);
        TextSelectionSnapshot snapshot;
        utassert(f.Snapshot(earliest, &snapshot));
        utassert(snapshot.Anchor().entity == earliest->selection.Entity());
    }
}

// text_selection.rs text_selection_namespace_is_a_safe_no_op_until_the_
// element_is_rendered.
static void TextSelectionNamespaceIsASafeNoOpUntilTheElementIsRendered() {
    App* app = TestAppNew();
    Window* win = TestWindowOpen(app, EntityNew<SelectionObserver>(app));
    char buf[16];
    utassert(!TextSelection::HasSelection(win, app));
    utassert(TextSelection::SelectedText(win, app, buf, 16) == 0);
    TextSelection::Clear(win, app);
    TextSelection::End(win, app);
    utassert(!TextSelection::HasSelection(win, app));
    TestAppFree(app);
}

// text_selection.rs unit_selection_element_supports_scope_and_registration_
// on_the_first_frame.
static void UnitSelectionElementSupportsScopeAndRegistrationOnTheFirstFrame() {
    App* app = TestAppNew();
    Entity<ParticipantView> view = EntityNew<ParticipantView>(app);
    ParticipantView* v = view.Get(app);
    v->activateScope = 23;
    FakeParticipant* p = &v->parts[v->n++];
    p->selection = TextSelectionHandle::New(StrL("first frame"), app);
    p->text = StrL("first frame");
    p->scope = TextSelectionScopeId::FromRaw(23);
    Window* win = TestWindowOpen(app, view, 400, 300);

    utassert(win->sel && win->sel->activeScope.raw == 23);
    bool registered = false;
    for (int i = 0; win->sel && i < win->sel->participants.len; i++) {
        registered = registered || win->sel->participants[i] == p->selection
                                                                    .Entity();
    }
    utassert(registered);
    TestAppFree(app);
}

// text_selection.rs lazy_registration_does_not_enable_queries_without_the_
// element: not ported — the window owns its selection unconditionally here
// (WindowSelectionOf makes it on first use), and TextSelectionLayer is an
// empty marker element, so a registered participant's local selection is
// answered whether or not a layer was rendered.

// ToggleSelectionElementView: the layer and a registered participant while
// enabled, nothing otherwise.
struct ToggleSelectionElementView {
    bool enabled = true;
    TextSelectionHandle selection = {};

    static El* Render(ToggleSelectionElementView* self, Ctx* cx) {
        El* root = Div(cx->a);
        if (!self->enabled) {
            return root;
        }
        Bounds box = {0, 0, cx->win->paint.viewW, cx->win->paint.viewH};
        self->selection.Register(TextSelectionRegistration::New(box, box)
                                     .WithTextBounds(&box, 1),
                                 cx->win, cx->app);
        return root->Child(TextSelectionLayer::New(cx))
            ->Child(Div(cx->a)->SizeFull());
    }
};

// text_selection.rs retained_selection_state_releases_and_does_not_
// resurrect_selection. window.simulate_next_frame is the next frame drawn.
static void RetainedSelectionStateReleasesAndDoesNotResurrectSelection() {
    App* app = TestAppNew();
    Entity<ToggleSelectionElementView> view =
        EntityNew<ToggleSelectionElementView>(app);
    view.Get(app)->selection = TextSelectionHandle::New(StrL("local"), app);
    TextSelectionHandle selection = view.Get(app)->selection;
    Window* win = TestWindowOpen(app, view, 400, 300);
    char buf[16];

    TestDraw(win);
    selection.SetLocalSelection(true, app);
    utassert(TextSelection::HasSelection(win, app));
    TestDraw(win);
    utassert(TextSelection::HasSelection(win, app));
    TestDraw(win);
    utassert(TextSelection::HasSelection(win, app));

    view.Get(app)->enabled = false;
    AppInvalidate(win);
    TestFlushEffects(app);
    TestDraw(win);
    TestDraw(win);
    TestRunUntilParked(app);
    utassert(!TextSelection::HasSelection(win, app));
    utassert(TextSelection::SelectedText(win, app, buf, 16) == 0);
    utassert(!selection.HasLocalSelection(app));
    TextSelection::Clear(win, app);

    view.Get(app)->enabled = true;
    AppInvalidate(win);
    TestFlushEffects(app);
    TestDraw(win);
    utassert(!TextSelection::HasSelection(win, app));
    utassert(TextSelection::SelectedText(win, app, buf, 16) == 0);
    TestAppFree(app);
}

// text_selection.rs mounted_selection_element_does_not_keep_an_idle_frame_
// queue_alive: not ported — there is no next-frame callback queue for the
// layer to schedule a sweep on (the frame calls WindowSelectionFinishFrame
// itself), and the window's selection state is made on first use rather
// than by the layer, so neither count has a counterpart.

// SelectionElementOnlyView: the layer, and a box that suppresses text
// selection while a press bubbles through it.
struct SelectionElementOnlyView {
    static void OnPress(SelectionElementOnlyView*, Ctx* cx,
                        const MouseDownEvent*) {
        BaseSuppressTextSelection(cx->app);
    }

    static El* Render(SelectionElementOnlyView*, Ctx* cx) {
        return Div(cx->a)
            ->SizeFull()
            ->Child(TextSelectionLayer::New(cx))
            ->Child(Div(cx->a)->SizeFull()->OnMouseDown(
                Listen(cx, &SelectionElementOnlyView::OnPress)));
    }
};

// text_selection.rs selection_element_initializes_suppression_and_respects_
// bubble_suppression.
static void
SelectionElementInitializesSuppressionAndRespectsBubbleSuppression() {
    App* app = TestAppNew();
    Window* win =
        TestWindowOpen(app, EntityNew<SelectionElementOnlyView>(app), 400, 300);
    TestDraw(win);
    TestSimulateMouseDown(win, {1, 1});
    TestSimulateMouseUp(win, {1, 1});
    utassert(BaseIsTextSelectionSuppressed(app));
    utassert(!TextSelection::HasSelection(win, app));
    TestAppFree(app);
}

// text_selection.rs frame_sweep_keeps_a_participant_registered_before_the_
// selection_element_paints: registered in this generation (directly, the
// way a participant painting before the layer is), then swept once.
static void
FrameSweepKeepsAParticipantRegisteredBeforeTheSelectionElementPaints() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("painted first", 0);
    Bounds box = {0, 0, 100, 10};
    participant->selection.Register(TextSelectionRegistration::New(box, box)
                                        .WithTextBounds(&box, 1),
                                    f.win, f.app);
    f.Begin(1, 1);
    f.Update(8, 1);
    f.End();

    WindowSelectionFinishFrame(f.win);

    utassert(f.SelectedIs("painted first"));
    utassert(f.Snapshot(participant, nullptr));
}

// text_selection.rs two_selection_elements_schedule_only_one_post_frame_
// sweep: a view with two layers, its participant registered by hand, the
// selection made, then a frame and the next.
static void TwoSelectionElementsScheduleOnlyOnePostFrameSweep() {
    SelectionFixture f;
    FakeParticipant* participant = f.Add("once", 0);
    (void)participant;
    f.Begin(1, 1);
    f.Update(8, 1);
    f.End();
    f.Draw();
    f.Draw();
    utassert(f.SelectedIs("once"));
}

// DoubleSelectionElementView as the pointer meets it: two layers, and the
// participant over the box it registers, which shows its text.
struct DoubleLayerParticipantView {
    TextSelectionHandle selection = {};

    static El* Render(DoubleLayerParticipantView* self, Ctx* cx) {
        Bounds box = {0, 0, 200, 40};
        self->selection.Register(TextSelectionRegistration::New(box, box)
                                     .WithTextBounds(&box, 1),
                                 cx->win, cx->app);
        return Div(cx->a)
            ->SizeFull()
            ->Child(TextSelectionLayer::New(cx))
            ->Child(TextSelectionLayer::New(cx))
            ->Child(TextEl(cx->a, StrL("once"))
                        ->Selectable()
                        ->SelectionOwner(self->selection.Entity())
                        ->W(box.w)
                        ->H(box.h));
    }
};

// text_selection.rs duplicate_selection_elements_gate_real_pointer_gestures_
// and_reentrant_clear: every press clears the participant once, whichever
// layer saw it, and a clear handler may read the selection back.
static void
DuplicateSelectionElementsGateRealPointerGesturesAndReentrantClear() {
    App* app = TestAppNew();
    Entity<DoubleLayerParticipantView> view =
        EntityNew<DoubleLayerParticipantView>(app);
    view.Get(app)->selection = TextSelectionHandle::New(StrL("once"), app);
    Entity<SelectionObserver> observer = EntityNew<SelectionObserver>(app);
    SubscribeTo(app, view.Get(app)->selection.state, observer,
                &SelectionObserver::OnEvent);
    Window* win = TestWindowOpen(app, view, 400, 300);
    TestDraw(win);

    Modifiers shift = {};
    shift.shift = true;
    TestSimulateMouseDown(win, {10, 10});
    TestSimulateMouseUp(win, {10, 10});
    TestSimulateMouseDown(win, {70, 10}, MouseButton::Left, shift);
    TestSimulateMouseUp(win, {70, 10});
    utassert(TextSelection::HasSelection(win, app));

    TestSimulateMouseDown(win, {15, 10});
    TestSimulateMouseMove(win, {85, 10}, true);
    TestSimulateMouseUp(win, {85, 10});
    utassert(TextSelection::HasSelection(win, app));
    utassert(observer.Get(app)->cleared == 3);
    TestAppFree(app);
}

static bool ContentKey17(void*, Point, const App*,
                         TextSelectionContentKey* out) {
    *out = TextSelectionContentKey::New(17);
    return true;
}

// DoubleSelectionElementView with a laid-out run: two layers, and the
// participant over the run it shows.
struct DoubleSelectionElementView {
    TextSelectionHandle selection = {};

    static El* Render(DoubleSelectionElementView* self, Ctx* cx) {
        El* run = TextEl(cx->a, StrL("alpha beta"))
                      ->Font(kPlainRunFont)
                      ->Selectable()
                      ->SelectionOwner(self->selection.Entity());
        return Div(cx->a)
            ->SizeFull()
            ->Child(TextSelectionLayer::New(cx))
            ->Child(TextSelectionLayer::New(cx))
            ->Child(Div(cx->a)->Absolute()->Left(0)->Top(0)->Child(run));
    }
};

// text_selection.rs selection_layer_handles_real_double_and_triple_click_
// events.
static void SelectionLayerHandlesRealDoubleAndTripleClickEvents() {
    App* app = TestAppNew();
    Entity<DoubleSelectionElementView> view =
        EntityNew<DoubleSelectionElementView>(app);
    view.Get(app)->selection = TextSelectionHandle::New(StrL(""), app);
    TextSelectionHandle selection = view.Get(app)->selection;
    Window* win = TestWindowOpen(app, view, 400, 300);
    TestDraw(win);
    const TextHit* hit = PlainHit(win, "alpha beta");
    utassert(hit);
    if (!hit) {
        TestAppFree(app);
        return;
    }
    PlainRun laid = LaidOutRun(win, "alpha beta", 0);
    laid.bounds = hit->bounds;
    Bounds box = hit->bounds;
    selection.Register(TextSelectionRegistration::New(box, box)
                           .WithTextBounds(&box, 1),
                       win, app);
    selection.ResolveContentKeyWith(&ContentKey17, nullptr, app);
    TextSelectionRun run = TextRun(0, laid);
    selection.UpdateRuns(&run, 1, app).Reset();

    Point position = PositionForIndex(laid, 7);
    PlatformInput down =
        InputMouseDown(MouseButton::Left, position.x, position.y, {}, 2, false);
    WindowDispatchInput(win, &down);
    PlatformInput up =
        InputMouseUp(MouseButton::Left, position.x, position.y, {}, 2);
    WindowDispatchInput(win, &up);
    char buf[32];
    selection.UpdateRuns(&run, 1, app).Reset();
    int n = TextSelection::SelectedText(win, app, buf, 32);
    utassert(StrEq(Str(buf, n), StrL("beta")));
    TextSelectionSnapshot snapshot;
    utassert(selection.Snapshot(app, &snapshot));
    utassert(snapshot.Anchor().hasContentKey &&
             snapshot.Anchor().contentKey == TextSelectionContentKey::New(17));
    utassert(snapshot.Cursor().hasContentKey &&
             snapshot.Cursor().contentKey == TextSelectionContentKey::New(17));

    down =
        InputMouseDown(MouseButton::Left, position.x, position.y, {}, 3, false);
    WindowDispatchInput(win, &down);
    up = InputMouseUp(MouseButton::Left, position.x, position.y, {}, 3);
    WindowDispatchInput(win, &up);
    selection.UpdateRuns(&run, 1, app).Reset();
    n = TextSelection::SelectedText(win, app, buf, 32);
    utassert(StrEq(Str(buf, n), StrL("alpha beta")));
    PlainRunFree(&laid);
    TestAppFree(app);
}

static void TestTextSelectionWindow() {
    SelectionCallbackCanReenterItsSelectionState();
    SelectionEventsPreserveSnapshotThenClearOrder();
    PublicSelectionDataUsesBuildersAndReaders();
    SelectionHandleIsThePublicAdapterSeam();
    SelectionHandleCanSubscribeItsWindowToRefresh();
    PlainProjectionPreservesForwardReversedAndUnicodeRanges();
    DoubleClickExpandsAPlainRunToTheInputWordBoundary();
    MultiClickUsesTextLayoutWindowCoordinatesAtANonzeroOrigin();
    TripleClickExpandsToTheInputLogicalLineNotTheVisualRow();
    PlainProjectionSpansMultipleRunsAndLeavesEmptyGuttersUnselected();
    PlainProjectionCachesMultipleParticipantCopiesInDocumentOrder();
    PlainProjectionInvalidatesCachedCopyWhenTheSnapshotChanges();
    PlainProjectionOrdersCachedRunsByFrameOrderNotInputOrder();
    BeginUpdateAndEndPublishACrossParticipantSelection();
    ShiftExtensionKeepsItsOriginalAnchorWhenReversed();
    ContentKeyResolverRunsOutsideTheWindowStateLease();
    ActiveDndDoesNotMoveATextSelectionCursor();
    ShiftExtensionFallsBackWhenTheAnchorParticipantWasSwept();
    ScopeAndSuppressionPreventUnrelatedParticipantsFromParticipating();
    DeadParticipantsArePrunedAndEmptySelectionFallsBackSafely();
    TextSelectionNamespaceReportsCopiesEndsAndClearsSelection();
    TwoWindowsIsolateSelectionCopyClearAndReleaseOwnership();
    CopyCallbackCanReenterWindowAndHandleSelection();
    CrossParticipantSelectionExcludesParticipantsOutsideItsDocumentInterval();
    ChangingScopeClearsThePreviousScopeSelection();
    BlankOnlyDragNeverPublishesOrCopiesSelection();
    StaleLiveParticipantsAreRemovedWhenTheNextFrameBegins();
    ClearStopsAnchorAutoScrollBeforeDiscardingTheAnchor();
    DragAutoScrollStopsWhenTheContentMaskCollapsesInAWindow();
    PointerMovesAfterAClickDoNotAutoScroll();
    ProxyEndpointsBreakEqualPositionTiesByDocumentOrder();
    EqualAreaHoveredParticipantsBreakTiesByDocumentOrder();
    TextSelectionNamespaceIsASafeNoOpUntilTheElementIsRendered();
    UnitSelectionElementSupportsScopeAndRegistrationOnTheFirstFrame();
    RetainedSelectionStateReleasesAndDoesNotResurrectSelection();
    SelectionElementInitializesSuppressionAndRespectsBubbleSuppression();
    FrameSweepKeepsAParticipantRegisteredBeforeTheSelectionElementPaints();
    TwoSelectionElementsScheduleOnlyOnePostFrameSweep();
    DuplicateSelectionElementsGateRealPointerGesturesAndReentrantClear();
    SelectionLayerHandlesRealDoubleAndTripleClickEvents();
}

void TestTextSelection() {
    TestSuite("text_selection");
    WrappedSelectionPaintsFullWidthMiddleLines();
    SelectionBandDecidesWholeRuns();
    SelectableTextJoinsTheDocumentItsHandleOwns();
    DragAutoScrollStopsWhenTheContentMaskCollapses();
    ADragThatNeverTouchesTextPublishesNothing();
    StartingInTheMarginAndDraggingOntoTextSelects();
    OnceItHasTouchedTextItStays();
    AFreshGestureStartsOver();
    ExtendingWithoutAGestureDoesNothing();
    ClearingDropsBoth();
    AWindowWithNoTextSelectsNothing();
    APressOffTextClearsIt();
    ADragAcrossTwoRunsCopiesBoth();
    ADragOutOfAScopeStaysInIt();
    AMarginOnlyDragPublishesNothing();
    ShiftClickExtendsFromTheAnchor();
    TwoClicksTakeTheWordAndThreeTheLine();
    ALongPressTakesAWordAndKeepsDragging();
    AHostFingerSwipeEmitsPhasedScroll();
    AHostLongPressTimerSelectsAWord();
    AHostHandleDragStartsOnTheSelectionHandle();
    ADoubleTapOnReadOnlyTextSelectsNothing();
    AMultiClickOffTextTakesNothing();
    AControlPressSuppressesWindowSelection();
    SourceParticipantContractsProjectAcrossAWindow();
    FrameSweepDropsOnlyRegistrationsNotRenewed();
    TestTextSelectionWindow();
}
