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
    win->paint.textDocLen += h.text.len + 1;
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
    int n = WindowSelectionText(&win, buf.s, buf.len + 1);
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
    int n = WindowSelectionText(&win, buf.s, buf.len + 1);
    utassert(StrEq(Str(buf.s, n), StrL("hello")));
    // The press ended the gesture, so a drag does not grow it.
    WindowSelectionDrag(&win, 115, 45);
    n = WindowSelectionText(&win, buf.s, buf.len + 1);
    utassert(StrEq(Str(buf.s, n), StrL("hello")));

    WindowSelectionPress(&win, 25, 5, 3, false);
    n = WindowSelectionText(&win, buf.s, buf.len + 1);
    utassert(StrEq(Str(buf.s, n), StrL("hello brave world")));
    // And it stops at the run: the line is this run's, not the document's.
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
    int n = WindowSelectionText(&win, buf.s, buf.len + 1);
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
    utassert(WindowSelectionText(&win, buf.s, buf.len + 1) == 0);
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
        TextSelection::SelectedText(&win, &app, selected.s, selected.len + 1);
    utassert(StrEq(Str(selected.s, selectedLen), StrL("first\nsecond")));
    utassert(TextSelection::HasSelection(&win, &app));
    WindowSelectionRelease(&win);
    utassert(first.Snapshot(&app, &firstSnapshot) && !firstSnapshot
                                                          .IsSelecting());
    outside.CopyWith(&ParticipantCopy, nullptr, &app);
    outside.SetLocalSelection(true, &app);
    selectedLen =
        TextSelection::SelectedText(&win, &app, selected.s, selected.len + 1);
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
    utassert(observed->cleared == 3 && observed->clearCallbacks == 3);

    El* layer = TextSelectionLayer::New(&cx);
    El* scoped = TextSelectionScope(Div(arena), one);
    utassert(base::StrEq(layer->id, StrL("window-text-selection")));
    utassert(scoped->style.trapId == one.RuntimeScope());

    WindowSelectionFree(&win);
    ArenaDelete(arena);
    EntityDropAll(&app);
}

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

void TestTextSelection() {
    TestSuite("text_selection");
    WrappedSelectionPaintsFullWidthMiddleLines();
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
    AMultiClickOffTextTakesNothing();
    AControlPressSuppressesWindowSelection();
    SourceParticipantContractsProjectAcrossAWindow();
    FrameSweepDropsOnlyRegistrationsNotRenewed();
}
