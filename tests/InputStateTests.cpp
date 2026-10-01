/* Ported from crates/base/src/input/base/state.rs, mod tests, plus the two
 * cases in movement.rs's neighbourhood that are pure logic.
 *
 * Every Rust case there is a `#[gpui::test]` built on `TestAppContext` and a
 * `VisualTestContext`: it opens a window, paints it, and then asserts. The
 * Pure state assertions are kept here: text, selection, history, providers,
 * decorations and the dependency-free editor facades. Assertions that need
 * GPUI's VisualTestContext are represented by the runtime layout/input tests
 * around them rather than by a second test-only window framework.
 *
 * The engine takes `App*` and `Window*` because it pauses a caret and asks
 * for a repaint; both are optional, so a test drives it with nulls. */

#include "Test.h"

static bool ValueIs(const InputState& s, const char* want) {
    return base::StrEq(InputValue(&s), want);
}

static bool RangeIs(const InputState& s, int start, int end) {
    return s.selectedRange.start == start && s.selectedRange.end == end;
}

// The user typing, which is what goes through the same path a key press does.
static void Type(InputState* s, const char* text) {
    InputReplaceTextInRange(s, nullptr, nullptr, nullptr, Str(text));
}

static void Act(InputState* s, InputAction action) {
    InputPerform(s, nullptr, nullptr, action, false);
}

// The input method staging a candidate: replace_and_mark_text_in_range with
// no range, which is what each keystroke of a composition does.
static void Mark(InputState* s, const char* text) {
    InputReplaceAndMarkText(s, nullptr, nullptr, nullptr, Str(text), nullptr);
}

static bool MarkIs(const InputState& s, int start, int end) {
    Selection m = {};
    if (!InputMarkedRange(&s, &m)) {
        return start < 0;
    }
    return m.start == start && m.end == end;
}

static void SingleLineRemovesNewlines() {
    InputState s;
    InputSetValue(&s, StrL("default\nvalue"));
    utassert(ValueIs(s, "defaultvalue"));

    InputSetValue(&s, StrL("first\nsecond\r\nthird\rfourth"));
    utassert(ValueIs(s, "firstsecondthirdfourth"));

    InputSetValue(&s, Str{});
    utassert(ValueIs(s, ""));

    // A textarea keeps them.
    InputState multi;
    multi.kind = InputKind::Textarea;
    InputSetValue(&multi, StrL("first\nsecond"));
    utassert(ValueIs(multi, "first\nsecond"));
}

// set_value parks a single-line caret at the end (matching an HTML <input>)
// and a multi-line one at 0..0. The scroll half of the Rust case needs a
// painted window.
static void SetValueCaretAtEnd() {
    InputState s;
    InputSetValue(&s, StrL("https://example.com/v1/users"));
    utassert(RangeIs(s, 28, 28));

    InputState multi;
    multi.kind = InputKind::Textarea;
    InputSetValue(&multi, StrL("one\ntwo"));
    utassert(RangeIs(multi, 0, 0));
}

// replace_all does the same to the selection, but stays in the history.
static void ReplaceAllPreservesUndoHistory() {
    InputState s;
    InputSetValue(&s, StrL("hello"));
    InputReplaceAll(&s, nullptr, nullptr, StrL("world!"));
    utassert(ValueIs(s, "world!"));
    utassert(RangeIs(s, 6, 6));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "hello"));

    // set_value, by contrast, clears the history: there is nothing to undo.
    InputSetValue(&s, StrL("fresh"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "fresh"));
}

static void SetSelectedRange() {
    InputState s;
    InputSetValue(&s, StrL("hello world"));

    InputSetSelectedRange(&s, nullptr, nullptr, 0, 5);
    utassert(RangeIs(s, 0, 5));
    utassert(base::StrEq(InputSelectedValue(&s), StrL("hello")));

    InputSetSelectedRange(&s, nullptr, nullptr, 6, 11);
    utassert(base::StrEq(InputSelectedValue(&s), StrL("world")));

    // clamped + collapsed
    InputSetSelectedRange(&s, nullptr, nullptr, 100, 100);
    utassert(RangeIs(s, 11, 11));
}

static void SetSelectedRangeClipsToUtf8Boundaries() {
    InputState s;
    InputSetValue(&s, StrL("éx"));

    // A non-empty range grows out to character boundaries...
    InputSetSelectedRange(&s, nullptr, nullptr, 0, 1);
    utassert(RangeIs(s, 0, 2));

    // ...an empty one clips back to the boundary before it.
    InputSetSelectedRange(&s, nullptr, nullptr, 1, 1);
    utassert(RangeIs(s, 0, 0));
}

static void AdjacentTypingCoalescesIntoOneUndo() {
    InputState s;
    Type(&s, "a");
    Type(&s, "b");
    Type(&s, "c");
    utassert(ValueIs(s, "abc"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, ""));
    Act(&s, InputAction::Redo);
    utassert(ValueIs(s, "abc"));
}

// A cursor move ends the typing session, so the two runs undo separately.
static void CursorMovementSplitsTyping() {
    InputState s;
    Type(&s, "ab");
    Act(&s, InputAction::MoveToStart);
    Type(&s, "X");
    utassert(ValueIs(s, "Xab"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "ab"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, ""));
}

static void BackwardAndForwardDeletesDoNotCoalesce() {
    InputState s;
    InputSetValue(&s, StrL("abcd"));
    InputSetSelectedRange(&s, nullptr, nullptr, 2, 2);

    Act(&s, InputAction::Backspace); // "acd"
    Act(&s, InputAction::Delete);    // "ad"
    utassert(ValueIs(s, "ad"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "acd"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "abcd"));
}

// Repeated deletes in the same direction do coalesce.
static void DirectionalCharacterDeletesCoalesce() {
    InputState s;
    InputSetValue(&s, StrL("abcd"));
    Act(&s, InputAction::Backspace);
    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, "ab"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "abcd"));
}

static void SelectedReplacementIsAtomic() {
    InputState s;
    InputSetValue(&s, StrL("hello world"));
    InputSetSelectedRange(&s, nullptr, nullptr, 0, 5);
    Type(&s, "bye");
    utassert(ValueIs(s, "bye world"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "hello world"));
    utassert(RangeIs(s, 0, 5));
}

static void ForwardDeleteRestoresCursor() {
    InputState s;
    InputSetValue(&s, StrL("abc"));
    InputSetSelectedRange(&s, nullptr, nullptr, 1, 1);
    Act(&s, InputAction::Delete);
    utassert(ValueIs(s, "ac"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "abc"));
    // The caret goes back to where it was, in front of what was deleted.
    utassert(RangeIs(s, 1, 1));
}

static void NoopEditPreservesRedo() {
    InputState s;
    Type(&s, "abc");
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, ""));

    // Deleting at the start of an empty field changes nothing.
    Act(&s, InputAction::Backspace);
    Act(&s, InputAction::Redo);
    utassert(ValueIs(s, "abc"));
}

static void MaskedRedoRestoresActualCursor() {
    InputState s;
    InputSetMaskPattern(&s, MaskPatternNew(StrL("(999)999-9999")));
    Type(&s, "1234567890");
    utassert(ValueIs(s, "(123)456-7890"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, ""));
    Act(&s, InputAction::Redo);
    utassert(ValueIs(s, "(123)456-7890"));
    // The caret is at the end of the masked text, not of what was typed.
    utassert(InputCursor(&s) == 13);
}

// A masked field keeps what it holds to itself: the clipboard never sees it,
// and the word motions have no boundaries to work with, since every character
// shows as the same bullet.
static void AMaskedValueStaysInTheField() {
    InputState s;
    s.masked = true;
    InputSetValue(&s, StrL("hunter2 secret"));
    InputSelectAll(&s, nullptr, nullptr);
    utassert(!InputIsCopyable(&s));
    // A cut is a copy that also deletes, so it does neither.
    Act(&s, InputAction::Cut);
    utassert(ValueIs(s, "hunter2 secret"));

    // Word-wise motion is the whole field either way.
    InputMoveTo(&s, nullptr, nullptr, 10);
    utassert(InputPreviousStartOfWord(&s) == 0);
    utassert(InputNextEndOfWord(&s) == 14);
    // And word-wise delete goes back to the start rather than stepping
    // through boundaries the reader cannot see.
    Act(&s, InputAction::DeleteToPreviousWordStart);
    utassert(ValueIs(s, "cret"));

    // The same field unmasked copies and moves by words again.
    s.masked = false;
    InputSetValue(&s, StrL("hello brave world"));
    InputMoveTo(&s, nullptr, nullptr, 11);
    utassert(InputPreviousStartOfWord(&s) == 6);
    InputSelectAll(&s, nullptr, nullptr);
    utassert(InputIsCopyable(&s));
}

// A field taken out of the tree while it had the keyboard takes its
// registration with it: the window points at nothing rather than at a state
// that has been freed.
static void AFocusedFieldGoingTakesItsRegistrationWithIt() {
    Window win = {};
    {
        InputState s;
        InputFocus(&s, nullptr, &win);
        utassert(win.input == &s);
        utassert(win.prevInput == &s);
    }
    utassert(win.input == nullptr);
    utassert(win.prevInput == nullptr);

    // A field that blurred first has nothing left to clear.
    InputState other;
    InputFocus(&other, nullptr, &win);
    InputBlur(&other, nullptr, &win);
    utassert(win.input == nullptr);
}

static void WordMovement() {
    InputState s;
    InputSetValue(&s, StrL("hello brave world"));

    Act(&s, InputAction::MoveToStart);
    Act(&s, InputAction::MoveToNextWord);
    utassert(InputCursor(&s) == 5);
    Act(&s, InputAction::MoveToNextWord);
    utassert(InputCursor(&s) == 11);

    Act(&s, InputAction::MoveToEnd);
    Act(&s, InputAction::MoveToPreviousWord);
    utassert(InputCursor(&s) == 12);
    Act(&s, InputAction::MoveToPreviousWord);
    utassert(InputCursor(&s) == 6);
}

static void DeleteToWordAndLineBoundaries() {
    InputState s;
    InputSetValue(&s, StrL("hello brave world"));
    Act(&s, InputAction::DeleteToPreviousWordStart);
    utassert(ValueIs(s, "hello brave "));

    InputSetValue(&s, StrL("hello brave world"));
    InputSetSelectedRange(&s, nullptr, nullptr, 6, 6);
    Act(&s, InputAction::DeleteToNextWordEnd);
    utassert(ValueIs(s, "hello  world"));

    InputSetValue(&s, StrL("hello world"));
    InputSetSelectedRange(&s, nullptr, nullptr, 5, 5);
    Act(&s, InputAction::DeleteToBeginningOfLine);
    utassert(ValueIs(s, " world"));

    InputSetValue(&s, StrL("hello world"));
    InputSetSelectedRange(&s, nullptr, nullptr, 5, 5);
    Act(&s, InputAction::DeleteToEndOfLine);
    utassert(ValueIs(s, "hello"));
}

static void TypeChars(InputState* s, const char* text) {
    for (const char* p = text; *p; p++) {
        InputTypeChar(s, nullptr, nullptr, (uint32_t)(uint8_t)*p);
    }
}

static bool Menu(InputState* s, InputAction action) {
    return InputCompletionAction(s, nullptr, nullptr, action);
}

static bool Menu2(InputState* s, InputAction action) {
    return InputCodeActionAction(s, nullptr, nullptr, action);
}

// ─── more than one code action provider ──────────────────────────────────

static int OneAction(void* data, Arena* a, Str text, Selection sel,
                     CodeActionItem* out, int cap) {
    (void)a;
    (void)text;
    (void)sel;
    if (cap > 0 && out) {
        out[0].title = data ? StrL("second") : StrL("first");
        out[0].range = sel;
        out[0].newText = data ? StrL("B") : StrL("A");
    }
    return 1;
}

static int gPerformed = -1;

static bool PerformIt(void* data, InputState* s, App* app, Window* win,
                      const CodeActionItem* item) {
    (void)data;
    (void)s;
    (void)app;
    (void)win;
    (void)item;
    gPerformed = item->provider;
    return true;
}

// Rust asks every registered provider and puts the answers in one list, each
// item remembering which one it came from — and performing it goes back to
// that provider.
static void EveryProviderIsAsked() {
    InputState s;
    s.kind = InputKind::Editor;
    int second = 1;
    InputAddCodeActionProvider(&s, &OneAction, nullptr);
    InputAddCodeActionProvider(&s, &OneAction, &second, &PerformIt);
    InputSetValue(&s, StrL("hello"));
    InputSetSelectedRange(&s, nullptr, nullptr, 0, 5);
    Act(&s, InputAction::ToggleCodeActions);
    utassert(s.codeActions.open && s.codeActions.items.len == 2);
    utassert(base::StrEq(s.codeActions.items[0].title, StrL("first")));
    utassert(base::StrEq(s.codeActions.items[1].title, StrL("second")));
    utassert(s.codeActions.items[0].provider == 0);
    utassert(s.codeActions.items[1].provider == 1);

    // The one that answered performs it, if it said it would.
    gPerformed = -1;
    utassert(Menu2(&s, InputAction::MoveDown));
    utassert(Menu2(&s, InputAction::Enter));
    utassert(gPerformed == 1);
    // It took the action, so the editor wrote nothing.
    utassert(ValueIs(s, "hello"));

    // The first provider named no perform, so its edits are the editor's to
    // apply.
    Act(&s, InputAction::ToggleCodeActions);
    utassert(Menu2(&s, InputAction::Enter));
    utassert(ValueIs(s, "A"));
}

static int ManyActions(void* data, Arena* a, Str text, Selection sel,
                       CodeActionItem* out, int cap) {
    (void)a;
    (void)text;
    int total = (int)(intptr_t)data;
    for (int i = 0; i < total && i < cap; i++) {
        out[i].title = StrL("action");
        out[i].range = sel;
        out[i].newText = StrL("x");
    }
    return total;
}

// Rust stores providers and each provider's response in Vecs. Neither the
// number of providers nor the number of answers stops at the port's former
// four- and thirty-two-entry tables.
static void CodeActionCollectionsGrowToTheirAnswers() {
    InputState manyProviders;
    manyProviders.kind = InputKind::Editor;
    int tags[6] = {};
    for (int i = 0; i < 6; i++) {
        tags[i] = i + 1;
        InputAddCodeActionProvider(&manyProviders, &OneAction, &tags[i]);
    }
    InputSetValue(&manyProviders, StrL("hello"));
    InputSetSelectedRange(&manyProviders, nullptr, nullptr, 0, 5);
    Act(&manyProviders, InputAction::ToggleCodeActions);
    utassert(manyProviders.codeActions.items.len == 6);
    utassert(manyProviders.codeActions.items[5].provider == 5);

    // Materializing the provider Vec after a caller used the legacy direct
    // field keeps that provider as the first entry.
    InputState mixed;
    mixed.kind = InputKind::Editor;
    mixed.codeActionProvider = &OneAction;
    InputAddCodeActionProvider(&mixed, &OneAction, &tags[0]);
    InputSetValue(&mixed, StrL("hello"));
    InputSetSelectedRange(&mixed, nullptr, nullptr, 0, 5);
    Act(&mixed, InputAction::ToggleCodeActions);
    utassert(mixed.codeActions.items.len == 2);
    utassert(base::StrEq(mixed.codeActions.items[0].title, StrL("first")));
    utassert(base::StrEq(mixed.codeActions.items[1].title, StrL("second")));

    InputState manyAnswers;
    manyAnswers.kind = InputKind::Editor;
    manyAnswers.codeActionProvider = &ManyActions;
    manyAnswers.codeActionData = (void*)(intptr_t)73;
    InputSetValue(&manyAnswers, StrL("hello"));
    InputSetSelectedRange(&manyAnswers, nullptr, nullptr, 0, 5);
    Act(&manyAnswers, InputAction::ToggleCodeActions);
    utassert(manyAnswers.codeActions.items.len == 73);
}

// Lsp::reset drops everything the layer was holding.
static void ResetDropsWhatTheLayerHeld() {
    InputState s;
    s.kind = InputKind::Editor;
    CompletionItem item = {};
    item.label = StrL("unwrap");
    InputPresentCompletionItems(&s, 0, StrL(""), &item, 1);
    InputPresentHover(&s, Selection{0, 2}, StrL("about it"));
    utassert(InputIsContextMenuOpen(&s));
    InputLspReset(&s);
    utassert(!InputIsContextMenuOpen(&s));
    utassert(s.hoverText.len == 0);
    utassert(s.semanticTokens.len == 0);
    utassert(s.documentColorsDirty && s.semanticTokensDirty);
}

// ─── the overlay seam (lsp/overlay.rs) ───────────────────────────────────

// A host presenting its own items, without a provider in sight.
static void AHostCanPresentItsOwnItems() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("un"));
    InputSetSelectedRange(&s, nullptr, nullptr, 2, 2);

    CompletionItem items[2] = {};
    items[0].label = StrL("unwrap");
    items[1].label = StrL("unsafe");
    uint64_t was = s.completion.revision;
    InputPresentCompletionItems(&s, 0, StrL("un"), items, 2);
    utassert(s.completion.open && s.completion.items.len == 2);
    utassert(InputIsContextMenuOpen(&s));
    // The revision is what a host renderer diffs against, so it moves
    // whenever the content does.
    utassert(s.completion.revision != was);

    // insert_completion writes the item the host picked over the range the
    // query occupied.
    InputInsertCompletion(&s, nullptr, nullptr, &items[1], Selection{0, 2});
    utassert(ValueIs(s, "unsafe"));

    // And an empty list closes the menu rather than showing nothing.
    InputPresentCompletionItems(&s, 0, StrL(""), items, 0);
    utassert(!s.completion.open);
    utassert(!InputIsContextMenuOpen(&s));
}

static int gOverlayKeys = 0;
static InputOverlayKind gOverlayKind = InputOverlayKind::CodeAction;

static bool TakeEverything(void* data, InputOverlayKind kind,
                           InputAction action) {
    (void)data;
    (void)action;
    gOverlayKeys++;
    gOverlayKind = kind;
    return true;
}

// set_overlay_action_handler: the host's popover takes the keys before the
// editor's own menu does.
static void AHostCanTakeTheKeys() {
    InputState s;
    s.kind = InputKind::Editor;
    CompletionItem item = {};
    item.label = StrL("unwrap");
    InputPresentCompletionItems(&s, 0, StrL(""), &item, 1);
    utassert(s.completion.open);

    gOverlayKeys = 0;
    s.overlayAction = &TakeEverything;
    // Down would have moved the selection; the host took it instead.
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::MoveDown, false));
    utassert(gOverlayKeys == 1);
    utassert(gOverlayKind == InputOverlayKind::Completion);
    utassert(s.completion.selected == 0);

    // With no handler, the menu takes it as before.
    s.overlayAction = nullptr;
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::MoveDown, false));
    utassert(s.completion.selected == 0 || s.completion.items.len == 1);

    // dismiss_lsp_overlays takes down whatever is up.
    InputDismissLspOverlays(&s);
    utassert(!InputIsContextMenuOpen(&s));
}

// ─── apply_lsp_edits ─────────────────────────────────────────────────────

// A list of edits is one undo step, and each one is resolved against the
// document the ones before it left — which is why a server sends them
// last-first.
static void AnEditListIsOneStep() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("hello world"));
    TextEditItem edits[2] = {};
    edits[0].range = Selection{6, 11};
    edits[0].newText = StrL("there");
    edits[1].range = Selection{0, 5};
    edits[1].newText = StrL("goodbye");
    InputApplyEdits(&s, nullptr, nullptr, edits, 2);
    utassert(ValueIs(s, "goodbye there"));
    // Each edit is its own step, the way Rust's loop over
    // `replace_text_in_range_silent` records them; the Atomic intent is what
    // keeps them from coalescing with the typing around them.
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "hello there"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "hello world"));
}

// An action that is more than one edit: the pair is what the menu performs,
// and the single-edit shorthand is what it falls back to.
static int WrappingAction(void* data, Arena* a, Str text, Selection sel,
                          CodeActionItem* out, int cap) {
    (void)data;
    (void)text;
    if (sel.IsEmpty()) {
        return 0;
    }
    if (cap > 0 && out) {
        auto* edits = (TextEditItem*)Alloc(a, (int)sizeof(TextEditItem) * 2);
        edits[0].range = Selection{sel.end, sel.end};
        edits[0].newText = StrL(")");
        edits[1].range = Selection{sel.start, sel.start};
        edits[1].newText = StrL("(");
        out[0].title = StrL("Wrap in Parentheses");
        out[0].edits = edits;
        out[0].nEdits = 2;
    }
    return 1;
}

static void ACodeActionCanBeMoreThanOneEdit() {
    InputState s;
    s.kind = InputKind::Editor;
    s.codeActionProvider = &WrappingAction;
    InputSetValue(&s, StrL("hello world"));
    InputSetSelectedRange(&s, nullptr, nullptr, 6, 11);
    Act(&s, InputAction::ToggleCodeActions);
    utassert(s.codeActions.open && s.codeActions.items.len == 1);
    utassert(Menu2(&s, InputAction::Enter));
    utassert(ValueIs(s, "hello (world)"));
    utassert(!s.codeActions.open);
    // Two edits, two steps.
    Act(&s, InputAction::Undo);
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "hello world"));
}

// additionalTextEdits: accepting the item writes at the caret *and* wherever
// else the item said, as one step.
static const TextEditItem kTestImport = {{0, 0}, StrL("import\n")};

static int ImportingCompletions(void* data, Str text, int offset, Str query,
                                CompletionItem* out, int cap) {
    (void)data;
    (void)text;
    (void)offset;
    (void)query;
    if (cap > 0 && out) {
        out[0].label = StrL("unwrap");
        out[0].additionalEdits = &kTestImport;
        out[0].nAdditionalEdits = 1;
    }
    return 1;
}

static void AnAcceptedItemBringsItsImport() {
    InputState s;
    s.kind = InputKind::Editor;
    s.completionProvider = &ImportingCompletions;
    InputTypeChar(&s, nullptr, nullptr, 'u');
    utassert(s.completion.open);
    utassert(Menu(&s, InputAction::Enter));
    utassert(ValueIs(s, "import\nunwrap"));
    // The insert and the edit it brought are a step each.
    Act(&s, InputAction::Undo);
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "u"));
}

static void CompletionAndActionEditListsGrowPastThirtyTwo() {
    Vec<TextEditItem> additions;
    VecReserve(additions, 40);
    for (int i = 0; i < 40; i++) {
        VecAppend(additions, {Selection{0, 0}, StrL("a")});
    }

    InputState completion;
    completion.kind = InputKind::Editor;
    CompletionItem item = {};
    item.label = StrL("z");
    item.additionalEdits = additions.els;
    item.nAdditionalEdits = len(additions);
    InputPresentCompletionItems(&completion, 0, {}, &item, 1);
    InputAcceptCompletion(&completion, nullptr, nullptr);
    utassert(len(InputValue(&completion)) == 41);
    utassert(InputValue(&completion).s[40] == 'z');

    InputState action;
    action.kind = InputKind::Editor;
    CodeActionItem codeAction = {};
    codeAction.title = StrL("many edits");
    codeAction.edits = additions.els;
    codeAction.nEdits = len(additions);
    InputPresentCodeActions(&action, &codeAction, 1);
    InputPerformCodeAction(&action, nullptr, nullptr);
    utassert(len(InputValue(&action)) == 40);
}

// ─── the rest of the completion surface (lsp/completions.rs) ─────────────

static int gCompleteCalls = 0;
static int gResolveCalls = 0;

static int TestCompletions(void* data, Str text, int offset, Str query,
                           CompletionItem* out, int cap) {
    (void)data;
    (void)text;
    (void)offset;
    (void)query;
    gCompleteCalls++;
    if (cap > 0 && out) {
        // The item goes out thin, the way a server sends a thousand of them.
        out[0].label = StrL("break");
        out[0].detail = StrL("keyword");
    }
    return 1;
}

static int ManyCompletions(void* data, Str text, int offset, Str query,
                           CompletionItem* out, int cap) {
    (void)text;
    (void)offset;
    (void)query;
    int total = (int)(intptr_t)data;
    for (int i = 0; i < total && i < cap; i++) {
        out[i].label = StrL("candidate");
    }
    return total;
}

static void CompletionResponsesGrowPastTheOldBuffer() {
    InputState s;
    s.kind = InputKind::Editor;
    s.completionProvider = &ManyCompletions;
    s.completionData = (void*)(intptr_t)257;
    InputSetValue(&s, StrL("c"));
    InputShowCompletions(&s, nullptr, nullptr);
    utassert(s.completion.open);
    utassert(s.completion.items.len == 257);
    utassert(base::StrEq(s.completion.items[256].label, StrL("candidate")));
}

static Str TestResolve(void* data, Arena* a, const CompletionItem* item) {
    (void)data;
    (void)item;
    gResolveCalls++;
    return StrDup(a, StrL("Exit a loop immediately."));
}

// A provider with an opinion: `:` opens the menu, which the built-in rule
// would have closed it on.
static CompletionTrigger TestTrigger(void* data, Str text, int offset,
                                     Str typed) {
    (void)data;
    (void)text;
    (void)offset;
    if (len(typed) > 0 && typed.s[0] == ':') {
        return CompletionTrigger::Open;
    }
    if (len(typed) > 0 && typed.s[0] == '#') {
        return CompletionTrigger::Close;
    }
    return CompletionTrigger::Continue;
}

static void TheProviderSaysWhenTheMenuOpens() {
    InputState s;
    s.kind = InputKind::Editor;
    s.completionProvider = &TestCompletions;
    gCompleteCalls = 0;

    // The trigger is the *typed character* path — a paste is not a trigger,
    // which is what `InputTypeChar` being the one caller says.
    InputTypeChar(&s, nullptr, nullptr, 'b');
    utassert(s.completion.open);
    // The provider is asked once, to fill the menu that opened.
    utassert(gCompleteCalls == 1);
    // Without a trigger function, the built-in rule: a word character opens
    // one, and a colon is not one of the two it knows.
    InputTypeChar(&s, nullptr, nullptr, ':');
    utassert(!s.completion.open);
    // A keystroke that closes the menu does not query the provider: the
    // decision is the trigger's, and it is made before anyone is asked.
    utassert(gCompleteCalls == 1);

    // With one, the provider's answer is what counts.
    s.completionTrigger = &TestTrigger;
    InputTypeChar(&s, nullptr, nullptr, ':');
    utassert(s.completion.open);
    utassert(gCompleteCalls == 2);
    // And what it says to close on closes it, word character or not.
    InputTypeChar(&s, nullptr, nullptr, '#');
    utassert(!s.completion.open);
    utassert(gCompleteCalls == 2);
}

static void DocumentationIsResolvedOnce() {
    InputState s;
    s.kind = InputKind::Editor;
    s.completionProvider = &TestCompletions;
    gResolveCalls = 0;
    InputTypeChar(&s, nullptr, nullptr, 'b');
    utassert(s.completion.open);

    // With no resolver, an item that came thin stays thin.
    utassert(InputCompletionDocumentation(&s).len == 0);
    utassert(gResolveCalls == 0);

    s.completionResolve = &TestResolve;
    Str doc = InputCompletionDocumentation(&s);
    utassert(base::StrEq(doc, StrL("Exit a loop immediately.")));
    utassert(gResolveCalls == 1);
    // Asked once: the answer is written back into the item, and the frame
    // after this one reads it rather than asking again.
    utassert(base::StrEq(InputCompletionDocumentation(&s),
                         StrL("Exit a loop immediately.")));
    utassert(gResolveCalls == 1);
}

// ─── inline completion (lsp/completions.rs) ──────────────────────────────

static int gInlineCalls = 0;

static Str TestInlineCompletion(void* data, Arena* a, Str text, int offset) {
    (void)data;
    (void)text;
    (void)offset;
    gInlineCalls++;
    return StrDup(a, StrL(" world"));
}

static Str LongInlineCompletion(void* data, Arena* a, Str text, int offset) {
    (void)text;
    (void)offset;
    int n = (int)(intptr_t)data;
    char* out = (char*)Alloc(a, n);
    if (!out) {
        return {};
    }
    memset(out, 'x', (size_t)n);
    return Str(out, n);
}

// The provider is asked once the typing has stopped, and not before.
// completion_inserting: the write the editor makes on the reader's behalf is
// not typing, so it asks for no suggestion.
static void AnInsertIsNotTyping() {
    InputState s;
    s.kind = InputKind::Editor;
    s.inlineCompletionProvider = &TestInlineCompletion;
    CompletionItem item = {};
    item.label = StrL("unwrap");
    InputPresentCompletionItems(&s, 0, StrL(""), &item, 1);
    InputClearInlineCompletion(&s);
    InputInsertCompletion(&s, nullptr, nullptr, &item, Selection{0, 0});
    utassert(ValueIs(s, "unwrap"));
    // Nothing was scheduled by it — `asked` is still where clearing left it.
    utassert(s.inlineCompletion.asked);
}

static void TheSuggestionWaitsForTheDebounce() {
    InputState s;
    s.kind = InputKind::Editor;
    s.inlineCompletionProvider = &TestInlineCompletion;
    gInlineCalls = 0;
    Type(&s, "hello");

    // The edit scheduled it; the debounce has not run, so nothing is asked
    // and the frame is told to come back.
    utassert(InputUpdateInlineCompletion(&s, false));
    utassert(gInlineCalls == 0);
    utassert(!InputHasInlineCompletion(&s));

    // Once it is up, the provider answers and the suggestion shows.
    s.inlineCompletion.dueAt = 0;
    utassert(!InputUpdateInlineCompletion(&s, false));
    utassert(gInlineCalls == 1);
    utassert(InputHasInlineCompletion(&s));
    // And it is asked once, not once a frame.
    utassert(!InputUpdateInlineCompletion(&s, false));
    utassert(gInlineCalls == 1);
}

// The two checks Rust makes on the far side of the timer.
static void ASuggestionThatMissedItsMomentIsDropped() {
    InputState s;
    s.kind = InputKind::Editor;
    s.inlineCompletionProvider = &TestInlineCompletion;
    gInlineCalls = 0;
    Type(&s, "hello");
    // The caret moved while the debounce ran.
    InputSetSelectedRange(&s, nullptr, nullptr, 2, 2);
    s.inlineCompletion.dueAt = 0;
    utassert(!InputUpdateInlineCompletion(&s, false));
    utassert(gInlineCalls == 0);
    utassert(!InputHasInlineCompletion(&s));

    // A completion menu open over the caret is the other one.
    Type(&s, "!");
    s.inlineCompletion.dueAt = 0;
    utassert(!InputUpdateInlineCompletion(&s, true));
    utassert(gInlineCalls == 0);
}

// Tab writes it in, escape says no to it, and an edit asks again.
static void TabAcceptsAndEscapeDeclines() {
    InputState s;
    s.kind = InputKind::Editor;
    s.inlineCompletionProvider = &TestInlineCompletion;
    Type(&s, "hello");
    s.inlineCompletion.dueAt = 0;
    InputUpdateInlineCompletion(&s, false);
    utassert(InputHasInlineCompletion(&s));

    // Escape consumes the key and drops the suggestion.
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Escape, false));
    utassert(!InputHasInlineCompletion(&s));
    utassert(ValueIs(s, "hello"));

    // With one showing, Tab writes it at the caret instead of indenting.
    gInlineCalls = 0;
    Type(&s, "!");
    s.inlineCompletion.dueAt = 0;
    InputUpdateInlineCompletion(&s, false);
    utassert(InputHasInlineCompletion(&s));
    utassert(InputAcceptInlineCompletion(&s, nullptr, nullptr));
    utassert(ValueIs(s, "hello! world"));
    utassert(!InputHasInlineCompletion(&s));
    // Accepting is an edit, which schedules the next question.
    utassert(!s.inlineCompletion.asked);
}

// Accepting clears the suggestion arena before it edits the document. A long
// suggestion therefore needs an owning copy too; the former 512-byte stack
// special case left a dangling pointer for anything larger.
static void ALongInlineCompletionSurvivesAcceptance() {
    InputState s;
    s.kind = InputKind::Editor;
    s.inlineCompletionProvider = &LongInlineCompletion;
    s.inlineCompletionData = (void*)(intptr_t)700;
    Type(&s, "a");
    s.inlineCompletion.dueAt = 0;
    InputUpdateInlineCompletion(&s, false);
    utassert(InputHasInlineCompletion(&s));
    utassert(InputAcceptInlineCompletion(&s, nullptr, nullptr));
    Str value = InputValue(&s);
    utassert(len(value) == 701);
    utassert(value.s[0] == 'a' && value.s[700] == 'x');
}

// ─── range semantic tokens (lsp/semantic_tokens.rs, its own tests) ────────

static const Str kSemanticLegend[] = {StrL("keyword"), StrL("comment")};

static void TheDeltaEncodingIsUnpacked() {
    // Two tokens: "keyword" at (0, 0..4) and "comment" at (1, 2..7).
    SemanticToken toks[2] = {};
    toks[0] = {0, 0, 4, 0, 0};
    toks[1] = {1, 2, 5, 1, 0};
    SemanticSpan out[4] = {};
    int n = SemanticTokensDecode(toks, 2, kSemanticLegend, 2, out, 4);
    utassert(n == 2);
    utassert(out[0].line == 0 && out[0].col == 0 && out[0].len == 4);
    utassert(base::StrEq(out[0].name, StrL("keyword")));
    // The second token's line is relative to the first, and its column
    // starts over because the line moved.
    utassert(out[1].line == 1 && out[1].col == 2 && out[1].len == 5);
    utassert(base::StrEq(out[1].name, StrL("comment")));
}

static void ATokenOutsideTheLegendIsSkipped() {
    SemanticToken tok = {0, 0, 3, 99, 0};
    SemanticSpan out[4] = {};
    utassert(SemanticTokensDecode(&tok, 1, kSemanticLegend, 2, out, 4) == 0);
}

static void OnlyTheVisibleTokensAreResolved() {
    Str text = StrL("SELECT * FROM users\n-- a comment line\n");
    SemanticSpan toks[2] = {};
    toks[0] = {0, 0, 6, StrL("keyword")};
    toks[1] = {1, 0, 17, StrL("comment")};
    SemanticRange out[4] = {};
    // A viewport over line 0 only (bytes 0..19).
    int n = SemanticTokensForRange(toks, 2, text, Selection{0, 19}, out, 4);
    utassert(n == 1);
    utassert(out[0].range.start == 0 && out[0].range.end == 6);
    utassert(base::StrEq(out[0].name, StrL("keyword")));
}

static void TheWindowIsBinarySearched() {
    // A hundred lines of "foo bar\n", one token over "foo" on each.
    TempStr buf = AllocStrTemp(800);
    for (int i = 0; i < 100; i++) {
        memcpy(buf.s + i * 8, "foo bar\n", 8);
    }
    Str text(buf.s, 800);
    SemanticSpan toks[100] = {};
    for (int i = 0; i < 100; i++) {
        toks[i] = {i, 0, 3, StrL("keyword")};
    }
    SemanticRange out[8] = {};
    const int lineBytes = 8;
    int start = 50 * lineBytes;
    int n = SemanticTokensForRange(toks, 100, text, Selection{start, start + 3},
                                   out, 8);
    utassert(n == 1);
    utassert(out[0].range.start == start && out[0].range.end == start + 3);
    // An empty viewport before every token windows nothing in.
    utassert(SemanticTokensForRange(toks, 100, text, Selection{0, 0}, out, 8) ==
             0);
}

static int ManySemanticTokens(void* data, Str text, Selection range,
                              SemanticToken* out, int cap) {
    (void)text;
    (void)range;
    int total = (int)(intptr_t)data;
    for (int i = 0; i < total && i < cap; i++) {
        out[i] = {0, (uint32_t)(i == 0 ? 0 : 1), 1, 0, 0};
    }
    return total;
}

static void SemanticTokenResponsesGrowPastTheOldBuffer() {
    const int total = 5000;
    Vec<char> text;
    VecReserve(text, total);
    text.len = total;
    memset(text.els, 'x', (size_t)total);

    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, Str(text.els, len(text)));
    s.semanticTokensProvider = &ManySemanticTokens;
    s.semanticTokensData = (void*)(intptr_t)total;
    s.semanticLegend = kSemanticLegend;
    s.nSemanticLegend = 2;
    InputUpdateSemanticTokens(&s);
    utassert(s.semanticTokens.len == total);
    utassert(s.semanticTokens[total - 1].col == total - 1);
}

// ─── go to definition (input/editor/lsp/definitions.rs) ───────────────────

// A provider that answers for one word: `Duration` is defined at the top of
// the document, and `Arc` is a page on the web.
static int gDefCalls = 0;

static int TestDefinitions(void* data, Arena* a, Str text, int offset,
                           DefinitionLink* out, int cap) {
    (void)data;
    (void)a;
    gDefCalls++;
    int wa = offset, wb = offset;
    if (!TextWordRangeAt(text, offset, &wa, &wb) || wa >= wb) {
        return 0;
    }
    Str word(text.s + wa, wb - wa);
    if (base::StrEq(word, StrL("Duration"))) {
        if (cap > 0 && out) {
            out[0].origin = {wa, wb};
            out[0].uri = Str{};
            out[0].target = {0, 8};
        }
        return 1;
    }
    if (base::StrEq(word, StrL("Arc"))) {
        if (cap > 0 && out) {
            out[0].origin = {wa, wb};
            out[0].uri =
                StrL("https://doc.rust-lang.org/std/sync/struct.Arc.html");
            out[0].target = {};
        }
        return 1;
    }
    return 0;
}

static int ManyDefinitions(void* data, Arena* a, Str text, int offset,
                           DefinitionLink* out, int cap) {
    (void)a;
    (void)text;
    (void)offset;
    int total = (int)(intptr_t)data;
    for (int i = 0; i < total && i < cap; i++) {
        out[i].origin = {0, 4};
        out[i].target = {i, i + 1};
    }
    return total;
}

static void DefinitionResponsesGrowPastTheOldBuffer() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("word"));
    s.definitionProvider = &ManyDefinitions;
    s.definitionData = (void*)(intptr_t)19;
    InputHoverDefinition(&s, 1);
    utassert(s.hoverDef.locations.len == 19);
    utassert(s.hoverDef.locations[18].target.start == 18);
}

static bool gShown = false;
static bool gShownExternal = false;

static bool TestShowDocument(void* data, Str uri, bool external,
                             Selection selection) {
    (void)data;
    (void)selection;
    // A local target names no document at all, which is what an empty uri
    // means — the host still gets first refusal on it.
    gShown = true;
    gShownExternal = external && len(uri) > 0;
    return true;
}

static void AHoveredSymbolIsAskedAboutOnce() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("Duration and Arc and other"));
    s.definitionProvider = &TestDefinitions;
    gDefCalls = 0;

    // The word at 2 is `Duration`, which the provider defines.
    InputHoverDefinition(&s, 2);
    utassert(gDefCalls == 1);
    utassert(s.hoverDef.locations.len == 1);
    utassert(s.hoverDef.symbolRange.start == 0 && s.hoverDef.symbolRange
                                                          .end == 8);

    // `is_same`: while the pointer stays inside the symbol it was asked
    // about, the provider is not asked again.
    InputHoverDefinition(&s, 5);
    utassert(gDefCalls == 1);

    // A word with no definition clears what was found, and asks.
    InputHoverDefinition(&s, 10);
    utassert(gDefCalls == 2);
    utassert(s.hoverDef.locations.len == 0);
}

static void ASecondaryClickFollowsTheDefinition() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("Duration and more Duration here"));
    s.definitionProvider = &TestDefinitions;
    utassert(InputCanGoToDefinition(&s));

    // The second `Duration`, at 18.
    InputHoverDefinition(&s, 20);
    utassert(s.hoverDef.locations.len == 1);

    // A plain click is not one: the caret placement below it stands.
    utassert(!InputClickDefinition(&s, nullptr, nullptr, 20, false));
    // Nor is a secondary click outside the symbol.
    utassert(!InputClickDefinition(&s, nullptr, nullptr, 2, true));
    // Inside it, the click is taken and the selection is the target.
    utassert(InputClickDefinition(&s, nullptr, nullptr, 20, true));
    utassert(RangeIs(s, 0, 8));
}

// definitions.rs on_action_go_to_definition (#3256): the keyboard action
// asks the provider about the caret, with no hover before it.
static void TheActionAsksAboutTheCaretWithoutAHover() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("Duration and more Duration here"));
    s.definitionProvider = &TestDefinitions;
    gDefCalls = 0;

    // A word the provider does not define: asked, and nothing moves.
    InputSetSelectedRange(&s, nullptr, nullptr, 14, 14);
    InputGoToDefinition(&s, nullptr, nullptr);
    utassert(gDefCalls == 1);
    utassert(RangeIs(s, 14, 14));

    // Inside one it does, it follows, never having been hovered.
    utassert(s.hoverDef.locations.len == 0);
    InputSetSelectedRange(&s, nullptr, nullptr, 20, 20);
    InputGoToDefinition(&s, nullptr, nullptr);
    utassert(gDefCalls == 2);
    utassert(RangeIs(s, 0, 8));
}

// window/showDocument: the host is asked first and can take it, which is the
// only way an external uri is reachable without opening a browser.
static void TheHostSeesTheDocumentFirst() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("Arc and Duration"));
    s.definitionProvider = &TestDefinitions;
    s.showDocument = &TestShowDocument;
    gShown = false;
    gShownExternal = false;

    InputHoverDefinition(&s, 1);
    utassert(s.hoverDef.locations.len == 1);
    utassert(InputClickDefinition(&s, nullptr, nullptr, 1, true));
    utassert(gShown);
    // An http(s) target is a page rather than a document, and the host is
    // told which it is.
    utassert(gShownExternal);
    // The host took it, so nothing moved in this document.
    utassert(RangeIs(s, 0, 0));

    // A local one is not external, and the host still gets first refusal.
    gShown = false;
    InputHoverDefinition(&s, 9);
    utassert(InputClickDefinition(&s, nullptr, nullptr, 9, true));
    utassert(gShown && !gShownExternal);
    utassert(RangeIs(s, 0, 0));
}

// A single-line field's line is the whole document; a textarea's is not.
static void LineBoundaries() {
    InputState one;
    InputSetValue(&one, StrL("hello world"));
    InputSetSelectedRange(&one, nullptr, nullptr, 4, 4);
    utassert(InputStartOfLine(&one) == 0);
    utassert(InputEndOfLine(&one) == 11);

    InputState many;
    many.kind = InputKind::Textarea;
    InputSetValue(&many, StrL("one\ntwo\nthree"));
    InputSetSelectedRange(&many, nullptr, nullptr, 5, 5);
    utassert(InputStartOfLine(&many) == 4);
    utassert(InputEndOfLine(&many) == 7);

    // A code editor answers its wrapped row's ends first, but the row is
    // measured against the window that laid it out: with none, the answer is
    // the logical line, which is what every field without a frame on screen
    // gets.
    InputState code;
    code.kind = InputKind::Editor;
    code.softWrap = true;
    InputSetValue(&code, StrL("one\ntwo\nthree"));
    InputSetSelectedRange(&code, nullptr, nullptr, 5, 5);
    utassert(InputStartOfLine(&code) == 4);
    utassert(InputEndOfLine(&code) == 7);

    // Down keeps the column, and coming back up returns to it even after
    // passing through a shorter line.
    InputSetSelectedRange(&many, nullptr, nullptr, 12, 12); // "three", col 4
    Act(&many, InputAction::MoveUp);
    utassert(InputCursor(&many) == 7); // "two" is shorter, so its end
    Act(&many, InputAction::MoveDown);
    utassert(InputCursor(&many) == 12);
}

// page_up / page_down: the viewport height in display rows, not
// LayoutModeRows. A code editor's rows stay 1, which used to make PageDown
// a one-line move.
static void PageMovesByTheViewport() {
    InputState s;
    s.kind = InputKind::Editor;
    s.mode.kind = LayoutModeKind::CodeEditor;
    InputSetValue(&s, StrL("0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n"
                           "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
                           "20\n21\n22\n23\n24\n25\n26\n27\n28\n29"));
    s.lastLineH = 20;
    s.viewH = 200;
    s.inputBounds = {0, 0, 400, 200};
    InputSetSelectedRange(&s, nullptr, nullptr, 0, 0);
    Act(&s, InputAction::MovePageDown);
    utassert(InputOffsetToPoint(&s, InputCursor(&s)).row == 10);
    Act(&s, InputAction::MovePageUp);
    utassert(InputOffsetToPoint(&s, InputCursor(&s)).row == 0);
    // A single-line field leaves the key for whatever is around it.
    InputState one;
    utassert(!InputPerform(&one, nullptr, nullptr, InputAction::MovePageDown,
                           false));
}

// Boundaries step whole characters, not bytes.
static void BoundariesStepCharacters() {
    InputState s;
    InputSetValue(&s, StrL("a中b"));
    utassert(InputNextBoundary(&s, 0) == 1);
    utassert(InputNextBoundary(&s, 1) == 4);
    utassert(InputPreviousBoundary(&s, 4) == 1);
    utassert(InputPreviousBoundary(&s, 1) == 0);

    Act(&s, InputAction::MoveToEnd);
    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, "a中"));
    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, "a"));
}

static void SelectionFollowsTheDragDirection() {
    InputState s;
    InputSetValue(&s, StrL("hello world"));
    InputMoveTo(&s, nullptr, nullptr, 5);

    Act(&s, InputAction::SelectRight);
    utassert(RangeIs(s, 5, 6));
    utassert(InputCursor(&s) == 6);

    // Back past the anchor: the live end flips to the other side.
    Act(&s, InputAction::SelectLeft);
    Act(&s, InputAction::SelectLeft);
    utassert(RangeIs(s, 4, 5));
    utassert(InputCursor(&s) == 4);
}

// select_word / select_line, which is what a double and a triple click take.
static void SelectWordAndLine() {
    InputState s;
    s.kind = InputKind::Textarea;
    InputSetValue(&s, StrL("hello brave\nnew world"));

    InputSelectWord(&s, nullptr, nullptr, 7);
    utassert(base::StrEq(InputSelectedValue(&s), StrL("brave")));

    InputSelectLine(&s, nullptr, nullptr, 14);
    utassert(base::StrEq(InputSelectedValue(&s), StrL("new world")));
}

// The word a double click took stays whole while the drag goes on.
static void DraggingCannotEatIntoTheSelectedWord() {
    InputState s;
    InputSetValue(&s, StrL("hello brave world"));
    InputSelectWord(&s, nullptr, nullptr, 7);
    utassert(RangeIs(s, 6, 11));

    InputSelectTo(&s, nullptr, nullptr, 8);
    utassert(RangeIs(s, 6, 11));

    InputSelectTo(&s, nullptr, nullptr, 15);
    utassert(RangeIs(s, 6, 15));
}

// readonly and disabled reject what the user does, not what the program does.
static void ReadonlyRejectsUserEditsOnly() {
    InputState s;
    InputSetValue(&s, StrL("hello"));
    s.readonly = true;

    Type(&s, "X");
    utassert(ValueIs(s, "hello"));
    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, "hello"));
    // Typing and the input method go through the same handler, so a readonly
    // field refuses a composition as flatly as it refuses a keystroke.
    Mark(&s, "\xE3\x81\x82"); // U+3042 HIRAGANA A
    utassert(ValueIs(s, "hello"));
    utassert(MarkIs(s, -1, -1));

    InputSetValue(&s, StrL("set anyway"));
    utassert(ValueIs(s, "set anyway"));
    InputInsert(&s, nullptr, nullptr, StrL("!"));
    utassert(ValueIs(s, "set anyway!"));

    // replace() writes over the selection where insert() writes at the caret.
    InputSetSelectedRange(&s, nullptr, nullptr, 0, 3);
    InputReplace(&s, nullptr, nullptr, StrL("got"));
    utassert(ValueIs(s, "got anyway!"));
    utassert(InputCursor(&s) == 3);

    // cursor_position() counts characters, where the point counts bytes.
    InputState ta;
    ta.kind = InputKind::Textarea;
    InputSetValue(&ta, StrL("a\n\xE4\xBD\xA0\xE5\xA5\xBDx")); // "a\n你好x"
    InputMoveTo(&ta, nullptr, nullptr, len(InputValue(&ta)));
    RopePoint at = InputCursorPosition(&ta);
    utassert(at.row == 1 && at.column == 3);
    utassert(InputOffsetToPoint(&ta, InputCursor(&ta)).column == 7);
}

// Enter is a submit in a single-line field and a newline in a textarea,
// unless the textarea submits on Enter — then only Shift+Enter breaks a line.
static void EnterInsertsANewlineOnlyWhereItShould() {
    InputState one;
    utassert(!InputPerform(&one, nullptr, nullptr, InputAction::Enter, false));
    utassert(ValueIs(one, ""));

    InputState many;
    many.kind = InputKind::Textarea;
    utassert(InputPerform(&many, nullptr, nullptr, InputAction::Enter, false));
    utassert(ValueIs(many, "\n"));

    InputState chat;
    chat.kind = InputKind::Textarea;
    chat.submitOnEnter = true;
    utassert(!InputPerform(&chat, nullptr, nullptr, InputAction::Enter, false));
    utassert(ValueIs(chat, ""));
    utassert(InputPerform(&chat, nullptr, nullptr, InputAction::Enter, true));
    utassert(ValueIs(chat, "\n"));
}

// indent.rs. Tab indents a multi-line field and shift-tab takes it back; a
// single-line one does not handle either, which is what lets the window walk
// the focus ring with the same key.
static void TabIndentsOnlyWhereThereIsSomethingToIndent() {
    InputState one;
    Type(&one, "ab");
    utassert(!InputPerform(&one, nullptr, nullptr, InputAction::IndentInline,
                           false));
    utassert(ValueIs(one, "ab"));

    InputState grow;
    grow.kind = InputKind::Textarea;
    grow.mode.kind = LayoutModeKind::AutoGrow;
    utassert(!InputPerform(&grow, nullptr, nullptr, InputAction::IndentInline,
                           false));

    // No selection: one tab at the caret, which the caret then sits after.
    InputState s;
    s.kind = InputKind::Textarea;
    InputSetValue(&s, StrL("one\ntwo"));
    InputSetSelectedRange(&s, nullptr, nullptr, 4, 4);
    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::IndentInline, false));
    utassert(ValueIs(s, "one\n    two"));
    utassert(RangeIs(s, 8, 8));

    // And back, from anywhere on the line.
    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::OutdentInline, false));
    utassert(ValueIs(s, "one\ntwo"));
    utassert(RangeIs(s, 4, 4));
    // A line with no indent left is left alone.
    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::OutdentInline, false));
    utassert(ValueIs(s, "one\ntwo"));

    // Each of the two is one undo step, whole.
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "one\n    two"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "one\ntwo"));
}

// A selection pushes every line it touches over, and its ends ride along
// with the text they sit in (compute_block_indent maps both through the
// edits before them).
static void TabIndentsEveryLineOfASelection() {
    InputState s;
    s.kind = InputKind::Textarea;
    InputSetValue(&s, StrL("one\ntwo\nthree"));
    // From the middle of "one" into the middle of "two".
    InputSetSelectedRange(&s, nullptr, nullptr, 1, 6);
    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::IndentInline, false));
    utassert(ValueIs(s, "    one\n    two\nthree"));
    utassert(RangeIs(s, 5, 14));

    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::OutdentInline, false));
    utassert(ValueIs(s, "one\ntwo\nthree"));
    // And back: the outdent maps the ends the other way, to where they were.
    utassert(RangeIs(s, 1, 6));

    // One undo step per indent, whatever it touched.
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "    one\n    two\nthree"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "one\ntwo\nthree"));
}

// The block pair on ctrl-] / ctrl-[ moves whole lines: a caret halfway
// along one still indents the line, where tab would have put the tab where
// the caret is.
static void TheBlockPairMovesTheWholeLine() {
    InputState s;
    s.kind = InputKind::Textarea;
    InputSetValue(&s, StrL("one\ntwo"));
    InputSetSelectedRange(&s, nullptr, nullptr, 6, 6);
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Indent, false));
    utassert(ValueIs(s, "one\n    two"));
    // The caret rode along with the text it sits in.
    utassert(RangeIs(s, 10, 10));

    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Outdent, false));
    utassert(ValueIs(s, "one\ntwo"));
    utassert(RangeIs(s, 6, 6));

    // A selection is the same for both pairs.
    InputSetSelectedRange(&s, nullptr, nullptr, 1, 6);
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Indent, false));
    utassert(ValueIs(s, "    one\n    two"));
    utassert(RangeIs(s, 5, 14));

    // And a single-line field has nothing to indent, whichever pair asks.
    InputState one;
    Type(&one, "ab");
    utassert(!InputPerform(&one, nullptr, nullptr, InputAction::Indent, false));
    utassert(
        !InputPerform(&one, nullptr, nullptr, InputAction::Outdent, false));
    utassert(ValueIs(one, "ab"));
}

// A mask rejects a character it has no room for and reformats as it fills.
static void MaskFormatsWhileTyping() {
    InputState s;
    InputSetMaskPattern(&s, MaskPatternNew(StrL("(999)999-9999")));
    // The cue comes from the pattern.
    utassert(base::StrEq(s.placeholder, StrL("(___)___-____")));

    Type(&s, "1");
    utassert(ValueIs(s, "(1"));
    // A separator only appears once something follows it, so the ")" is
    // not written until the fourth digit arrives.
    Type(&s, "23");
    utassert(ValueIs(s, "(123"));
    Type(&s, "4567890");
    utassert(ValueIs(s, "(123)456-7890"));

    // A letter has no token to land on, so the edit is rejected.
    Type(&s, "A");
    utassert(ValueIs(s, "(123)456-7890"));
}

// The keymap state.rs::init installs. Two chords in it are not the same key
// on every platform, so the test says which modifier it means rather than
// spelling one of them: `Sec` is the shortcut modifier — Command on macOS,
// Control elsewhere — and `Word` is the word-wise one, which is the other.
static InputAction Sec(const InputState* s, int vk, bool shift) {
#if GPUI_OS_MAC
    return InputActionForKey(s, vk, shift, false, false, true);
#else
    return InputActionForKey(s, vk, shift, true, false, false);
#endif
}

static InputAction Word(const InputState* s, int vk, bool shift) {
#if GPUI_OS_MAC
    return InputActionForKey(s, vk, shift, false, true, false);
#else
    return InputActionForKey(s, vk, shift, true, false, false);
#endif
}

static void ActionForKey() {
    InputState s;
    utassert(InputActionForKey(&s, KeyPageUp, false, false, false) ==
             InputAction::MovePageUp);
    utassert(InputActionForKey(&s, KeyPageDown, false, false, false) ==
             InputAction::MovePageDown);
    utassert(InputActionForKey(&s, KeyLeft, false, false, false) ==
             InputAction::MoveLeft);
    utassert(InputActionForKey(&s, KeyLeft, true, false, false) ==
             InputAction::SelectLeft);
    utassert(Word(&s, KeyLeft, false) == InputAction::MoveToPreviousWord);
    utassert(Word(&s, KeyRight, true) == InputAction::SelectToNextWordEnd);
    utassert(InputActionForKey(&s, KeyHome, false, false, false) ==
             InputAction::MoveHome);
    utassert(InputActionForKey(&s, KeyHome, true, false, false) ==
             InputAction::SelectToStartOfLine);
    // The document ends: cmd-up / cmd-down on macOS, ctrl-home / ctrl-end
    // elsewhere. state.rs binds the first pair and this tree adds the second,
    // which upstream leaves unbound off macOS.
#if GPUI_OS_MAC
    utassert(Sec(&s, KeyUp, false) == InputAction::MoveToStart);
    utassert(Sec(&s, KeyDown, false) == InputAction::MoveToEnd);
#else
    utassert(Sec(&s, KeyHome, false) == InputAction::MoveToStart);
    utassert(Sec(&s, KeyEnd, false) == InputAction::MoveToEnd);
#endif
    utassert(Word(&s, KeyBack, false) ==
             InputAction::DeleteToPreviousWordStart);
    utassert(InputActionForKey(&s, KeyDelete, false, false, false) ==
             InputAction::Delete);
    utassert(Sec(&s, KeyA, false) == InputAction::SelectAll);
    utassert(Sec(&s, KeyZ, false) == InputAction::Undo);
    utassert(Sec(&s, KeyZ, true) == InputAction::Redo);
#if GPUI_OS_MAC
    // cmd-shift-z, and cmd-y is not a chord at all.
    utassert(Sec(&s, KeyY, false) == InputAction::None);
#else
    utassert(Sec(&s, KeyY, false) == InputAction::Redo);
#endif
    utassert(Sec(&s, KeyC, false) == InputAction::Copy);
    utassert(Sec(&s, KeyV, false) == InputAction::Paste);
    utassert(Sec(&s, KeyX, false) == InputAction::Cut);

    // On a Mac, Control is not the shortcut key and none of those answer to
    // it: state.rs binds ctrl-backspace and cmd-backspace to different
    // actions in the same context, which only works if the two stay apart.
#if GPUI_OS_MAC
    // Control is not the shortcut key here; it carries the emacs bindings
    // state.rs adds in its macOS half, which is why the two have to stay
    // apart. ctrl-c is nothing at all.
    utassert(InputActionForKey(&s, KeyC, false, true, false) ==
             InputAction::None);
    utassert(InputActionForKey(&s, KeyA, false, true, false) ==
             InputAction::MoveHome);
    utassert(InputActionForKey(&s, KeyE, false, true, false) ==
             InputAction::MoveEnd);
    utassert(InputActionForKey(&s, KeyA, true, true, false) ==
             InputAction::SelectToStartOfLine);
    utassert(InputActionForKey(&s, KeyE, true, true, false) ==
             InputAction::SelectToEndOfLine);
    // ctrl-backspace and cmd-backspace are two chords with two actions —
    // the pair that could not be told apart before.
    utassert(InputActionForKey(&s, KeyBack, false, true, false) ==
             InputAction::Backspace);
    utassert(InputActionForKey(&s, KeyBack, false, false, false, true) ==
             InputAction::DeleteToBeginningOfLine);
    utassert(InputActionForKey(&s, KeyDelete, false, false, false, true) ==
             InputAction::DeleteToEndOfLine);
#endif
    // Without the modifier a letter is text, not an action.
    utassert(InputActionForKey(&s, KeyA, false, false, false) ==
             InputAction::None);
    utassert(InputActionForKey(&s, KeyTab, false, false, false) ==
             InputAction::IndentInline);
    utassert(InputActionForKey(&s, KeyTab, true, false, false) ==
             InputAction::OutdentInline);
    // A modified tab belongs to whatever is outside the field.
    utassert(InputActionForKey(&s, KeyTab, false, true, false) ==
             InputAction::None);
    utassert(InputActionForKey(&s, KeyTab, false, false, false, true) ==
             InputAction::None);
    // cmd-] / cmd-[ on macOS, ctrl-] / ctrl-[ elsewhere.
    utassert(Sec(&s, KeyRightBracket, false) == InputAction::Indent);
    utassert(Sec(&s, KeyLeftBracket, false) == InputAction::Outdent);
    // Without the shortcut modifier a bracket is text.
    utassert(InputActionForKey(&s, KeyLeftBracket, false, false, false) ==
             InputAction::None);
}

// mode.rs LayoutMode: rows, and the clamp an auto-growing one applies.
static void LayoutModeRowsClamp() {
    LayoutMode plain;
    plain.rows = 5;
    utassert(LayoutModeRows(plain) == 5);
    utassert(LayoutModeMinRows(plain) == 1);

    LayoutMode grow;
    grow.kind = LayoutModeKind::AutoGrow;
    grow.minRows = 2;
    grow.maxRows = 5;
    grow.rows = 2;
    utassert(LayoutModeRows(grow) == 2);
    utassert(LayoutModeMinRows(grow) == 2);

    LayoutModeSetRows(&grow, 4);
    utassert(LayoutModeRows(grow) == 4);
    LayoutModeSetRows(&grow, 1);
    utassert(LayoutModeRows(grow) == 2);
    LayoutModeSetRows(&grow, 10);
    utassert(LayoutModeRows(grow) == 5);
}

// kind.rs: the kind decides whether an input is multi-line, not the row count.
static void KindDoesNotFollowTheRowCount() {
    InputState s;
    s.kind = InputKind::Textarea;
    s.mode.kind = LayoutModeKind::AutoGrow;
    s.mode.minRows = 1;
    s.mode.maxRows = 1;
    utassert(InputIsMultiLine(&s));

    InputState one;
    one.mode.rows = 4;
    utassert(InputIsSingleLine(&one));
}

// A field twenty lines tall inside a box that shows five of them.
static void SeedScroll(InputState* s) {
    s->lastLineH = 20;
    s->viewH = 100;
    s->viewW = 200;
    s->contentH = 400;
    s->contentW = 600;
}

static void ScrollToBringsTheCaretIntoView() {
    InputState s;
    SeedScroll(&s);
    // A caret inside the box moves nothing.
    InputScrollToCaret(&s, 0, 40, InputMoveDir::None);
    utassertnear(s.scrollY, 0.f);

    // Past the bottom: the line comes in with a line's clearance under it.
    InputScrollToCaret(&s, 0, 200, InputMoveDir::None);
    utassertnear(s.scrollY, 140.f);

    // Back above the top: a line's clearance over it.
    InputScrollToCaret(&s, 0, 100, InputMoveDir::None);
    utassertnear(s.scrollY, 80.f);
}

// test_edit_reveals_far_offscreen_caret: an edit at a caret far outside the
// viewport reveals it at once. Rust's cursor-follow in layout_cursors used to
// step one line per changed selection; upstream 03490654 puts the caret's
// line at the edge instead. Here an edit reveals through scroll_to directly.
static void AnEditRevealsAFarOffscreenCaret() {
    InputState s;
    s.kind = InputKind::Textarea;
    s.mode.kind = LayoutModeKind::AutoGrow;
    s.mode.minRows = 1;
    s.mode.maxRows = 6;
    StrBuilder text;
    for (int i = 1; i <= 100; i++) {
        text.Append(StrDup(fmt(i < 100 ? "line %d\n" : "line %d", i)));
    }
    InputSetValue(&s, text.TakeStr());
    s.lastLineH = 20;
    s.viewH = 120;
    s.viewW = 700;
    s.contentH = 100 * 20.f;
    s.contentW = 700;
    int end = len(InputValue(&s));
    s.selectedRange = SelectionAt(end);
    // The reader scrolled back to the top.
    s.scrollY = 0;
    InputTypeChar(&s, nullptr, nullptr, 'X');
    utassert(StrEndsWith(InputValue(&s), StrL("line 100X")));
    float caretTop = 99 * 20.f - s.scrollY;
    utassert(caretTop >= 0 && caretTop + 20.f <= s.viewH);
}

static void AVerticalWalkDoesNotFightItself() {
    InputState s;
    SeedScroll(&s);
    s.scrollY = 140;
    // Rust clamps the answer by the direction the caret went: a move up is
    // never answered by scrolling down...
    InputScrollToCaret(&s, 0, 300, InputMoveDir::Up);
    utassertnear(s.scrollY, 140.f);
    // ...and a move down is never answered by scrolling up.
    InputScrollToCaret(&s, 0, 40, InputMoveDir::Down);
    utassertnear(s.scrollY, 140.f);
}

static void TheOffsetStaysInsideTheContent() {
    InputState s;
    SeedScroll(&s);
    // The last line cannot pull the view past the end of the text.
    InputScrollToCaret(&s, 0, 10000, InputMoveDir::None);
    utassertnear(s.scrollY, 300.f);
    // Nor can the first pull it above the start.
    InputScrollToCaret(&s, 0, 0, InputMoveDir::None);
    utassertnear(s.scrollY, 0.f);
}

static void EmptyBottomHeightMatchesRust() {
    // crates/base/src/input/base/element.rs empty_bottom_height.
    float lineH = 20;
    for (int rows : {-1, 0, 3, 99}) {
        utassertnear(InputEmptyBottomHeight(false, rows, 800, lineH), 0.f);
    }
    utassertnear(InputEmptyBottomHeight(true, -1, 800, lineH), 400.f);
    utassertnear(InputEmptyBottomHeight(true, -1, 40, lineH), 60.f);
    for (int rows : {0, 1, 3, 8, 64}) {
        float want = (float)rows * lineH;
        utassertnear(InputEmptyBottomHeight(true, rows, 800, lineH), want);
        utassertnear(InputEmptyBottomHeight(true, rows, 20, lineH), want);
    }
}

static void CursorSurroundingPaddingMatchesRust() {
    float lineH = 20;
    for (int lines : {-1, 0, 3, 99}) {
        for (int visible : {0, 1, 8, 64}) {
            utassertnear(
                InputCursorSurroundingPadding(true, lines, visible, lineH),
                lineH);
        }
    }
    int fewVisible = 3 * 8 - 1;
    utassertnear(InputCursorSurroundingPadding(false, -1, fewVisible, lineH),
                 lineH);
    utassertnear(InputCursorSurroundingPadding(false, -1, 24, lineH),
                 3.f * lineH);
    utassertnear(InputCursorSurroundingPadding(false, -1, 100, lineH),
                 3.f * lineH);

    utassertnear(InputCursorSurroundingPadding(false, 50, 10, lineH), 100.f);
    utassertnear(InputCursorSurroundingPadding(false, 3, 40, lineH), 60.f);
}

static void CodeEditorSurroundingUsesTheOverride() {
    InputState s;
    SeedScroll(&s);
    s.mode.kind = LayoutModeKind::CodeEditor;
    s.cursorSurroundingLines = 3;
    s.viewH = 200;
    s.contentH = 800;
    InputScrollToCaret(&s, 0, 180, InputMoveDir::Down);
    utassertnear(s.scrollY, 40.f);
}

// state.rs: test_next_search_match_reveals_with_padding_after_manual_scroll
// and its previous_search_match twin. Match order does not describe the
// viewport's direction once the reader has scrolled by hand, so Next has to be
// allowed to scroll up and Previous down, and both keep the configured
// surrounding-line padding.
static void SearchRevealsWithPaddingAfterManualScroll(bool previous) {
    App app;
    Window* win = new Window();
    win->app = &app;
    InputState s;
    s.kind = InputKind::Editor;
    s.mode.kind = LayoutModeKind::CodeEditor;
    s.searchable = true;
    s.cursorSurroundingLines = 3;
    const float lineH = 20;
    s.lastLineH = lineH;
    s.viewH = 200;
    s.viewW = 400;
    s.contentW = 400;
    StrBuilder text;
    for (int row = 0; row < 160; row++) {
        if (row > 0) text.Append(StrL("\n"));
        if (row == 20 || row == 60 || row == 100) {
            text.Append(fmt("match on row %d", row));
        } else {
            text.Append(fmt("line %d", row));
        }
    }
    Str value = text.TakeStr();
    InputSetValue(&s, value);
    s.contentH = 160 * lineH;
    InputSetSearchQuery(&s, &app, win, StrL("match"), true);
    if (previous) {
        Selection skip = {};
        SearchMatcherNext(&s.search.matcher, &skip);
        SearchMatcherNext(&s.search.matcher, &skip);
    }
    // The reader scrolled by hand: the second match is above the viewport
    // for Next, below it for Previous.
    s.scrollY = previous ? 0 : lineH * 80;

    Selection range = {};
    bool moved = previous ? InputSearchPrev(&s, &app, win, &range)
                          : InputSearchNext(&s, &app, win, &range);
    utassert(moved);
    int start = StrFind(value, StrL("match on row 60"));
    utassert(range.start == start && range.end == start + 5);
    utassert(SearchMatcherIndex(&s.search.matcher) == 1);

    // Row 60 is inside the box with three lines of clearance, the matched
    // line included.
    float targetY = lineH * 60 - s.scrollY;
    utassert(targetY >= lineH * 2 - 0.1f);
    utassert(targetY + lineH * 3 <= s.viewH + 0.1f);
    StrFree(value);
    delete win;
    EntityDropAll(&app);
}

static void SearchNavigationRevealsTheMatchAfterAManualScroll() {
    SearchRevealsWithPaddingAfterManualScroll(false);
    SearchRevealsWithPaddingAfterManualScroll(true);
}

static void ASidewaysCaretPullsTheRunAcross() {
    InputState s;
    SeedScroll(&s);
    // Past the right edge, with the margin Rust keeps.
    InputScrollToCaret(&s, 400, 0, InputMoveDir::None);
    utassertnear(s.scrollX, 205.f);
    // And back to the left edge.
    InputScrollToCaret(&s, 100, 0, InputMoveDir::None);
    utassertnear(s.scrollX, 95.f);
    // Never past the end of the run.
    InputScrollToCaret(&s, 100000, 0, InputMoveDir::None);
    utassertnear(s.scrollX, 400.f);
}

static void TheNumberKeysStepTheField() {
    StepAction action = StepAction::Decrement;
    utassert(NumberStepForKey(KeyUp, &action));
    utassert(action == StepAction::Increment);
    utassert(NumberStepForKey(KeyDown, &action));
    utassert(action == StepAction::Decrement);
    // Anything else is the field's own.
    utassert(!NumberStepForKey(KeyLeft, &action));
    utassert(!NumberStepForKey(KeyReturn, &action));
}

// replace_and_mark_text_in_range: each candidate stands in for the last, and
// the range that is marked is what the next one replaces. The Rust case is
// `undo_with_ime_input`, typed the way a pinyin IME types 你.
static void ACompositionReplacesItselfUntilItCommits() {
    InputState s;
    Type(&s, "prefix ");
    Mark(&s, "n");
    utassert(ValueIs(s, "prefix n"));
    utassert(MarkIs(s, 7, 8));
    Mark(&s, "ni");
    utassert(ValueIs(s, "prefix ni"));
    utassert(MarkIs(s, 7, 9));
    Mark(&s, "\xE4\xBD\xA0"); // U+4F60, three bytes
    utassert(ValueIs(s, "prefix \xE4\xBD\xA0"));
    utassert(MarkIs(s, 7, 10));
    // The caret sits at the end of the marked run while it is being composed.
    utassert(RangeIs(s, 10, 10));

    InputUnmarkText(&s, nullptr, nullptr);
    utassert(MarkIs(s, -1, -1));
    Type(&s, " suffix");
    utassert(ValueIs(s, "prefix \xE4\xBD\xA0 suffix"));
}

// The whole composition is one undo step: the candidates were staging posts,
// not edits the user made.
static void ACompositionUndoesAsOneThing() {
    InputState s;
    Type(&s, "prefix ");
    Mark(&s, "n");
    Mark(&s, "ni");
    Mark(&s, "\xE4\xBD\xA0");
    InputUnmarkText(&s, nullptr, nullptr);
    Type(&s, " suffix");
    utassert(ValueIs(s, "prefix \xE4\xBD\xA0 suffix"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "prefix \xE4\xBD\xA0"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "prefix "));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, ""));
}

// An empty insert is the composition being abandoned: the staged text goes,
// the caret goes back where it started, and nothing is left marked.
static void AnAbandonedCompositionLeavesNothingBehind() {
    InputState s;
    Type(&s, "ab");
    Mark(&s, "ni");
    utassert(ValueIs(s, "abni"));
    Mark(&s, "");
    utassert(ValueIs(s, "ab"));
    utassert(RangeIs(s, 2, 2));
    utassert(MarkIs(s, -1, -1));
}

// Two compositions in a row are two undo steps, and what is typed after one
// is a third. The commit ends the transaction: neither platform follows a
// confirmed candidate with an unmark, and a transaction left open would swallow
// everything typed after it.
static void ConsecutiveCompositionsUndoSeparately() {
    InputState s;
    Mark(&s, "j");
    Mark(&s, "jin");
    // The commit: a replace with no range of its own, which is what
    // `insertText:` and GCS_RESULTSTR come to.
    Type(&s, "ä»å¤©"); // 今天
    Mark(&s, "w");
    Mark(&s, "wo");
    Type(&s, "æä»¬"); // 我们
    utassert(ValueIs(s, "ä»å¤©æä»¬"));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "ä»å¤©"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, ""));
    Act(&s, InputAction::Redo);
    utassert(ValueIs(s, "ä»å¤©"));
    Act(&s, InputAction::Redo);
    utassert(ValueIs(s, "ä»å¤©æä»¬"));
}

// A commit that names no range of its own replaces the marked text rather
// than the selection — which is how the platform hands over a result string.
static void ACommitReplacesWhatWasMarked() {
    InputState s;
    Type(&s, "ab");
    Mark(&s, "ni");
    Type(&s, "\xE4\xBD\xA0");
    utassert(ValueIs(s, "ab\xE4\xBD\xA0"));
    utassert(MarkIs(s, -1, -1));
}

// ─── completion ───────────────────────────────────────────────────────────
//
// The menu an editor puts up while a word is being typed: what the provider
// is asked, what the keys do to it, and what accepting one writes. Rust's own
// are in `completion_menu.rs` behind a `VisualTestContext`; these drive the
// state the same way a keystroke does.

static const CompletionItem kItems[] = {
    {StrL("const"), StrL("const NAME: Type"), {}, StrL("A constant."), false},
    {StrL("continue"), {}, {}, {}, false},
    {StrL("core"), {}, StrL("core::"), {}, false},
    {StrL("fn"), {}, {}, {}, false},
};

// A provider that answers the labels starting with the query, and counts how
// often it was asked — which is what says a keystroke opened the menu rather
// than the test doing it by hand.
static int Complete(void* data, Str, int, Str query, CompletionItem* out,
                    int cap) {
    if (data) {
        (*(int*)data)++;
    }
    int n = 0;
    for (const CompletionItem& item : kItems) {
        if (len(query) > len(item.label)) {
            continue;
        }
        if (len(query) > 0 && !StrEq(Str(item.label.s, len(query)), query)) {
            continue;
        }
        if (n < cap && out) {
            out[n] = item;
        }
        n++;
    }
    return n;
}

static void TypingAWordOpensTheMenu() {
    int asked = 0;
    InputState s;
    s.kind = InputKind::Editor;
    s.completionProvider = &Complete;
    s.completionData = &asked;

    TypeChars(&s, "co");
    utassert(asked == 2);
    utassert(s.completion.open);
    utassert(s.completion.items.len == 3); // const, continue, core
    utassert(s.completion.triggerStart == 0);
    utassert(s.completion.selected == 0);

    int start = -1;
    Str query = InputCompletionQuery(&s, &start);
    utassert(start == 0 && base::StrEq(query, StrL("co")));

    // A word nothing answers to closes it rather than showing an empty menu.
    TypeChars(&s, "zz");
    utassert(!s.completion.open);

    // And a field with no provider never opens one at all.
    InputState plain;
    TypeChars(&plain, "co");
    utassert(!plain.completion.open);
    utassert(ValueIs(plain, "co"));
}

static void TheMenuKeysMoveTheSelectionAndAccept() {
    InputState s;
    s.kind = InputKind::Editor;
    s.completionProvider = &Complete;

    TypeChars(&s, "co");
    utassert(Menu(&s, InputAction::MoveDown));
    utassert(s.completion.selected == 1);
    // Down at the end stays there rather than wrapping.
    utassert(Menu(&s, InputAction::MoveDown));
    utassert(Menu(&s, InputAction::MoveDown));
    utassert(s.completion.selected == 2);
    utassert(Menu(&s, InputAction::MoveUp));
    utassert(s.completion.selected == 1);

    // Enter writes the item over the word it was completing.
    utassert(Menu(&s, InputAction::Enter));
    utassert(ValueIs(s, "continue"));
    utassert(!s.completion.open);
    utassert(InputCursor(&s) == 8);

    // Escape closes it, and the keys go back to the editor once it is closed.
    InputSetValue(&s, Str{});
    TypeChars(&s, "f");
    utassert(s.completion.open);
    utassert(Menu(&s, InputAction::Escape));
    utassert(!s.completion.open);
    utassert(!Menu(&s, InputAction::Enter));
    utassert(!Menu(&s, InputAction::MoveDown));
}

static void AnAcceptedItemWritesItsInsertText() {
    InputState s;
    s.kind = InputKind::Editor;
    s.completionProvider = &Complete;

    InputSetValue(&s, StrL("x = "));
    InputSetSelectedRange(&s, nullptr, nullptr, 4, 4);
    TypeChars(&s, "cor");
    utassert(s.completion.open && s.completion.items.len == 1);
    utassert(s.completion.triggerStart == 4);
    utassert(Menu(&s, InputAction::Enter));
    utassert(ValueIs(s, "x = core::"));

    // completions.rs deleting_prefix_invalidates_pending_completion (#3256):
    // a deletion invalidates what the provider said, and triggers nothing,
    // so the menu goes; typing again asks about the word as it now stands.
    InputSetValue(&s, Str{});
    TypeChars(&s, "cor");
    utassert(s.completion.items.len == 1);
    InputPerform(&s, nullptr, nullptr, InputAction::Backspace, false);
    utassert(ValueIs(s, "co"));
    utassert(!s.completion.open);
    TypeChars(&s, "n");
    utassert(s.completion.open);
}

// ─── code actions ─────────────────────────────────────────────────────────

// TextConvertor's two simplest, which is enough to say what the menu does
// with what a provider offers.
static int Actions(void* data, Arena* a, Str text, Selection sel,
                   CodeActionItem* out, int cap) {
    if (data) {
        (*(int*)data)++;
    }
    if (sel.IsEmpty()) {
        return 0;
    }
    if (cap > 0 && out) {
        char* up = (char*)Alloc(a, sel.end - sel.start);
        for (int i = sel.start; i < sel.end; i++) {
            char c = text.s[i];
            up[i - sel.start] =
                c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
        }
        out[0].title = StrL("Convert to Uppercase");
        out[0].range = sel;
        out[0].newText = Str(up, sel.end - sel.start);
    }
    if (cap > 1 && out) {
        out[1].title = StrL("Delete");
        out[1].range = sel;
        out[1].newText = Str{};
    }
    return 2;
}

static void TheCodeActionMenuRewritesWhatIsSelected() {
    int asked = 0;
    InputState s;
    s.kind = InputKind::Editor;
    s.codeActionProvider = &Actions;
    s.codeActionData = &asked;

    InputSetValue(&s, StrL("hello world"));
    InputSetSelectedRange(&s, nullptr, nullptr, 0, 5);
    Act(&s, InputAction::ToggleCodeActions);
    utassert(asked == 1);
    utassert(s.codeActions.open && s.codeActions.items.len == 2);
    utassert(s.codeActions.selected == 0);

    // The chord again asks again and replaces the menu that is up
    // (handle_code_action_trigger), rather than putting it away.
    uint64_t revision = s.codeActions.revision;
    Act(&s, InputAction::ToggleCodeActions);
    utassert(asked == 2);
    utassert(s.codeActions.open && s.codeActions.items.len == 2);
    utassert(s.codeActions.revision != revision);

    // Down walks it and enter performs the one it is on.
    utassert(Menu2(&s, InputAction::MoveDown));
    utassert(s.codeActions.selected == 1);
    utassert(Menu2(&s, InputAction::MoveUp));
    utassert(Menu2(&s, InputAction::Enter));
    utassert(ValueIs(s, "HELLO world"));
    utassert(!s.codeActions.open);
    // One undo step, so the whole rewrite comes back at once.
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "hello world"));

    // Escape closes it, and an empty selection is nothing to offer, so the
    // menu does not come up at all.
    InputSetSelectedRange(&s, nullptr, nullptr, 0, 5);
    Act(&s, InputAction::ToggleCodeActions);
    utassert(s.codeActions.open);
    utassert(Menu2(&s, InputAction::Escape));
    utassert(!s.codeActions.open);
    InputSetSelectedRange(&s, nullptr, nullptr, 3, 3);
    Act(&s, InputAction::ToggleCodeActions);
    utassert(!s.codeActions.open);

    // A field with no provider leaves the chord alone.
    InputState plain;
    InputSetValue(&plain, StrL("hello"));
    InputSetSelectedRange(&plain, nullptr, nullptr, 0, 5);
    utassert(!InputPerform(&plain, nullptr, nullptr,
                           InputAction::ToggleCodeActions, false));
    utassert(!plain.codeActions.open);
}

// ─── document colours ─────────────────────────────────────────────────────

// A provider that answers one colour over the first four characters, and
// counts how often it was asked.
static int OneColor(void* data, Str text, DocumentColor* out, int cap) {
    if (data) {
        (*(int*)data)++;
    }
    if (len(text) < 4) {
        return 0;
    }
    if (cap > 0 && out) {
        out[0].range = Selection{0, 4};
        out[0].color = Rgba{1, 2, 3, 255};
    }
    return 1;
}

static void DocumentColorsAreAskedForAgainAfterAnEdit() {
    int asked = 0;
    InputState s;
    s.kind = InputKind::Editor;
    s.documentColorProvider = &OneColor;
    s.documentColorData = &asked;

    InputSetValue(&s, StrL("#f0a is a colour"));
    InputUpdateDocumentColors(&s);
    utassert(asked == 1);
    utassert(s.documentColors.len == 1);
    utassert(s.documentColors[0].range.start == 0);

    // Asking again with nothing changed answers off what is already there.
    InputUpdateDocumentColors(&s);
    utassert(asked == 1);

    // An edit makes them stale, and the next ask goes back to the provider.
    Type(&s, "x");
    InputUpdateDocumentColors(&s);
    utassert(asked == 2);

    // A field with no provider keeps an empty set and never asks.
    InputState plain;
    InputSetValue(&plain, StrL("#f0a"));
    InputUpdateDocumentColors(&plain);
    utassert(plain.documentColors.len == 0);
}

static int ManyDocumentColors(void* data, Str text, DocumentColor* out,
                              int cap) {
    (void)text;
    int total = *(int*)data;
    for (int i = 0; i < total && i < cap; i++) {
        int start = total - 1 - i;
        out[i].range = {start, start + 1};
        out[i].color = Rgba{1, 2, 3, 255};
    }
    return total;
}

static void DocumentColorResponsesUseTheRustLimit() {
    int total = 1500;
    InputState s;
    s.kind = InputKind::Editor;
    s.documentColorProvider = &ManyDocumentColors;
    s.documentColorData = &total;
    InputSetValue(&s, StrL("x"));
    InputUpdateDocumentColors(&s);
    utassert(s.documentColors.len == 1500);
    utassert(s.documentColors[0].range.start == 0);
    utassert(s.documentColors[1499].range.start == 1499);

    // Upstream rejects the whole response beyond 10,000. Keep the previous
    // valid cache rather than displaying a misleading prefix.
    total = kMaxDocumentColors + 1;
    s.documentColorsDirty = true;
    InputUpdateDocumentColors(&s);
    utassert(s.documentColors.len == 1500);
}

static El* FindNamedEl(El* root, const char* name) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && base::StrEqI(root->id, name)) {
        return root;
    }
    for (El* c = root->first; c; c = c->next) {
        if (El* hit = FindNamedEl(c, name)) {
            return hit;
        }
    }
    return nullptr;
}

// search.rs: `v_flex().id("search-panel")` over `Button::new("prev")`,
// `Button::new("next")`, `Button::new("close")` and the rest. The names only
// have to be unique among siblings because the bar above them is a stateful
// element, and two editors on one page are two bars. The port spelled the
// bar's id into every child instead.
static void ReopeningFindSelectsItsQueryWithoutChangingUntouchedFrames() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    InputState editor;
    editor.searchable = true;
    InputSetValue(&editor, StrL("foo bar foo"));
    InputSetSearchQuery(&editor, &app, win, StrL("foo"), false);
    InputOpenSearch(&editor, &app, win, false);
    component::SearchPanel::New(&cx, StrL("find"), &editor)->IntoEl();
    InputState* query = win->input;
    utassert(query && query != &editor);
    utassert(StrEq(InputSelectedValue(query), StrL("foo")));
    InputSetSelectedRange(query, &app, win, 1, 1);
    component::SearchPanel::New(&cx, StrL("find"), &editor)->IntoEl();
    utassert(len(InputSelectedValue(query)) == 0);
    uint64_t revision = InputSearchActivationRevision(&editor);
    InputFocus(&editor, &app, win);
    InputOpenSearch(&editor, &app, win, false);
    component::SearchPanel::New(&cx, StrL("find"), &editor)->IntoEl();
    utassert(InputSearchActivationRevision(&editor) == revision + 1);
    utassert(win->input == query);
    utassert(StrEq(InputSelectedValue(query), StrL("foo")));
    InputBlur(query, &app, win);
    win->input = nullptr;
    win->prevInput = nullptr;
    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

// overlay.rs: reopening_search_panel_preserves_the_previous_match. Closing
// and reopening Find keeps the previous occurrence in browsers. Neither Base
// reopening the session nor the styled panel echoing its retained query may
// reset the current match to zero.
static void ReopeningSearchPanelPreservesThePreviousMatch() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    InputState editor;
    editor.kind = InputKind::Editor;
    editor.searchable = true;
    InputSetValue(&editor, StrL("foo bar foo baz foo"));
    InputOpenSearch(&editor, &app, win, false);
    InputSetSearchQuery(&editor, &app, win, StrL("foo"), true);
    Selection range = {};
    utassert(InputSearchNext(&editor, &app, win, &range));
    utassert(range.start == 8 && range.end == 11);
    utassert(SearchMatcherIndex(&editor.search.matcher) == 1);
    InputCloseSearch(&editor, &app, win);
    InputOpenSearch(&editor, &app, win, false);
    utassert(SearchMatcherIndex(&editor.search.matcher) == 1);

    // The styled panel, built over the reopened session, echoes the query it
    // was given back to the editor.
    component::SearchPanel::New(&cx, StrL("find"), &editor)->IntoEl();
    InputState* query = win->input;
    utassert(query && query != &editor);
    InputSetSearchQuery(&editor, &app, win, InputValue(query),
                        editor.search.caseInsensitive);
    utassert(SearchMatcherIndex(&editor.search.matcher) == 1);
    {
        Str label = SearchMatcherLabel(arena, &editor.search.matcher);
        utassert(StrEq(label, StrL("2/3")));
    }
    utassert(InputSearchNext(&editor, &app, win, &range));
    utassert(range.start == 16 && range.end == 19);

    if (query) InputBlur(query, &app, win);
    win->input = nullptr;
    win->prevInput = nullptr;
    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

// test_set_search_query_highlights_without_the_panel
static void SetSearchQueryHighlightsWithoutThePanel() {
    App app;
    Window* win = new Window();
    win->app = &app;
    InputState editor;
    editor.kind = InputKind::Editor;
    editor.searchable = false;
    InputSetValue(&editor, StrL("foo bar foo"));
    InputSetSearchQuery(&editor, &app, win, StrL("foo"), true);
    utassert(SearchSessionIsActive(&editor.search));
    utassert(!editor.search.open);
    utassert(SearchMatcherLen(&editor.search.matcher) == 2);
    InputCloseSearch(&editor, &app, win);
    utassert(!SearchSessionIsActive(&editor.search));
    delete win;
}

// test_search_shortcut_reaches_the_host_when_not_searchable /
// test_search_shortcut_opens_the_panel_when_searchable
static void SearchShortcutPropagatesWhenTheEditorIsNotSearchable() {
    App app;
    Window* win = new Window();
    win->app = &app;
    InputState editor;
    editor.kind = InputKind::Editor;
    editor.searchable = false;
    utassert(Sec(&editor, KeyF, false) == InputAction::Search);
    utassert(!InputPerform(&editor, &app, win, InputAction::Search, false));
    utassert(!editor.search.open);
    utassert(!SearchSessionIsActive(&editor.search));

    editor.searchable = true;
    utassert(InputPerform(&editor, &app, win, InputAction::Search, false));
    utassert(editor.search.open);
    utassert(SearchSessionIsActive(&editor.search));
    delete win;
}

static void TwoFindBarsHaveTwoPrevButtons() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    InputState one;
    InputState two;
    one.searchable = true;
    two.searchable = true;
    InputOpenSearch(&one, &app, win, false);
    InputOpenSearch(&two, &app, win, false);

    El* page = Div(a);
    El* left = component::SearchPanel::New(&cx, StrL("left"), &one)->IntoEl();
    El* right = component::SearchPanel::New(&cx, StrL("right"), &two)->IntoEl();
    page->Child(left)->Child(right);
    IdsCollect(page);

    El* prevL = FindNamedEl(left, "prev");
    El* prevR = FindNamedEl(right, "prev");
    utassert(prevL && prevR);
    utassert(prevL->clickId != 0 && prevR->clickId != 0);
    utassert(prevL->clickId != prevR->clickId);
    // And the bar's own children are not each other.
    El* nextL = FindNamedEl(left, "next");
    utassert(nextL && nextL->clickId != prevL->clickId);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// crates/ui/src/input/state.rs, editor.rs and popovers/*.rs. The three text
// aliases share InputState in this runtime, but the tagged façade must retain
// their concrete identity and every source-named overlay must operate on the
// same sessions the editor renders.
static void TheUiInputFacadeKeepsTheSourceShapes() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};

    InputState state;
    InputSetValue(&state, StrL("alpha"));
    component::AnyInputState any = component::AnyInputState::From(&state);
    utassert(any.kind == component::AnyInputKind::Input);
    utassert(any.AsInput() == &state && !any.AsTextarea() && !any.AsEditor());
    utassert(StrEqI(any.Value(a, &app), "alpha"));

    state.masked = true;
    Str masked = any.Value(a, &app);
    utassert(len(masked) == 15); // five UTF-8 bullets
    state.masked = false;
    InputFocus(&state, &app, win);
    utassert(any.FocusHandleOf(win, &app).IsValid());
    utassert(FocusHandleIsFocused(win, any.FocusHandleOf(win, &app)));

    state.kind = InputKind::Textarea;
    any = component::AnyInputState::From(&state);
    utassert(any.AsTextarea() == &state && !any.AsInput());
    state.kind = InputKind::Editor;
    any = component::AnyInputState::From(&state);
    utassert(any.AsEditor() == &state && !any.AsTextarea());

    Entity<OtpState> otp = EntityNewState<OtpState>(&app);
    OtpState* otpState = otp.Get(&app);
    OtpSetValue(otpState, StrL("42"));
    component::AnyInputState anyOtp = component::AnyInputState::FromOtp(otp);
    utassert(anyOtp.AsOtp().id == otp.id && !anyOtp.AsEditor());
    utassert(StrEqI(anyOtp.Value(a, &app), "42"));
    otpState->masked = true;
    utassert(anyOtp.Value(a, &app).len == 6);
    utassert(anyOtp == component::AnyInputState::FromOtp(otp));

    gpui::Style refinement;
    refinement.width = 321;
    El* editor = component::Editor::New(&cx, StrL("source-editor"), &state)
                     ->H(180)
                     ->Readonly()
                     ->Disabled(true)
                     ->TabIndex(3)
                     ->AriaLabel(StrL("Source"))
                     ->Language(StrL("rust"))
                     ->ActiveLine()
                     ->IndentGuides()
                     ->Folding()
                     ->Refine(refinement, StyleFieldWidth)
                     ->IntoEl();
    utassert(editor);
    utassert(state.kind == InputKind::Editor);
    utassert(state.mode.kind == LayoutModeKind::CodeEditor);
    utassert(state.mode.folding);
    utassert(state.readonly && state.disabled);
    utassert(editor->style.tabIndex == 3);
    utassert(editor->accessibility
                 .role == AccessibilityRole::MultilineTextInput);
    utassert((editor->StyleStates()->refineSet & StyleFieldWidth) != 0);
    utassert(state.focus.IsValid());
    // editor.rs: `.font_family(cx.theme().mono_font_family)` under the
    // caller's own, and the rows are measured in the family they draw in.
    uint16_t word = state.lastFontWord;
    utassert((word & kFontMono) != 0);
    utassert(FontFamilyOf(word) ==
             FontFamilyIntern(ThemeNow(&app).monoFontFamily));
    component::Editor::New(&cx, StrL("source-editor"), &state)
        ->FontFamily(StrL("Monaco"))
        ->IntoEl();
    utassert(FontFamilyOf(state.lastFontWord) ==
             FontFamilyIntern(StrL("Monaco")));
    utassert((state.lastFontWord & kFontMono) != 0);

    state.disabled = false;
    state.readonly = false;
    InputSetValue(&state, StrL("a"));
    CompletionItem completions[2] = {};
    completions[0].label = StrL("alpha");
    completions[1].label = StrL("atom");
    component::CompletionMenu* completion =
        component::CompletionMenu::New(&cx, &state)
            ->UpdateQuery(0, StrL("a"))
            ->Show(1, completions, 2);
    utassert(state.completion.open && state.completion.items.len == 2);
    utassert(StrEqI(state.completion.query, "a"));
    El* completionEl = completion->IntoEl();
    utassert(completionEl && completionEl->onMouseDownOut.IsValid());
    utassert(completion->HandleAction(InputAction::MoveDown));
    utassert(state.completion.selected == 1);
    completion->Hide();
    utassert(!state.completion.open);

    CodeActionItem actions[2] = {};
    actions[0].title = StrL("First");
    actions[1].title = StrL("Second");
    component::CodeActionMenu* codeActions =
        component::CodeActionMenu::New(&cx, &state)->Show(1, actions, 2);
    El* codeActionEl = codeActions->IntoEl();
    utassert(codeActionEl && codeActionEl->onMouseDownOut.IsValid());
    utassert(codeActions->HandleAction(InputAction::MoveDown));
    utassert(state.codeActions.selected == 1);
    codeActions->Hide();
    utassert(!state.codeActions.open);

    Diagnostic diagnostic;
    diagnostic.range = {0, 1};
    diagnostic.severity = DiagnosticSeverity::Warning;
    diagnostic.message = StrL("**warning**");
    VecAppend(state.diagnostics, diagnostic);
    state.hoverDiagnosticX = 20;
    state.hoverDiagnosticY = 30;
    state.popoverTriggerBounds = {10, 20, 30, 16};
    El* diagnosticEl = component::DiagnosticPopover::New(&cx, &state, 0)
                           ->IntoEl();
    utassert(diagnosticEl && diagnosticEl->style.explicitPositioner);
    state.hoverX = 40;
    state.hoverY = 50;
    El* hoverEl =
        component::HoverPopover::New(&cx, &state, {0, 1}, StrL("`hover`"))
            ->IntoEl();
    utassert(hoverEl && hoverEl->style.explicitPositioner);

    InputBlur(&state, &app, win);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

static int DummyDefinitions(void*, Arena*, Str, int, DefinitionLink*, int) {
    return 0;
}

static void BaseInputCoreKeepsTheSourceModeAndPresentationSeams() {
    utassert(MultiLineMode::Includes(InputKind::Textarea));
    utassert(MultiLineMode::Includes(InputKind::Editor));
    utassert(!MultiLineMode::Includes(InputKind::Input));

    InputState state;
    state.kind = InputKind::Editor;
    state.readonly = true;
    state.masked = true;
    state.selectedRange = {0, 2};
    state.definitionProvider = DummyDefinitions;
    InputSetPlaceholder(&state, StrL("value"));
    InputModeKind mode = InputModeKind::Of(&state);
    utassert(mode.IsMultiLine() && mode.IsCodeEditor());
    EditorExtras extras = EditorExtras::Of(&state);
    utassert(extras.state == &state && extras.HasDefinition());

    InputContextMenuCapabilities capabilities =
        InputContextMenuCapabilities::Of(&state);
    utassert(capabilities.IsReadonly());
    utassert(!capabilities.IsEditable());
    utassert(capabilities.HasSelection());
    utassert(capabilities.IsMasked());
    utassert(!capabilities.IsCopyable());
    utassert(capabilities.HasDefinition());

    Arena* arena = ArenaNew();
    InputPresentation presentation = InputPresentation::Of(arena, &state);
    utassert(presentation.readonly && presentation.multiLine);
    utassert(presentation.codeEditor && presentation.masked);
    utassert(base::StrEq(presentation.placeholder, StrL("value")));

    Style normal;
    normal.color = Rgb(1, 2, 3);
    Style focus;
    focus.color = Rgb(4, 5, 6);
    Style disabled;
    disabled.opacity = 0.5f;
    InputStyles styles;
    styles.Focused(focus, StyleFieldColor)
        .Disabled(disabled, StyleFieldOpacity);
    styles.Apply(&normal, true, true);
    utassert(
        normal.color.r == focus.color.r && normal.color.g == focus.color.g &&
        normal.color.b == focus.color.b && normal.color.a == focus.color.a);
    utassertnear(normal.opacity, 0.5f);

    // input.rs's built-in menu. A read-only code editor can still go to a
    // definition; the items that change the text, and the copy items of a
    // masked value, are disabled; Select All always is not.
    NativeMenu menu;
    InputDefaultNativeMenu(&state, &menu);
    utassert(menu.items.len == 8);
    utassert(menu.items[0].goToDefinition && !menu.items[0].disabled);
    utassert(menu.items[1].action == InputAction::ToggleCodeActions &&
             menu.items[1].disabled);
    utassert(menu.items[2].kind == NativeMenuItemKind::Separator);
    utassert(menu.items[3].action == InputAction::Cut && menu.items[3]
                                                             .disabled);
    utassert(menu.items[4].action == InputAction::Copy && menu.items[4]
                                                              .disabled);
    utassert(menu.items[5].action == InputAction::Paste && menu.items[5]
                                                               .disabled);
    utassert(menu.items[7].action == InputAction::SelectAll && !menu.items[7]
                                                                    .disabled);
    // A plain editable input with a selection: no code rows, and Cut, Copy
    // and Paste are live.
    InputState plain;
    InputSetValue(&plain, StrL("hello"));
    plain.selectedRange = {0, 2};
    NativeMenu plainMenu;
    InputDefaultNativeMenu(&plain, &plainMenu);
    utassert(plainMenu.items.len == 5);
    utassert(!plainMenu.items[0].disabled && !plainMenu.items[1].disabled &&
             !plainMenu.items[2].disabled);
    ArenaDelete(arena);
}

static void DecorationsAreIndependentClippedAndTrackEdits() {
    InputState state;
    state.kind = InputKind::Editor;
    InputSetValue(&state, StrL("héllo"));
    DecorationCollections collections(&state);

    TextSpan firstStyle;
    firstStyle.color = Rgb(1, 2, 3);
    TextSpan secondStyle;
    secondStyle.bg = Rgb(4, 5, 6);
    TextDecoration firstValue = TextDecoration::New({2, 4}, firstStyle);
    TextDecoration secondValue = TextDecoration::New({5, 100}, secondStyle);
    TextDecorationCollection first = collections.Create(&firstValue, 1);
    TextDecorationCollection second = collections.Create(&secondValue, 1);

    Selection ranges[2] = {};
    utassert(first.GetRanges(ranges, 2) == 1);
    utassert(ranges[0].start == 1 && ranges[0].end == 4);
    utassert(second.GetRanges(ranges, 2) == 1);
    utassert(ranges[0].start == 5 && ranges[0].end == 6);

    TextDecoration overlap = TextDecoration::New({3, 6}, secondStyle);
    utassert(second.Append(&overlap, 1));
    TextSpan spans[4] = {};
    int n = collections.BuildSpans(spans, 4);
    utassert(n == 3);
    utassert(spans[0].lo == 1 && spans[0].hi == 4);
    utassert(spans[1].lo == 4 && spans[1].hi == 5);
    utassert(spans[2].lo == 5 && spans[2].hi == 6);

    collections.AdjustForEdit({0, 0}, 2);
    utassert(first.GetRanges(ranges, 2) == 1);
    utassert(ranges[0].start == 3 && ranges[0].end == 6);
    collections.AdjustForEdit({3, 6}, 1);
    utassert(first.GetRanges(ranges, 2) == 1);
    utassert(ranges[0].start == 3 && ranges[0].end == 4);
}

// decorations.rs
// geometric_collections_share_utf8_normalization_and_edit_affinity
static void GeometricCollectionsShareUtf8NormalizationAndEditAffinity() {
    RangeDecorationCollections collections;
    Str text = StrL("h\xC3\xA9llo world");
    RangeDecoration firstIn[3] = {RangeDecoration::New({2, 4}),
                                  RangeDecoration::New({2, 1}),
                                  RangeDecoration::New({100, 200})};
    Vec<RangeDecoration> normalized;
    RangeDecorationsNormalize(text, firstIn, 3, &normalized);
    uint64_t first = collections.Create(normalized.els, len(normalized));
    RangeDecoration secondIn = RangeDecoration::New({7, 12});
    VecClear(normalized);
    RangeDecorationsNormalize(text, &secondIn, 1, &normalized);
    uint64_t second = collections.Create(normalized.els, len(normalized));
    auto firstRange = [&]() { return collections.Get(first)->decorations[0]; };
    utassert(len(collections.Get(first)->decorations) == 1);
    utassert(firstRange().range.start == 1 && firstRange().range.end == 4);
    collections.AdjustForEdit({1, 1}, 2);
    utassert(firstRange().range.start == 3 && firstRange().range.end == 6);
    collections.AdjustForEdit({6, 6}, 1);
    utassert(firstRange().range.start == 3 && firstRange().range.end == 6);
    collections.AdjustForEdit({4, 4}, 2);
    utassert(firstRange().range.start == 3 && firstRange().range.end == 8);
    collections.AdjustForEdit({3, 8}, 0);
    utassert(len(collections.Get(first)->decorations) == 0);
    utassert(len(collections.Get(second)->decorations) > 0);
    utassert(collections.Remove(first));
    uint64_t third = collections.Create(nullptr, 0);
    utassert(third != first);
    RangeDecoration one = RangeDecoration::New({0, 1});
    utassert(!collections.Set(first, &one, 1));
    utassert(collections.Get(second) != nullptr);
}

static bool IntersectingIs(const RangeDecorationCollections& collections,
                           const Selection* query, int nQuery,
                           const Selection* want, int nWant) {
    const RangeDecoration* got[16] = {};
    int n = collections.Intersecting(query, nQuery, got, 16);
    if (n != nWant) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (got[i]->range.start != want[i].start ||
            got[i]->range.end != want[i].end) {
            return false;
        }
    }
    return true;
}

// decorations.rs visible_query_preserves_layers_and_skips_folded_spans
static void VisibleQueryPreservesLayersAndSkipsFoldedSpans() {
    RangeDecorationCollections collections;
    RangeDecoration firstIn[4] = {
        RangeDecoration::New({90, 100}), RangeDecoration::New({0, 100}),
        RangeDecoration::New({40, 50}), // hidden in a fold
        RangeDecoration::New({0, 5})};
    uint64_t first = collections.Create(firstIn, 4);
    RangeDecoration secondIn = RangeDecoration::New({2, 4});
    collections.Create(&secondIn, 1);
    Selection query[2] = {{0, 5}, {90, 100}};
    Selection want1[4] = {{90, 100}, {0, 100}, {0, 5}, {2, 4}};
    utassert(IntersectingIs(collections, query, 2, want1, 4));
    collections.AdjustForEdit({0, 0}, 1);
    Selection want2[4] = {{91, 101}, {1, 101}, {1, 6}, {3, 5}};
    utassert(IntersectingIs(collections, query, 2, want2, 4));
    RangeDecoration replaced = RangeDecoration::New({50, 60});
    collections.Set(first, &replaced, 1);
    Selection want3[1] = {{3, 5}};
    utassert(IntersectingIs(collections, query, 2, want3, 1));
}

// decorations.rs
// interval_index_culls_large_collections_even_with_a_spanning_range
static void IntervalIndexCullsLargeCollectionsEvenWithASpanningRange() {
    Vec<RangeDecoration> decorations;
    for (int ix = 0; ix < 100000; ix++) {
        VecAppend(decorations, RangeDecoration::New({ix * 10, ix * 10 + 5}));
    }
    VecAppend(decorations, RangeDecoration::New({0, 1000000}));
    DecorationIndex index;
    index.Rebuild(decorations.els, len(decorations));
    Selection queries[4] = {
        {0, 1}, {500000, 500020}, {999990, 1000001}, {1000000, 1000010}};
    for (Selection query : queries) {
        Vec<int> matches;
        int visited =
            index.Query(decorations.els, 0, len(decorations), query, &matches);
        std::sort(matches.els, matches.els + len(matches));
        Vec<int> expected;
        for (int ix = 0; ix < len(decorations); ix++) {
            Selection r = decorations[ix].range;
            if (r.start < query.end && r.end > query.start) {
                VecAppend(expected, ix);
            }
        }
        bool same = len(matches) == len(expected);
        for (int i = 0; same && i < len(matches); i++) {
            same = matches[i] == expected[i];
        }
        utassert(same);
        utassert(visited < 100);
    }
}

// decorations.rs
// interval_index_matches_linear_reference_for_overlaps_and_mutations
static void IntervalIndexMatchesLinearReferenceForOverlapsAndMutations() {
    RangeDecorationCollections collections;
    Vec<RangeDecoration> initial;
    for (int ix = 0; ix < 512; ix++) {
        int start = (ix * 37) % 997;
        VecAppend(initial, RangeDecoration::New({start, start + ix % 61 + 1}));
    }
    uint64_t id = collections.Create(initial.els, len(initial));
    Selection edits[3] = {{0, 0}, {300, 450}, {900, 1100}};
    bool allSame = true;
    for (Selection edit : edits) {
        collections.AdjustForEdit(edit, 3);
        for (int start = 0; start < 1100; start += 13) {
            Selection query = {start, start + 17};
            const Vec<RangeDecoration>& all = collections.Get(id)->decorations;
            Vec<Selection> expected;
            for (int i = 0; i < len(all); i++) {
                if (all[i].range.start < query.end &&
                    all[i].range.end > query.start) {
                    VecAppend(expected, all[i].range);
                }
            }
            int n = collections.Intersecting(&query, 1, nullptr, 0);
            Vec<const RangeDecoration*> got;
            VecResize(got, n);
            collections.Intersecting(&query, 1, got.els, n);
            bool same = n == len(expected);
            for (int i = 0; same && i < n; i++) {
                same = got[i]->range.start == expected[i].start &&
                       got[i]->range.end == expected[i].end;
            }
            allSame = allSame && same;
        }
    }
    utassert(allSame);
}

static RangeCorners CornersAt(float l, float t, float r, float b) {
    RangeCorners c;
    c.topLeft = {l, t};
    c.topRight = {r, t};
    c.bottomLeft = {l, b};
    c.bottomRight = {r, b};
    return c;
}

static bool PointsAre(const Point* got, int n, const Point* want, int nWant) {
    if (n != nWant) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (fabsf(got[i].x - want[i].x) > 1e-4f ||
            fabsf(got[i].y - want[i].y) > 1e-4f) {
            return false;
        }
    }
    return true;
}

// element.rs frame_outline_is_continuous_across_different_line_widths
static void FrameOutlineIsContinuousAcrossDifferentLineWidths() {
    RangeCorners corners[2] = {CornersAt(2, 0, 20, 10),
                               CornersAt(0, 10, 12, 20)};
    Vec<Point> points;
    FrameOutlinePoints(corners, 2, &points);
    Point want[9] = {{2, 0},  {20, 0}, {20, 10}, {12, 10}, {12, 20},
                     {0, 20}, {0, 10}, {2, 10},  {2, 0}};
    utassert(PointsAre(points.els, len(points), want, 9));
}

// element.rs frame_outline_keeps_horizontal_space_between_the_stroke_and_text
static void FrameOutlineKeepsHorizontalSpaceBetweenTheStrokeAndText() {
    RangeCorners corners[1] = {CornersAt(2, 0, 20, 10)};
    RangeCorners padded = corners[0];
    PadFrameCorners(&padded, 1, 1.f);
    utassertnear(padded.topLeft.x, 1.f);
    utassertnear(padded.topRight.x, 21.f);
    utassertnear(padded.bottomLeft.x, 1.f);
    utassertnear(padded.bottomRight.x, 21.f);
    utassertnear(padded.topLeft.y, 0.f);
    utassertnear(padded.bottomRight.y, 10.f);
    Vec<Point> points;
    FrameOutlinePoints(corners, 1, &points);
    utassert(len(points) > 0 && points[0] == points[len(points) - 1]);
}

// element.rs
// frame_outline_stroke_is_aligned_to_physical_pixels_at_each_scale_factor
static void FrameOutlineStrokeIsAlignedToPhysicalPixelsAtEachScaleFactor() {
    struct Case {
        float scale;
        float width;
        Point want[2];
    };
    Case cases[3] = {
        {1.f, 1.f, {{0.5f, 1.5f}, {10.5f, 20.5f}}},
        {1.5f, 2.f / 3.f, {{1.f / 3.f, 5.f / 3.f}, {11.f, 61.f / 3.f}}},
        {2.f, 1.f, {{0.f, 2.f}, {10.5f, 20.5f}}},
    };
    for (const Case& c : cases) {
        Point points[2] = {{0.2f, 1.8f}, {10.7f, 20.3f}};
        float width = SnapFrameOutline(points, 2, 1.f, c.scale);
        utassertnear(width, c.width);
        utassert(PointsAre(points, 2, c.want, 2));
    }
}

// element.rs frame_outline_keeps_its_vertical_edges_inside_the_content_mask
static void FrameOutlineKeepsItsVerticalEdgesInsideTheContentMask() {
    Point points[5] = {{9.5f, 2.5f},
                       {20.5f, 2.5f},
                       {20.5f, 10.5f},
                       {9.5f, 10.5f},
                       {9.5f, 2.5f}};
    ClampFrameToContentMask(points, 5, 1.f, Bounds{10, 0, 10, 20});
    Point want[5] = {{10.5f, 2.5f},
                     {19.5f, 2.5f},
                     {19.5f, 10.5f},
                     {10.5f, 10.5f},
                     {10.5f, 2.5f}};
    utassert(PointsAre(points, 5, want, 5));
}

static bool CollectionRangesAre(const RangeDecorationCollection& c,
                                const Selection* want, int nWant) {
    Selection got[8] = {};
    int n = c.GetRanges(got, 8);
    if (n != nWant) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (got[i].start != want[i].start || got[i].end != want[i].end) {
            return false;
        }
    }
    return true;
}

// element.rs
// geometric_decorations_track_edits_history_replacement_and_owner_lifetime. The
// element.rs tests that lay the editor out and measure the corners (viewport
// clipping, shaped wrap boundaries and newline cells, CRLF, folds) need a
// window this suite does not draw; the story's Decorations tab shows them, and
// the paint path is RangeDecorationCorners in src/base/input.cpp.
static void GeometricDecorationsTrackEditsHistoryReplacementAndOwnerLifetime() {
    InputState* s = new InputState();
    s->kind = InputKind::Editor;
    InputSetValue(s, StrL("abc def"));
    RangeDecoration firstIn = RangeDecoration::New({4, 7});
    RangeDecoration secondIn = RangeDecoration::New({0, 3});
    RangeDecorationCollection first =
        InputCreateRangeDecorationsCollection(s, &firstIn, 1);
    RangeDecorationCollection second =
        InputCreateRangeDecorationsCollection(s, &secondIn, 1);

    InputSetSelectedRange(s, nullptr, nullptr, 0, 0);
    Type(s, "\n");
    Selection r58[1] = {{5, 8}};
    Selection r47[1] = {{4, 7}};
    utassert(CollectionRangesAre(first, r58, 1));
    Act(s, InputAction::Undo);
    utassert(CollectionRangesAre(first, r47, 1));
    Act(s, InputAction::Redo);
    utassert(CollectionRangesAre(first, r58, 1));
    first.Clear();
    Selection r14[1] = {{1, 4}};
    utassert(CollectionRangesAre(second, r14, 1));
    RangeDecoration again = RangeDecoration::New({5, 8});
    first.Append(&again, 1);
    InputReplaceAll(s, nullptr, nullptr, StrL("formatted"));
    Selection r09[1] = {{0, 9}};
    utassert(CollectionRangesAre(first, r09, 1));
    Act(s, InputAction::Undo);
    // Annotations are transformed, not snapshotted in undo history.
    Selection r08[1] = {{0, 8}};
    utassert(CollectionRangesAre(first, r08, 1));
    InputSetValue(s, StrL("new"));
    Selection r03[1] = {{0, 3}};
    utassert(CollectionRangesAre(first, r03, 1));
    RangeDecorationCollection clone = first;
    first.Dispose();
    RangeDecoration one = RangeDecoration::New({0, 1});
    clone.Append(&one, 1);
    utassert(clone.GetRanges(nullptr, 0) == 0);
    utassert(CollectionRangesAre(second, r03, 1));
    InputSetValue(s, StrL(""));
    utassert(second.GetRanges(nullptr, 0) == 0);

    // The editor dropped: every handle is a harmless no-op from then on.
    delete s;
    second.Append(&one, 1);
    utassert(!second.IsValid() && second.GetRanges(nullptr, 0) == 0);
}

// element.rs line_number_column_stays_at_three_digits_then_grows_up_to_seven
// and displayed_line_number_stays_within_seven_digits.
// editor_line_number_gutter_resizes_with_document_lines draws a window; the
// width it compares is 7px per InputLineNumberLen column here.
static void LineNumberColumnStaysAtThreeDigitsThenGrowsUpToSeven() {
    utassert(InputLineNumberLen(1) == 3);
    utassert(InputLineNumberLen(9) == 3);
    utassert(InputLineNumberLen(10) == 3);
    utassert(InputLineNumberLen(999) == 3);
    utassert(InputLineNumberLen(1000) == 4);
    utassert(InputLineNumberLen(999999) == 6);
    utassert(InputLineNumberLen(9999999) == 7);
    utassert(InputLineNumberLen(10000000) == 7);
    utassert(InputDisplayedLineNumber(42) == 42);
    utassert(InputDisplayedLineNumber(9999999) == 9999999);
    utassert(InputDisplayedLineNumber(10000000) == 9999999);
}

static void DiagnosticSetOwnsMetadataAndAnswersRanges() {
    DiagnosticSet set(
        StrL("Hello, 你好warld!\nThis is a test.\nGoodbye, world!"));
    DiagnosticRelatedInformation related = {
        StrL("file:///other.cpp"), {1, 2}, StrL("first declared here")};
    DiagnosticTag tag = DiagnosticTag::Deprecated;
    Diagnostic spelling;
    spelling.range = {7, 19};
    spelling.severity = DiagnosticSeverity::Warning;
    spelling.message = StrL("Spelling mistake");
    spelling.source = StrL("spell");
    spelling.relatedInformation = &related;
    spelling.nRelatedInformation = 1;
    spelling.tags = &tag;
    spelling.nTags = 1;
    set.Push(spelling);

    Diagnostic syntax;
    syntax.range = {45, 50};
    syntax.severity = DiagnosticSeverity::Error;
    syntax.message = StrL("Syntax error");
    set.Push(syntax);
    utassert(set.Len() == 2);
    utassert(set.Summary().start == 7 && set.Summary().end == 50);

    const DiagnosticEntry* found = set.ForOffset(10);
    utassert(found &&
             base::StrEq(found->diagnostic.message, StrL("Spelling mistake")));
    utassert(found->diagnostic.nRelatedInformation == 1);
    utassert(base::StrEq(found->diagnostic.relatedInformation[0].message,
                         StrL("first declared here")));
    utassert(found->diagnostic.tags[0] == DiagnosticTag::Deprecated);
    utassert(set.ForOffset(30) == nullptr);

    const DiagnosticEntry* entries[2] = {};
    utassert(set.Range({6, 48}, entries, 2) == 2);
    set.Clear();
    utassert(set.IsEmpty());
}

static bool ResolveKeyword(void*, Str name, TextSpan* out) {
    if (!base::StrEq(name, StrL("keyword"))) {
        return false;
    }
    out->color = Rgb(10, 20, 30);
    return true;
}

static Str HighlighterLanguage(void*) {
    return StrL("cpp");
}

static int HighlighterStyles(void*, Selection range,
                             const HighlightStyleResolver* resolver, Arena* a,
                             TextSpan** out) {
    TextSpan style;
    if (!resolver || !resolver->Style(StrL("keyword"), &style)) {
        return 0;
    }
    style.lo = range.start;
    style.hi = range.end;
    auto* spans = (TextSpan*)a->Alloc((int)sizeof(TextSpan));
    spans[0] = style;
    *out = spans;
    return 1;
}

static void HighlighterContractsAreDependencyFreeAndFunctional() {
    HighlightStyleResolver resolver;
    resolver.style = ResolveKeyword;
    InputHighlighter highlighter;
    highlighter.language = HighlighterLanguage;
    highlighter.styles = HighlighterStyles;
    utassert(base::StrEq(highlighter.Language(), StrL("cpp")));
    Arena* a = ArenaNew();
    TextSpan* spans = nullptr;
    utassert(highlighter.Styles({2, 5}, &resolver, a, &spans) == 1);
    utassert(spans[0].lo == 2 && spans[0].hi == 5);
    utassert(spans[0].color.r == Rgb(10, 20, 30).r);
    ArenaDelete(a);
}

static int LspFacadeCompletions(void*, Str, int, Str, CompletionItem* out,
                                int cap) {
    if (out && cap > 0) {
        out[0].label = StrL("value");
    }
    return 1;
}

static CompletionTrigger LspFacadeTrigger(void*, Str, int, Str) {
    return CompletionTrigger::Continue;
}

static int LspFacadeActions(void*, Arena*, Str, Selection, CodeActionItem* out,
                            int cap) {
    if (out && cap > 0) {
        out[0].title = StrL("Fix");
    }
    return 1;
}

static Str LspFacadeId(void*) {
    return StrL("test");
}

static int LspFacadeSemantic(void*, Str, Selection, SemanticToken*, int) {
    return 0;
}

static void LspFacadesInstallCapabilitiesAndExposeOverlayState() {
    InputState state;
    state.kind = InputKind::Editor;
    InputSetValue(&state, StrL("value"));
    int marker = 42;

    CompletionProvider completion;
    completion.data = &marker;
    completion.completions = LspFacadeCompletions;
    completion.isCompletionTrigger = LspFacadeTrigger;
    completion.inlineCompletionDebounceMs = 125.f;
    CodeActionProvider action;
    action.data = &marker;
    action.id = LspFacadeId;
    action.codeActions = LspFacadeActions;
    DefinitionProvider definition;
    definition.data = &marker;
    definition.definitions = DummyDefinitions;
    Str legend[] = {StrL("keyword")};
    DocumentRangeSemanticTokensProvider semantic;
    semantic.data = &marker;
    semantic.legend = legend;
    semantic.nLegend = 1;
    semantic.semanticTokens = LspFacadeSemantic;

    CompletionMenuOptions options;
    options.maxWidth = 480.f;
    Lsp lsp;
    lsp.Completion(completion)
        .AddCodeAction(action)
        .Definition(definition)
        .SemanticTokens(semantic)
        .CompletionMenu(options);
    lsp.Install(&state);
    utassert(state.completionProvider == LspFacadeCompletions);
    utassert(state.completionData == &marker);
    utassert(state.completionTrigger == LspFacadeTrigger);
    utassertnear(state.inlineCompletionDebounceMs, 125.f);
    utassertnear(state.completionMenuMaxW, 480.f);
    utassert(state.codeActionProviders.len == 1);
    utassert(state.definitionProvider == DummyDefinitions);
    utassert(state.semanticTokensProvider == LspFacadeSemantic);
    utassert(state.semanticLegend == legend && state.nSemanticLegend == 1);

    CompletionItem item;
    item.label = StrL("value");
    InputPresentCompletionItems(&state, 1, StrL("val"), &item, 1);
    CompletionMenuState completionState = CompletionMenuState::Of(&state);
    utassert(completionState.open && completionState.nItems == 1);
    utassert(completionState.triggerStartOffset == 1);
    utassert(base::StrEq(completionState.query, StrL("val")));

    CodeActionItem actionItem;
    actionItem.title = StrL("Fix");
    InputPresentCodeActions(&state, &actionItem, 1);
    CodeActionMenuState actionState = CodeActionMenuState::Of(&state);
    utassert(actionState.open && actionState.nItems == 1);
    utassert(actionState.revision > 0);

    InputPresentHover(&state, {0, 5}, StrL("documentation"));
    HoverPopoverState hoverState = HoverPopoverState::Of(&state);
    utassert(hoverState.open && hoverState.symbolRange.end == 5);
    utassert(base::StrEq(hoverState.hover, StrL("documentation")));

    VecAppend(state.documentColors, {{0, 2}, Rgb(1, 2, 3)});
    DocumentColor colors[1] = {};
    utassert(lsp.DocumentColorsForRange({0, 1}, colors, 1) == 1);
    utassert(colors[0].range.start == 0 && colors[0].range.end == 2);

    VecAppend(state.semanticTokens, {0, 0, 2, StrL("keyword")});
    TextSpan spans[1] = {};
    HighlightStyleResolver resolver;
    resolver.style = ResolveKeyword;
    utassert(lsp.SemanticTokensForRange({0, 2}, resolver, spans, 1) == 1);
    utassert(spans[0].lo == 0 && spans[0].hi == 2);

    lsp.Reset();
    utassert(state.documentColors.len == 0);
    utassert(state.semanticTokens.len == 0);
    utassert(!CompletionMenuState::Of(&state).open);
    utassert(!CodeActionMenuState::Of(&state).open);
    utassert(!HoverPopoverState::Of(&state).open);
}

static void SoftWrapBoundariesKeepTheVisualRowAffinity() {
    InputState state;
    InputSetValue(&state, StrL("alpha beta gamma delta epsilon"));
    state.kind = InputKind::Editor;
    state.softWrap = true;
    InputMoveToWithAffinity(&state, nullptr, nullptr, 5, true);
    utassert(state.cursorLineEndAffinity);
    InputSelectTo(&state, nullptr, nullptr, 6);
    utassert(!state.cursorLineEndAffinity);

    PaintApp* paint = PaintAppNew();
    utassert(paint);
    if (!paint) {
        return;
    }
    PaintCtx ctx;
    ctx.pa = paint;
    Str line = InputValue(&state);
    const float font = 16.f;
    const float width = 72.f;
    const float lineMult = 1.5f;
    int boundary = -1;
    float endY = 0, endH = 0, nextY = 0, nextH = 0;
    for (int i = 1; i < len(line); i++) {
        float endX = 0, nextX = 0;
        if (TextPointAt(&ctx, line, font, width, true, i, &endX, &endY, &endH,
                        false, lineMult, true) &&
            TextPointAt(&ctx, line, font, width, true, i, &nextX, &nextY,
                        &nextH, false, lineMult, false) &&
            endY + 0.5f < nextY) {
            boundary = i;
            break;
        }
    }
    utassert(boundary > 0);
    if (boundary > 0) {
        // The same byte offset closes one row and opens the next. The
        // affinity decides which caret position is intended.
        utassert(endY < nextY);
        state.lastBounds = {0, 0, width, nextY + nextH + 20};
        state.inputBounds = state.lastBounds;
        state.lastFont = font;
        state.lastLineH = font * lineMult;
        VecAppend(state.rowBoxes, state.lastBounds);

        bool affinity = false;
        int at = InputIndexForPosition(&state, &ctx, width + 100,
                                       endY + endH * 0.5f, &affinity);
        utassert(at == boundary && affinity);
        at = InputIndexForPosition(&state, &ctx, 0, nextY + nextH * 0.5f,
                                   &affinity);
        utassert(at == boundary && !affinity);
    }
    TextMeasClear(&ctx);
    PaintAppFree(paint);
}

static int CountElTree(El* e) {
    int n = 0;
    for (; e; e = e->next) {
        n++;
        n += CountElTree(e->first);
    }
    return n;
}

// A long document with no viewport yet, or with viewH set to the content
// column's height, must not build every line. That was the editor's one-frame
// spike to thousands of taffy nodes on file open.
static void ALongDocumentBuildsOnlyTheVisibleBand() {
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.viewH = 756;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};

    const int kLines = 2000;
    char* buf = (char*)Alloc(nullptr, kLines * 2);
    utassert(buf);
    for (int i = 0; i < kLines; i++) {
        buf[i * 2] = 'x';
        buf[i * 2 + 1] = '\n';
    }
    InputState state;
    state.kind = InputKind::Editor;
    InputSetValue(&state, Str(buf, kLines * 2));
    Free(nullptr, buf);

    state.viewH = 0;
    El* none = gpui::Editor::New(&cx, &state);
    utassert(none);
    int nNone = CountElTree(none);
    utassert(nNone > 0 && nNone < 800);

    a->Reset();
    state.viewH = (float)kLines * 20.f;
    El* full = gpui::Editor::New(&cx, &state);
    utassert(full);
    int nFull = CountElTree(full);
    utassert(nFull > 0 && nFull < 800);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// The first element under `e` whose first child is absolute and painted in
// `bg`: the caret row's band, by the wash it carries.
static El* FindWashedBand(El* e, Rgba bg) {
    for (; e; e = e->next) {
        El* f = e->first;
        if (f && f->style.absolute && f->style.hasBg &&
            f->style.bg.color.r == bg.r && f->style.bg.color.g == bg.g &&
            f->style.bg.color.b == bg.b && f->style.bg.color.a == bg.a) {
            return e;
        }
        if (El* found = FindWashedBand(e->first, bg)) {
            return found;
        }
    }
    return nullptr;
}

// element.rs paint: the active-line quad starts left of the gutter, so it
// covers the editor's left padding (set_editor_paddings' 6px) as well as the
// line numbers and the text.
static void TheActiveLineWashCoversTheLeftPadding() {
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.viewH = 400;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    InputState state;
    state.kind = InputKind::Editor;
    InputSetValue(&state, StrL("one\ntwo\nthree"));
    InputEditorStyle style;
    style.activeLine = Rgba8(10, 20, 30, 255);
    style.activeLineBleedL = 6;
    El* editor = gpui::Editor::New(&cx, &state, style);
    El* band = FindWashedBand(editor, style.activeLine);
    utassert(band != nullptr);
    if (band) {
        // Behind the row's own cells, from 6px left of the band to its right
        // edge, top to bottom.
        utassertnear(band->first->style.absLeft, -6.f);
        utassertnear(band->first->style.absRight, 0.f);
        utassert(!band->style.hasBg);
    }
    // Without a padding to cover, the band takes the wash itself.
    style.activeLineBleedL = 0;
    El* plain = gpui::Editor::New(&cx, &state, style);
    utassert(FindWashedBand(plain, style.activeLine) == nullptr);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// A click in a scrolled editor must use the clip box plus live scrollY.
// lastBounds is row 0's text (only painted at the top of the file);
// contentBox.y is the column's last painted origin, so it still has the
// scrollY of that frame. Scrolling from line 200 to 400 then clicking
// would otherwise map as if the top were still 200.
static void AClickInAScrolledEditorMapsThroughScrollY() {
    const int kLines = 400;
    char* buf = (char*)Alloc(nullptr, kLines * 2);
    utassert(buf);
    for (int i = 0; i < kLines; i++) {
        buf[i * 2] = 'x';
        buf[i * 2 + 1] = '\n';
    }
    InputState state;
    state.kind = InputKind::Editor;
    InputSetValue(&state, Str(buf, kLines * 2));
    Free(nullptr, buf);
    state.lastLineH = 20;
    state.lastFont = 14;
    state.lastBounds = {12, 80, 200, 20};
    state.inputBounds = {0, 80, 400, 400};
    // Stale column origin from when the viewport top was line 200. A click
    // after scrolling to line 400 must not use it.
    state.contentBox = {0, 80.f - 200.f * 20.f, 400, (float)kLines * 20.f};
    state.scrollY = 400.f * 20.f;
    PaintCtx ctx = {};
    int at = InputIndexForPosition(&state, &ctx, 12, 80.f + 100.f, nullptr);
    utassert(at == InputLineStartOffset(&state, 405));
}

// Wrap walks rowBoxes. After a scroll the off-screen rows still hold the
// window y they had when last painted, which still covers the viewport, so
// a click would map to the old band and scroll_to would jump back there.
static void AClickInAWrappedScrolledEditorIgnoresStaleWindowY() {
    const int kLines = 40;
    char* buf = (char*)Alloc(nullptr, kLines * 2);
    utassert(buf);
    for (int i = 0; i < kLines; i++) {
        buf[i * 2] = 'x';
        buf[i * 2 + 1] = '\n';
    }
    InputState state;
    state.kind = InputKind::Editor;
    state.softWrap = true;
    InputSetValue(&state, Str(buf, kLines * 2));
    Free(nullptr, buf);
    state.lastLineH = 20;
    state.lastFont = 14;
    state.lastBounds = {12, 80, 200, 20};
    state.inputBounds = {0, 80, 400, 400};
    state.scrollY = 200;
    int rows = InputLinesLen(&state);
    for (int i = 0; i < rows; i++) {
        Bounds box = {12, 80.f + (float)i * 20.f, 200, 20};
        VecAppend(state.rowBoxes, box);
    }
    PaintCtx ctx = {};
    int at = InputIndexForPosition(&state, &ctx, 12, 80.f + 30.f, nullptr);
    utassert(at == InputLineStartOffset(&state, 11));
}

static void ScrollToCursorUsesDocumentYNotStaleWindowY() {
    const int kLines = 40;
    char* buf = (char*)Alloc(nullptr, kLines * 2);
    utassert(buf);
    for (int i = 0; i < kLines; i++) {
        buf[i * 2] = 'x';
        buf[i * 2 + 1] = '\n';
    }
    InputState state;
    state.kind = InputKind::Editor;
    state.softWrap = true;
    InputSetValue(&state, Str(buf, kLines * 2));
    Free(nullptr, buf);
    state.lastLineH = 20;
    state.viewH = 400;
    state.contentH = (float)kLines * 20.f;
    state.scrollY = 400;
    // Row 0 last painted at the top of the file; row 20 is on screen now
    // at the same window y. Subtracting those would put the caret at 0.
    int rows = InputLinesLen(&state);
    for (int i = 0; i < rows; i++) {
        Bounds box = {12, 80.f + (float)i * 20.f, 200, 20};
        if (i == 20) {
            box.y = 80;
        }
        VecAppend(state.rowBoxes, box);
    }
    state.selectedRange = SelectionAt(InputLineStartOffset(&state, 20));
    InputScrollToCursor(&state, InputMoveDir::None);
    utassert(state.scrollY > 200);
}

// test_unfold_at: unfolding at a position opens exactly the folds hiding it.
//
// A fold keeps its own first and last line visible, so a position on either
// of them opens nothing. Nested folds all open at once, sibling folds stay
// closed, and the opened ranges stay fold candidates.
static void UnfoldingAtAPositionOpensExactlyWhatHidesIt() {
    InputState s;
    s.kind = InputKind::Editor;
    s.mode.kind = LayoutModeKind::CodeEditor;
    s.mode.folding = true;
    InputSetValue(&s, StrL("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl"));

    // An outer fold over lines 0..=5, a fold nested inside it, and a sibling
    // fold that must never be touched.
    FoldRange ranges[3] = {};
    ranges[0].startLine = 0;
    ranges[0].endLine = 5;
    ranges[1].startLine = 2;
    ranges[1].endLine = 4;
    ranges[2].startLine = 7;
    ranges[2].endLine = 10;
    InputSetFoldCandidates(&s, ranges, 3);
    FoldMapSetFolded(&s.folds, 0, true);
    FoldMapSetFolded(&s.folds, 2, true);
    FoldMapSetFolded(&s.folds, 7, true);
    FoldMapRebuild(&s.folds, InputLinesLen(&s));

    // The outer fold's own first and last line stay visible, so neither
    // position opens anything.
    const int kOwnLines[] = {0, 5};
    for (int line : kOwnLines) {
        utassert(!FoldMapLineHidden(&s.folds, line));
        utassert(!InputUnfoldAt(&s, nullptr, nullptr, {line, 0}));
        utassert(FoldMapIsFolded(&s.folds, 0));
        utassert(FoldMapIsFolded(&s.folds, 2));
        utassert(FoldMapIsFolded(&s.folds, 7));
    }

    // Line 3 is hidden by both the outer and the nested fold, so both open;
    // the sibling fold does not.
    utassert(FoldMapLineHidden(&s.folds, 3));
    utassert(InputUnfoldAt(&s, nullptr, nullptr, {3, 0}));
    FoldMapRebuild(&s.folds, InputLinesLen(&s));
    utassert(!FoldMapLineHidden(&s.folds, 3));
    utassert(!FoldMapIsFolded(&s.folds, 0));
    utassert(!FoldMapIsFolded(&s.folds, 2));
    utassert(FoldMapIsFolded(&s.folds, 7));
    // The opened ranges are still candidates for refolding.
    utassert(FoldMapIsCandidate(&s.folds, 0));
    utassert(FoldMapIsCandidate(&s.folds, 2));

    // Nothing is hidden there any more, so a second call is a no-op.
    utassert(!InputUnfoldAt(&s, nullptr, nullptr, {3, 0}));

    // A field that is not a folding code editor has no folds to open.
    InputState plain;
    plain.kind = InputKind::Textarea;
    InputSetValue(&plain, StrL("a\nb\nc"));
    utassert(!InputUnfoldAt(&plain, nullptr, nullptr, {1, 0}));
}

// ─── multiple cursors ─────────────────────────────────────────────────────
//
// state.rs mod tests, the multi-cursor cases that are pure state: the alt
// click and alt+shift block come in through the offsets the window would
// have resolved from the pointer.

static bool ExtraIs(const InputState& s, int i, int start, int end) {
    return i < s.extraCursors.len && s.extraCursors[i].range.start == start &&
           s.extraCursors[i].range.end == end;
}

static InputState* MakeEditor(InputState* s, const char* text) {
    s->kind = InputKind::Editor;
    InputSetValue(s, Str(text));
    return s;
}

// add_cursor_at: a second caret, and a keystroke writes at both.
static void AnAltClickAddsACursorAndTypingWritesAtEach() {
    InputState s;
    MakeEditor(&s, "aa\nbb\ncc");
    InputMoveTo(&s, nullptr, nullptr, 0);
    InputAddCursorAt(&s, nullptr, nullptr, 3);
    utassert(InputCursorCount(&s) == 2);
    utassert(ExtraIs(s, 0, 3, 3));

    Type(&s, "x");
    utassert(ValueIs(s, "xaa\nxbb\ncc"));
    utassert(RangeIs(s, 1, 1));
    utassert(ExtraIs(s, 0, 5, 5));

    // Enter goes to every cursor too.
    Act(&s, InputAction::Enter);
    utassert(ValueIs(s, "x\naa\nx\nbb\ncc"));
    utassert(InputCursorCount(&s) == 2);

    // Rejected inside an existing selection, on top of an existing caret,
    // and in a single-line field.
    InputState one;
    InputSetValue(&one, StrL("hello"));
    InputAddCursorAt(&one, nullptr, nullptr, 3);
    utassert(InputCursorCount(&one) == 1);

    InputState sel;
    MakeEditor(&sel, "hello\nworld");
    InputSetSelectedRange(&sel, nullptr, nullptr, 0, 3);
    InputAddCursorAt(&sel, nullptr, nullptr, 1);
    utassert(InputCursorCount(&sel) == 1);
    InputAddCursorAt(&sel, nullptr, nullptr, 8);
    utassert(InputCursorCount(&sel) == 2);
    InputAddCursorAt(&sel, nullptr, nullptr, 8);
    utassert(InputCursorCount(&sel) == 2);
    // A click drops them all again.
    InputMoveTo(&sel, nullptr, nullptr, 2);
    utassert(InputCursorCount(&sel) == 1);
}

// Backspace and delete take one character at every caret as one step, and
// an undo puts every caret back where it was.
static void DeletesAtEveryCursorAreOneUndoStep() {
    InputState s;
    MakeEditor(&s, "aa\nbb");
    InputMoveTo(&s, nullptr, nullptr, 2);
    InputAddCursorAt(&s, nullptr, nullptr, 5);

    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, "a\nb"));
    utassert(RangeIs(s, 1, 1));
    utassert(ExtraIs(s, 0, 3, 3));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "aa\nbb"));
    utassert(InputCursorCount(&s) == 2);
    utassert(RangeIs(s, 2, 2));
    utassert(ExtraIs(s, 0, 5, 5));

    Act(&s, InputAction::Redo);
    utassert(ValueIs(s, "a\nb"));
    utassert(RangeIs(s, 1, 1));
    utassert(ExtraIs(s, 0, 3, 3));

    // Two backspaces coalesce the way one cursor's do.
    InputState run;
    MakeEditor(&run, "abc\ndef");
    InputMoveTo(&run, nullptr, nullptr, 3);
    InputAddCursorAt(&run, nullptr, nullptr, 7);
    Act(&run, InputAction::Backspace);
    Act(&run, InputAction::Backspace);
    utassert(ValueIs(run, "a\nd"));
    Act(&run, InputAction::Undo);
    utassert(ValueIs(run, "abc\ndef"));
    utassert(RangeIs(run, 3, 3));
    utassert(ExtraIs(run, 0, 7, 7));

    // Forward delete at the front of both lines.
    InputState fwd;
    MakeEditor(&fwd, "abc\ndef");
    InputMoveTo(&fwd, nullptr, nullptr, 0);
    InputAddCursorAt(&fwd, nullptr, nullptr, 4);
    Act(&fwd, InputAction::Delete);
    utassert(ValueIs(fwd, "bc\nef"));
    utassert(RangeIs(fwd, 0, 0));
    utassert(ExtraIs(fwd, 0, 3, 3));
}

// A run of typing at two carets is one undo, back to both carets.
static void TypingAtEveryCursorUndoesToEveryCursor() {
    InputState s;
    MakeEditor(&s, "ab\ncd");
    InputMoveTo(&s, nullptr, nullptr, 0);
    InputAddCursorAt(&s, nullptr, nullptr, 3);
    Type(&s, "x");
    Type(&s, "y");
    utassert(ValueIs(s, "xyab\nxycd"));
    utassert(RangeIs(s, 2, 2));
    utassert(ExtraIs(s, 0, 7, 7));

    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "ab\ncd"));
    utassert(InputCursorCount(&s) == 2);
    utassert(RangeIs(s, 0, 0));
    utassert(ExtraIs(s, 0, 3, 3));
}

// escape: the extras go first, and only then does the key mean anything else.
static void EscapeCollapsesTheExtraCursorsFirst() {
    InputState s;
    MakeEditor(&s, "ab\ncd");
    InputAddCursorAt(&s, nullptr, nullptr, 3);
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Escape, false));
    utassert(InputCursorCount(&s) == 1);
    utassert(ValueIs(s, "ab\ncd"));
    utassert(!InputPerform(&s, nullptr, nullptr, InputAction::Escape, false));
}

// add_cursor_below / add_cursor_above keep the column, skip a cursor that
// has nowhere to go, and never double up.
static void AddCursorBelowKeepsTheColumn() {
    InputState s;
    MakeEditor(&s, "abc\nde\nfgh");
    InputMoveTo(&s, nullptr, nullptr, 1);
    Act(&s, InputAction::AddCursorBelow);
    utassert(InputCursorCount(&s) == 2);
    utassert(RangeIs(s, 1, 1));
    utassert(ExtraIs(s, 0, 5, 5));

    // Both cursors step down: the first lands on the second's row, where a
    // caret already is, so only the second adds one.
    Act(&s, InputAction::AddCursorBelow);
    utassert(InputCursorCount(&s) == 3);
    utassert(ExtraIs(s, 1, 8, 8));

    // Nothing below the last row.
    Act(&s, InputAction::AddCursorBelow);
    utassert(InputCursorCount(&s) == 3);

    InputState up;
    MakeEditor(&up, "abc\nde");
    InputMoveTo(&up, nullptr, nullptr, 6);
    Act(&up, InputAction::AddCursorAbove);
    utassert(InputCursorCount(&up) == 2);
    utassert(ExtraIs(up, 0, 2, 2));

    // A single-line field has no rows to add on.
    InputState one;
    InputSetValue(&one, StrL("abc"));
    utassert(!InputPerform(&one, nullptr, nullptr, InputAction::AddCursorBelow,
                           false));
}

// Every move and select goes to every cursor, and cursors that meet merge.
static void MovementFansOutOverEveryCursor() {
    InputState s;
    MakeEditor(&s, "abc\ndef");
    InputMoveTo(&s, nullptr, nullptr, 0);
    InputAddCursorAt(&s, nullptr, nullptr, 4);
    Act(&s, InputAction::MoveRight);
    utassert(RangeIs(s, 1, 1));
    utassert(ExtraIs(s, 0, 5, 5));

    Act(&s, InputAction::SelectRight);
    utassert(RangeIs(s, 1, 2));
    utassert(ExtraIs(s, 0, 5, 6));

    Act(&s, InputAction::MoveEnd);
    utassert(RangeIs(s, 3, 3));
    utassert(ExtraIs(s, 0, 7, 7));

    Act(&s, InputAction::MoveHome);
    utassert(RangeIs(s, 0, 0));
    utassert(ExtraIs(s, 0, 4, 4));

    Act(&s, InputAction::MoveDown);
    // The first caret walks onto the second's row and the two merge.
    utassert(InputCursorCount(&s) == 1);
    utassert(RangeIs(s, 4, 4));

    // Going to the document's end is a plain move_to: one cursor.
    InputAddCursorAt(&s, nullptr, nullptr, 0);
    Act(&s, InputAction::MoveToEnd);
    utassert(InputCursorCount(&s) == 1);
    utassert(RangeIs(s, 7, 7));

    // Selections that grow into one another merge into one, and the active
    // one stays active.
    InputState merge;
    MakeEditor(&merge, "abcdef\n");
    InputMoveTo(&merge, nullptr, nullptr, 2);
    InputAddCursorAt(&merge, nullptr, nullptr, 3);
    Act(&merge, InputAction::SelectRight);
    Act(&merge, InputAction::SelectRight);
    utassert(InputCursorCount(&merge) == 1);
    utassert(RangeIs(merge, 2, 5));
}

// build_columnar_selection: one selection per row over the same columns,
// clipped to a short row, and a keystroke replaces each.
static void AColumnarSelectionIsOneSelectionPerRow() {
    InputState s;
    MakeEditor(&s, "abcd\nef\nghij");
    InputBuildColumnarSelection(&s, nullptr, nullptr, 1, 11);
    utassert(InputCursorCount(&s) == 3);
    utassert(RangeIs(s, 1, 3));
    utassert(ExtraIs(s, 0, 6, 7));
    utassert(ExtraIs(s, 1, 9, 11));

    Type(&s, "X");
    utassert(ValueIs(s, "aXd\neX\ngXj"));
    utassert(RangeIs(s, 2, 2));
    utassert(ExtraIs(s, 0, 6, 6));
    utassert(ExtraIs(s, 1, 9, 9));

    // Dragging back up past the anchor is the same block.
    InputState back;
    MakeEditor(&back, "abcd\nef\nghij");
    InputBuildColumnarSelection(&back, nullptr, nullptr, 11, 1);
    utassert(InputCursorCount(&back) == 3);
    utassert(RangeIs(back, 1, 3));
}

static void AShortRowDoesNotNarrowAColumnarSelection() {
    InputState s;
    MakeEditor(&s, "abcdef\nab\nabcdef");
    InputBuildColumnarSelection(&s, nullptr, nullptr, {1, 0}, {9, 3});
    utassert(InputCursorCount(&s) == 2);
    utassert(RangeIs(s, 1, 5));
    utassert(ExtraIs(s, 0, 8, 9));
}

static void ACrLfIsOneCursorBoundary() {
    InputState s;
    MakeEditor(&s, "first\r\nlast");
    InputMoveTo(&s, nullptr, nullptr, 0);
    Act(&s, InputAction::MoveEnd);
    utassert(RangeIs(s, 5, 5));
    Act(&s, InputAction::MoveRight);
    utassert(RangeIs(s, 7, 7));
    Act(&s, InputAction::MoveLeft);
    utassert(RangeIs(s, 5, 5));
    Act(&s, InputAction::SelectToEndOfLine);
    utassert(RangeIs(s, 5, 5));

    InputMoveTo(&s, nullptr, nullptr, 6);
    utassert(RangeIs(s, 5, 5));
    utassert(ValueIs(s, "first\r\nlast"));

    InputState lone;
    MakeEditor(&lone, "a\rb");
    Act(&lone, InputAction::MoveRight);
    utassert(RangeIs(lone, 1, 1));
    Act(&lone, InputAction::MoveRight);
    utassert(RangeIs(lone, 2, 2));
}

// test_block_indent_tracks_all_preceding_edits /
// test_multi_cursor_indent_then_outdent_roundtrips: the block pair indents
// every cursor's line and the inline pair puts a tab at every caret, each as
// one undo step that puts every caret back.
static void IndentMovesEveryCursorsLine() {
    InputState s;
    MakeEditor(&s, "ab\ncd\nef");
    InputMoveTo(&s, nullptr, nullptr, 0);
    InputAddCursorAt(&s, nullptr, nullptr, 3);
    InputAddCursorAt(&s, nullptr, nullptr, 6);
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Indent, false));
    utassert(ValueIs(s, "    ab\n    cd\n    ef"));
    utassert(RangeIs(s, 4, 4));
    utassert(ExtraIs(s, 0, 11, 11));
    utassert(ExtraIs(s, 1, 18, 18));
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Outdent, false));
    utassert(ValueIs(s, "ab\ncd\nef"));
    utassert(RangeIs(s, 0, 0));
    utassert(ExtraIs(s, 0, 3, 3));
    utassert(ExtraIs(s, 1, 6, 6));

    // The inline pair, the same round trip, one undo step each way.
    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::IndentInline, false));
    utassert(ValueIs(s, "    ab\n    cd\n    ef"));
    utassert(ExtraIs(s, 1, 18, 18));
    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::OutdentInline, false));
    utassert(ValueIs(s, "ab\ncd\nef"));
    utassert(ExtraIs(s, 1, 6, 6));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "    ab\n    cd\n    ef"));
    utassert(InputCursorCount(&s) == 3);
    utassert(ExtraIs(s, 1, 18, 18));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "ab\ncd\nef"));
    utassert(InputCursorCount(&s) == 3);
    utassert(ExtraIs(s, 1, 6, 6));

    // test_inline_outdent_only_removes_line_indentation: a mid-line tab lands
    // at the caret and the inline outdent does not take it back.
    InputState mid;
    MakeEditor(&mid, "12\n12");
    InputMoveTo(&mid, nullptr, nullptr, 1);
    InputAddCursorAt(&mid, nullptr, nullptr, 4);
    Act(&mid, InputAction::IndentInline);
    utassert(ValueIs(mid, "1    2\n1    2"));
    utassert(RangeIs(mid, 5, 5));
    utassert(ExtraIs(mid, 0, 12, 12));
    Act(&mid, InputAction::OutdentInline);
    utassert(ValueIs(mid, "1    2\n1    2"));

    // test_block_outdent_clamps_cursor_inside_indent.
    InputState inside;
    MakeEditor(&inside, "ab\n    cd");
    InputMoveTo(&inside, nullptr, nullptr, 5);
    Act(&inside, InputAction::Outdent);
    utassert(ValueIs(inside, "ab\ncd"));
    utassert(RangeIs(inside, 3, 3));
}

// build_columnar_selection walks wrap display rows: a soft-wrapped line is
// one row per visual row to the block, at the byte column within each.
static void AColumnarSelectionFollowsTheWrappedRows() {
    PaintApp* paint = PaintAppNew();
    utassert(paint);
    if (!paint) {
        return;
    }
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.pa = paint;
    InputState s;
    MakeEditor(&s, "alpha beta gamma delta epsilon\nzeta");
    s.softWrap = true;
    const float font = 16.f;
    const float width = 72.f;
    s.lastFont = font;
    s.lastLineH = font * 1.5f;
    s.lastBounds = {0, 0, width, 400};
    s.inputBounds = s.lastBounds;
    // Where the first line breaks, read the way the block reads it.
    Str line = StrL("alpha beta gamma delta epsilon");
    int boundary = -1;
    for (int i = 1; i < len(line); i++) {
        float ex = 0, ey = 0, eh = 0, nx = 0, ny = 0, nh = 0;
        if (TextPointAt(&win->paint, line, font, width, true, i, &ex, &ey, &eh,
                        false, 1.5f, true) &&
            TextPointAt(&win->paint, line, font, width, true, i, &nx, &ny, &nh,
                        false, 1.5f, false) &&
            ey + 0.5f < ny) {
            boundary = i;
            break;
        }
    }
    utassert(boundary > 0);
    if (boundary > 0) {
        // From column 1 of the first visual row to column 3 of the second:
        // two selections on the one document line, one per visual row.
        InputBuildColumnarSelection(&s, &app, win, 1, boundary + 3);
        utassert(InputCursorCount(&s) == 2);
        utassert(RangeIs(s, 1, 3));
        utassert(ExtraIs(s, 0, boundary + 1, boundary + 3));
    }
    TextMeasClear(&win->paint);
    delete win;
    PaintAppFree(paint);
}

// replace_text_in_ranges: the paste that hands one line to each cursor, in
// document order, whichever is active.
static void RangesAreReplacedHighestFirst() {
    InputState s;
    MakeEditor(&s, "ab\ncd");
    InputMoveTo(&s, nullptr, nullptr, 4);
    InputAddCursorAt(&s, nullptr, nullptr, 0);
    Selection ranges[2] = {SelectionAt(4), SelectionAt(0)};
    Str texts[2] = {StrL("22"), StrL("1")};
    utassert(InputReplaceTextInRanges(&s, nullptr, nullptr, ranges, texts, 2));
    utassert(ValueIs(s, "1ab\nc22d"));
    // The active cursor is still the one that was: after its own edit.
    utassert(RangeIs(s, 7, 7));
    utassert(ExtraIs(s, 0, 1, 1));

    // One step back, to both cursors.
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "ab\ncd"));
    utassert(RangeIs(s, 4, 4));
    utassert(ExtraIs(s, 0, 0, 0));
}

static void LanguagePairsAndSmartIndent() {
    InputState s;
    MakeEditor(&s, "");
    Type(&s, "(");
    utassert(ValueIs(s, "()"));
    utassert(RangeIs(s, 1, 1));

    // A closer already at the caret is traversed, not duplicated.
    Type(&s, ")");
    utassert(ValueIs(s, "()"));
    utassert(RangeIs(s, 2, 2));

    // Backspace between a configured pair takes the pair as one edit.
    InputMoveTo(&s, nullptr, nullptr, 1);
    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, ""));

    MakeEditor(&s, "{}");
    InputMoveTo(&s, nullptr, nullptr, 1);
    Act(&s, InputAction::Enter);
    utassert(ValueIs(s, "{\n    \n}"));
    utassert(RangeIs(s, 6, 6));

    InputSetSmartIndent(&s, false, nullptr);
    InputSetValue(&s, StrL("{"));
    InputMoveTo(&s, nullptr, nullptr, 1);
    Act(&s, InputAction::Enter);
    utassert(ValueIs(s, "{\n"));
}

static Str PythonLanguage(void*) {
    return StrL("python");
}

static void IndentationPatternsMatchPythonRules() {
    Str increase = StrL("[\\{\\(\\[:]\\s*$");
    Str decrease = StrL("^\\s*[\\}\\)\\]]");
    utassert(IndentPatternMatch(increase, StrL("if enabled:")));
    utassert(IndentPatternMatch(increase, StrL("if enabled:  ")));
    utassert(IndentPatternMatch(increase, StrL("items = [")));
    utassert(!IndentPatternMatch(increase, StrL("x = 1")));
    utassert(IndentPatternMatch(decrease, StrL("}")));
    utassert(IndentPatternMatch(decrease, StrL("  ]")));
    utassert(!IndentPatternMatch(decrease, StrL("x}")));

    App app;
    LanguageConfig python = LanguageConfig::Default();
    python.indentation = IndentationRules::FromPatterns(increase, decrease);
    python.hasIndentationRules = true;
    InputSetLanguageConfig(&app, StrL("python"), python);

    InputState s;
    MakeEditor(&s, "if enabled:");
    s.highlighter.language = PythonLanguage;
    InputMoveTo(&s, &app, nullptr, 11);
    InputPerform(&s, &app, nullptr, InputAction::Enter, false);
    utassert(ValueIs(s, "if enabled:\n    "));
}

static void GeneratedPairsAreTrackedThroughEditsAndHistory() {
    InputState s;
    MakeEditor(&s, "");
    Type(&s, "(");
    utassert(ValueIs(s, "()"));
    Type(&s, "a");
    utassert(ValueIs(s, "(a)"));
    Type(&s, ")");
    utassert(ValueIs(s, "(a)"));
    utassert(RangeIs(s, 3, 3));
    InputMoveTo(&s, nullptr, nullptr, 2);
    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, "()"));
    InputMoveTo(&s, nullptr, nullptr, 1);
    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, ""));

    Type(&s, "(");
    utassert(ValueIs(s, "()"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, ""));
    Act(&s, InputAction::Redo);
    utassert(ValueIs(s, "()"));
    InputMoveTo(&s, nullptr, nullptr, 1);
    Act(&s, InputAction::Backspace);
    utassert(ValueIs(s, ""));
}

static bool ConsumeImagePaste(void* data, const ClipboardItem& item, App*,
                              Window*) {
    int* calls = (int*)data;
    (*calls)++;
    return item.HasImage();
}

static void TheThreeInputBuildersInstallPasteInterception() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    int calls = 0;

    InputState input;
    component::Input::New(&cx, StrL("input"), &input)
        ->OnPaste(&ConsumeImagePaste, &calls)
        ->IntoEl();
    utassert(input.pasteHandler == &ConsumeImagePaste);
    utassert(input.pasteHandlerData == &calls);

    InputState textarea;
    component::Textarea::New(&cx, StrL("textarea"), &textarea)
        ->OnPaste(&ConsumeImagePaste, &calls)
        ->IntoEl();
    utassert(textarea.pasteHandler == &ConsumeImagePaste);

    InputState editor;
    component::Editor::New(&cx, StrL("editor"), &editor)
        ->OnPaste(&ConsumeImagePaste, &calls)
        ->IntoEl();
    utassert(editor.pasteHandler == &ConsumeImagePaste);

    ClipboardItem image;
    uint8_t byte = 0;
    image.imageBytes = &byte;
    image.imageBytesLen = 1;
    utassert(editor.pasteHandler(editor.pasteHandlerData, image, &app, win));
    utassert(calls == 1);

    component::Input::New(&cx, StrL("readonly"), &input)
        ->Readonly()
        ->OnPaste(&ConsumeImagePaste, &calls)
        ->IntoEl();
    utassert(!input.pasteHandler && !input.pasteHandlerData);

    ArenaDelete(arena);
    delete win;
}

// state.rs single_line_is_centered_in_a_taller_frame: the frame is laid out
// by the application, which should not have to center a single line in it.
static void SingleLineIsCenteredInATallerFrame() {
    App app = {};
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    InputState state;
    InputSetValue(&state, StrL("a"));
    El* line = gpui::Input::New(&cx, &state);
    El* frame = InputBase::New(&cx, StrL("frame"), true)->H(60)->Child(line);
    El* page = Div(arena)->FlexCol()->W(400)->H(100)->Child(frame);
    const RuntimeStyle& th = RuntimeStyleNow(&app);
    LayoutEl(&win->paint, page, 0, 0, 400, 100, th.fontSize, th.foreground);
    utassertnear(line->y + line->h / 2, 30.f);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// state.rs test_paste_without_text_leaves_the_selection_alone: an image-only
// clipboard must not replace the selection with nothing. Rust drives it
// through the Paste action and a test clipboard; the insert is the seam here,
// since the action reads the real system clipboard.
static void PasteWithoutTextLeavesTheSelectionAlone() {
    InputState s;
    InputSetValue(&s, StrL("hello world"));
    InputSelectAll(&s, nullptr, nullptr);
    ClipboardItem image;
    const uint8_t png[4] = {0x89, 'P', 'N', 'G'};
    image.imageBytes = png;
    image.imageBytesLen = 4;
    InputInsertClipboard(&s, nullptr, nullptr, image);
    utassert(ValueIs(s, "hello world"));
    utassert(RangeIs(s, 0, 11));
}

// state.rs test_paste_target_tracks_edits_and_selections.
static void PasteTargetTracksEditsAndSelections() {
    InputState s;
    MakeEditor(&s, "abc");
    InputMoveTo(&s, nullptr, nullptr, 2);
    InputPasteTarget target = InputPasteTargetOf(&s);
    // Nothing happened: a paste asked for then still applies.
    utassert(InputPasteTargetOf(&s) == target);

    // Moving the caret changes where the paste would go.
    InputMoveTo(&s, nullptr, nullptr, 1);
    InputPasteTarget moved = InputPasteTargetOf(&s);
    utassert(moved != target);

    // Editing the text changes it too, even with the caret put back.
    Type(&s, "x");
    InputMoveTo(&s, nullptr, nullptr, 1);
    utassert(InputPasteTargetOf(&s) != moved);
}

static void InlineTokenContentValidatesRanges() {
    InputContent c = InputContent::New(StrL("Ask @alice"));
    InlineToken tok = InlineToken::New(StrL("person:alice"), StrL("@alice"))
                          .WithLabel(StrL("Alice"));
    utassert(c.WithToken(4, 10, tok) == InlineTokenError::Ok);
    utassert(c.tokens.len == 1);
    utassert(c.tokens[0].start == 4 && c.tokens[0].end == 10);
    utassert(c.WithToken(4, 10, tok) == InlineTokenError::OverlappingTokens);
    utassert(c.WithToken(0, 0, tok) == InlineTokenError::InvalidRange);
    utassert(c.WithToken(4, 9, tok) == InlineTokenError::TextMismatch);
    InlineToken bad = InlineToken::New(StrL(""), StrL("@alice"));
    utassert(bad.Validate() == InlineTokenError::InvalidToken);
    VecReset(c.tokens);
}

static int TokenCount(const InputState& s) {
    const Vec<InlineTokenSpan>* spans = InputTokens(&s);
    return spans ? spans->len : 0;
}

static bool TokenRangeIs(const InputState& s, int i, int start, int end) {
    const Vec<InlineTokenSpan>* spans = InputTokens(&s);
    return spans && i < spans->len && (*spans)[i].start == start &&
           (*spans)[i].end == end;
}

static void InlineTokensAreAtomicForCaretAndHistory() {
    InputState s;
    InputSetValue(&s, StrL("问 @alice!"));
    InlineToken tok = InlineToken::New(StrL("alice-1"), StrL("@alice"))
                          .WithLabel(StrL("Alice"));
    utassert(InputReplaceRangeWithToken(&s, nullptr, nullptr, 4, 10, tok) ==
             InlineTokenError::Ok);
    utassert(ValueIs(s, "问 @alice!"));
    utassert(TokenCount(s) == 1);
    utassert(TokenRangeIs(s, 0, 4, 10));
    utassert(InputNextEndOfWordAt(&s, 4) == 10);
    utassert(InputPreviousStartOfWordAt(&s, 10) == 4);
    utassert(InputPreviousBoundary(&s, 10) == 4);
    utassert(InputNextBoundary(&s, 4) == 10);

    Act(&s, InputAction::Undo);
    utassert(TokenCount(s) == 0);
    utassert(ValueIs(s, "问 @alice!"));
    Act(&s, InputAction::Redo);
    utassert(TokenCount(s) == 1);
    utassert(InlineTokenEq((*InputTokens(&s))[0].token, tok));

    InputSetSelectedRange(&s, nullptr, nullptr, 6, 7);
    utassert(RangeIs(s, 4, 10));
    InputReplaceTextInRange(&s, nullptr, nullptr, nullptr, Str{});
    utassert(ValueIs(s, "问 !"));
    utassert(TokenCount(s) == 0);
    Act(&s, InputAction::Undo);
    utassert(TokenCount(s) == 1);
    utassert(InlineTokenEq((*InputTokens(&s))[0].token, tok));

    InputSetSelectedRange(&s, nullptr, nullptr, 0, 0);
    InputReplaceTextInRange(&s, nullptr, nullptr, nullptr, StrL("🙂"));
    utassert(TokenRangeIs(s, 0, 8, 14));
    Act(&s, InputAction::Undo);
    utassert(TokenRangeIs(s, 0, 4, 10));

    utassert(InputReplaceRangeWithToken(&s, nullptr, nullptr, 0, 0, tok) ==
             InlineTokenError::Ok);
    utassert(ValueIs(s, "@alice问 @alice!"));
    utassert(TokenCount(s) == 2);
    Act(&s, InputAction::Undo);
    utassert(TokenCount(s) == 1);
    utassert(TokenRangeIs(s, 0, 4, 10));

    InputSetValue(&s, StrL("问 @alice!"));
    utassert(TokenCount(s) == 0);
    utassert(s.undo.undos.len == 0);
}

static void InlineTokensRespectModeAndContent() {
    InputState multi;
    multi.kind = InputKind::Textarea;
    InputContent content = InputContent::New(StrL("@a@b\n后面"));
    utassert(content.WithToken(0, 2, InlineToken::New(StrL("a"), StrL("@a"))) ==
             InlineTokenError::Ok);
    utassert(content.WithToken(2, 4, InlineToken::New(StrL("b"), StrL("@b"))) ==
             InlineTokenError::Ok);
    InputSetValue(&multi, content);
    utassert(TokenCount(multi) == 2);
    utassert(InputPreviousBoundary(&multi, 2) == 0);
    utassert(InputNextBoundary(&multi, 2) == 4);

    Mark(&multi, "中");
    utassert(InputReplaceWithToken(&multi, nullptr, nullptr,
                                   InlineToken::New(StrL("x"), StrL("x"))) ==
             InlineTokenError::CompositionActive);
    InputUnmarkText(&multi, nullptr, nullptr);

    InputState masked;
    InputSetValue(&masked, StrL("ab"));
    utassert(InputReplaceWithToken(&masked, nullptr, nullptr,
                                   InlineToken::New(StrL("a"), StrL("@a"))) ==
             InlineTokenError::Ok);
    masked.masked = true;
    utassert(!InputTokensVisible(&masked));
    utassert(InputReplaceWithToken(&masked, nullptr, nullptr,
                                   InlineToken::New(StrL("b"), StrL("b"))) ==
             InlineTokenError::UnsupportedMode);

    InputState editor;
    editor.kind = InputKind::Editor;
    InputSetValue(&editor, StrL("ab"));
    utassert(InputReplaceWithToken(&editor, nullptr, nullptr,
                                   InlineToken::New(StrL("a"), StrL("@a"))) ==
             InlineTokenError::UnsupportedMode);

    VecReset(content.tokens);
}

static El* FindWrappedTokenRow(El* el) {
    if (!el) {
        return nullptr;
    }
    if (el->style.flexWrap) {
        return el;
    }
    for (El* child = el->first; child; child = child->next) {
        if (El* found = FindWrappedTokenRow(child)) {
            return found;
        }
    }
    return nullptr;
}

static void TextareaTokenGapsBreakAtUtf8Characters() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    InputState state;
    state.kind = InputKind::Textarea;
    state.softWrap = true;
    InputContent content = InputContent::New(StrL("ab@a🙂cd"));
    utassert(content.WithToken(
                 2, 4, InlineToken::New(StrL("person:a"), StrL("@a"))) ==
             InlineTokenError::Ok);
    InputSetValue(&state, content);
    El* row = FindWrappedTokenRow(Textarea::New(&cx, &state));
    utassert(row);
    const char* expected[] = {"a", "b", nullptr, "🙂", "c", "d"};
    El* child = row->first;
    for (int i = 0; i < 6; i++) {
        utassert(child);
        if (expected[i]) {
            utassert(child->kind == ElKind::Text);
            utassert(StrEq(child->text, Str(expected[i])));
            utassert(child->style.flexShrink == 0);
        } else {
            utassert(child->kind == ElKind::Div);
            utassert(child->style.flexShrink == 0);
        }
        child = child->next;
    }
    utassert(!child);
    VecReset(content.tokens);
    ArenaDelete(arena);
    delete win;
}

// blink_cursor.rs. The clock is the window's timer list here: a flip is the
// armed interval firing, and a loop that has ended is one with nothing armed.
namespace {
struct BlinkFixture {
    App app;
    Window* win = nullptr;
    EntityId handle = {};

    BlinkFixture() {
        win = new Window();
        win->app = &app;
    }
    ~BlinkFixture() {
        delete win;
        EntityDropAll(&app);
    }
    BlinkCursor* Cursor() {
        Entity<BlinkCursor> e;
        e.id = handle;
        return e.Get(&app);
    }
    // One INTERVAL of the clock: the armed interval fires.
    void Flip() {
        Ctx cx = {&app, win, nullptr, handle};
        TickEvent tick = {};
        BlinkCursor::OnFlip(Cursor(), &cx, &tick);
    }
};
} // namespace

// pausing_a_cursor_that_is_not_blinking_does_not_start_it
static void PausingACursorThatIsNotBlinkingDoesNotStartIt() {
    BlinkFixture f;
    // Never focused, so nothing started it: what a programmatic write to an
    // unfocused input does.
    BlinkPause(&f.app, f.win, &f.handle);
    utassert(!BlinkVisible(&f.app, f.handle));
    utassert(len(f.win->timers) == 0);
}

// test_set_value_on_unfocused_input_stays_quiet: seeding a field that does
// not have the keyboard arms no caret timer, so nothing repaints after it.
static void SetValueOnUnfocusedInputStaysQuiet() {
    App app;
    Window* win = new Window();
    win->app = &app;
    {
        InputState s;
        InputReplaceAll(&s, &app, win, StrL("seeded"));
        utassert(StrEq(InputValue(&s), "seeded"));
        utassert(len(win->timers) == 0);
        utassert(!BlinkVisible(&app, s.blink));
    }
    delete win;
    EntityDropAll(&app);
}

// blurring_a_paused_cursor_leaves_the_next_focus_blinking
static void BlurringAPausedCursorLeavesTheNextFocusBlinking() {
    BlinkFixture f;
    BlinkStart(&f.app, f.win, &f.handle);
    // Typing pauses the blink, then the input is blurred before the pause
    // elapses: tabbing away right after a keystroke does exactly this.
    BlinkPause(&f.app, f.win, &f.handle);
    BlinkStop(&f.app, f.win, &f.handle);
    utassert(!BlinkVisible(&f.app, f.handle));

    // Focusing again shows the cursor and blinks it, rather than leaving a
    // stale pause to swallow the start.
    BlinkStart(&f.app, f.win, &f.handle);
    utassert(BlinkVisible(&f.app, f.handle));
    f.Flip();
    utassert(!BlinkVisible(&f.app, f.handle));
}

// stopping_a_paused_cursor_ends_the_blink_loop
static void StoppingAPausedCursorEndsTheBlinkLoop() {
    BlinkFixture f;
    BlinkStart(&f.app, f.win, &f.handle);
    BlinkPause(&f.app, f.win, &f.handle);
    BlinkStop(&f.app, f.win, &f.handle);
    // A stopped cursor keeps nothing armed that could blink it.
    utassert(f.Cursor()->timer == 0 && len(f.win->timers) == 0);
    utassert(!BlinkVisible(&f.app, f.handle));

    BlinkStart(&f.app, f.win, &f.handle);
    utassert(BlinkVisible(&f.app, f.handle));
}

// stopping_a_blinking_cursor_ends_the_blink_loop
static void StoppingABlinkingCursorEndsTheBlinkLoop() {
    BlinkFixture f;
    BlinkStart(&f.app, f.win, &f.handle);
    BlinkStop(&f.app, f.win, &f.handle);
    utassert(f.Cursor()->timer == 0 && len(f.win->timers) == 0);
    utassert(!BlinkVisible(&f.app, f.handle));
}

// kit/tests/input_focus.rs (#3253): each input is one tab stop, so Tab and
// Shift-Tab walk the inputs in order and wrap, a passive prefix or suffix
// takes no stop of its own, and addon buttons take theirs in paint order.
// Upstream reverted #3246 because its frame and editor registered the same
// focus handle twice and Shift-Tab stuck on the focused editor. Here the
// field and every editor row bound to the state track its handle, so the
// traversal counts a handle once, at its last element. The Rust tests drive
// a window and type between moves; this checks the traversal on fields that
// hold text, which is when the most elements track one handle.
static int InputFocusIdOf(El* e, InputState* state) {
    if (!e) return 0;
    if (e->input == state && e->style.focusId) return e->style.focusId;
    for (El* child = e->first; child; child = child->next) {
        if (int id = InputFocusIdOf(child, state)) return id;
    }
    return 0;
}

static int ButtonFocusIdOf(El* e, Str id) {
    if (!e) return 0;
    if (StrEq(e->id, id) && e->style.focusId) return e->style.focusId;
    for (El* child = e->first; child; child = child->next) {
        if (int found = ButtonFocusIdOf(child, id)) return found;
    }
    return 0;
}

static void InputFocusCyclesThroughInputsAndAddons() {
    for (int buttons = 0; buttons < 2; buttons++) {
        App app;
        component::Init(&app);
        Window* win = new Window();
        win->app = &app;
        Arena* arena = ArenaNew();
        Ctx cx = {&app, win, arena, {}};
        InputState states[3];
        const char* ids[3] = {"first", "second", "third"};
        El* root = Div(arena)->FlexCol()->Pad(16)->Gap(16);
        for (int i = 0; i < 3; i++) {
            InputSetValue(&states[i], StrL("xx"));
            component::Input* input =
                component::Input::New(&cx, Str(ids[i]), &states[i])->W(384);
            if (buttons && i == 1) {
                input
                    ->Prefix(component::Button::New(&cx, StrL("prefix-button"))
                                 ->Label(StrL("Prefix"))
                                 ->IntoEl())
                    ->Suffix(component::Button::New(&cx, StrL("suffix-button"))
                                 ->Label(StrL("Suffix"))
                                 ->IntoEl());
            } else {
                input->Prefix(TextEl(arena, StrL("Prefix")))
                    ->Suffix(TextEl(arena, StrL("Suffix")));
            }
            root->Child(input->IntoEl());
        }
        IdsCollect(root);
        FocusCollect(win, root);
        int first = InputFocusIdOf(root, &states[0]);
        int second = InputFocusIdOf(root, &states[1]);
        int third = InputFocusIdOf(root, &states[2]);
        utassert(first && second && third);
        utassert(first != second && second != third && first != third);
        int order[5] = {first, second, third};
        int stops = 3;
        if (buttons) {
            // tab_cycles_keep_prefix_and_suffix_buttons_focused.
            int prefix = ButtonFocusIdOf(root, StrL("prefix-button"));
            int suffix = ButtonFocusIdOf(root, StrL("suffix-button"));
            utassert(prefix && suffix);
            int withButtons[5] = {first, prefix, second, suffix, third};
            memcpy(order, withButtons, sizeof(order));
            stops = 5;
        }
        // reverse_tab_cycles_three_inputs_with_passive_addons: start on the
        // last one and go backwards first, wrapping, twice over; then
        // forwards.
        win->focusId = third;
        for (int round = 0; round < 2; round++) {
            for (int k = 1; k <= stops; k++) {
                int want = order[(stops - 1 - k + stops) % stops];
                utassert(FocusNext(win, 0, true) == want);
            }
        }
        for (int round = 0; round < 2; round++) {
            for (int k = 0; k < stops; k++) {
                utassert(FocusNext(win, 0, false) == order[k]);
            }
        }
        delete win;
        ArenaDelete(arena);
        AppGlobalClear(&app);
    }
}

// ─── kit/tests/input (#3256) ─────────────────────────────────────────────

// textarea.rs
// vertical_selection_reaches_document_edges_from_inside_the_only_row: with no
// row further up or down, shift-up and shift-down still take the selection to
// the document's start or end, and pressing again keeps it.
static void VerticalSelectionReachesDocumentEdges() {
    InputState s;
    s.kind = InputKind::Textarea;
    InputSetValue(&s, StrL("abcdef"));
    // The field has been laid out, which is when Rust looks for the edge.
    s.lastBounds = Bounds{0, 0, 200, 80};
    InputSetSelectedRange(&s, nullptr, nullptr, 3, 3);
    Act(&s, InputAction::SelectUp);
    utassert(RangeIs(s, 0, 3) && InputCursor(&s) == 0);
    Act(&s, InputAction::SelectUp);
    utassert(RangeIs(s, 0, 3));
    InputSetSelectedRange(&s, nullptr, nullptr, 3, 3);
    Act(&s, InputAction::SelectDown);
    utassert(RangeIs(s, 3, 6) && InputCursor(&s) == 6);
    Act(&s, InputAction::SelectDown);
    utassert(RangeIs(s, 3, 6));

    // A row further on is where the selection goes, at the same column.
    InputSetValue(&s, StrL("abc\ndef"));
    InputSetSelectedRange(&s, nullptr, nullptr, 1, 1);
    Act(&s, InputAction::SelectDown);
    utassert(RangeIs(s, 1, 5));
    Act(&s, InputAction::SelectDown);
    utassert(RangeIs(s, 1, 7));
    utassert(ValueIs(s, "abc\ndef"));
}

// completions.rs
// escape_dismisses_inline_completion_and_tab_returns_to_indentation and
// typing_clears_inline_suggestion_before_next_debounce: every edit drops what
// the providers said about the old document, and escape drops a suggestion that
// is still waiting for its debounce.
static void EditsAndEscapeDropStaleProviderResponses() {
    InputState s;
    s.kind = InputKind::Editor;
    s.codeActionProvider = &WrappingAction;
    InputSetValue(&s, StrL("hello world"));
    InputSetSelectedRange(&s, nullptr, nullptr, 6, 11);
    Act(&s, InputAction::ToggleCodeActions);
    utassert(s.codeActions.open);
    // A deletion triggers no completion, and still closes the menu.
    Act(&s, InputAction::Backspace);
    utassert(!s.codeActions.open);
    utassert(ValueIs(s, "hello "));

    s.inlineCompletionProvider = &TestInlineCompletion;
    Type(&s, "x");
    utassert(!s.inlineCompletion.asked);
    Act(&s, InputAction::Escape);
    utassert(s.inlineCompletion.asked);
    s.inlineCompletion.dueAt = 0;
    utassert(!InputUpdateInlineCompletion(&s, false));
    utassert(!InputHasInlineCompletion(&s));
}

// handle_action_for_context_menu: a host that handles Enter or Escape for
// its popover closes both menus.
static void AHandledConfirmClosesTheMenus() {
    InputState s;
    s.kind = InputKind::Editor;
    CompletionItem item = {};
    item.label = StrL("unwrap");
    InputPresentCompletionItems(&s, 0, StrL(""), &item, 1);
    s.overlayAction = &TakeEverything;
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::MoveDown, false));
    utassert(s.completion.open);
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Enter, false));
    utassert(!InputIsContextMenuOpen(&s));
    utassert(ValueIs(s, ""));
    s.overlayAction = nullptr;
}

// composition.rs
// cancelling_preedit_leaves_no_undo_entry_and_does_not_swallow_later_typing: a
// cancelled preedit still separates the typing on either side of it.
static void ACancelledPreeditSeparatesTyping() {
    InputState s;
    Type(&s, "A");
    Mark(&s, "ni");
    Mark(&s, "");
    utassert(ValueIs(s, "A"));
    utassert(MarkIs(s, -1, -1));
    Type(&s, "x");
    utassert(ValueIs(s, "Ax"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, "A"));
    Act(&s, InputAction::Undo);
    utassert(ValueIs(s, ""));
}

// state.rs test_closed_search_resyncs_matches_after_edits (#3260): a closed
// search does not rescan on each edit, and navigating it rescans first.
static void ClosedSearchResyncsMatchesAfterEdits() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("foo bar foo"));
    InputSetSearchQuery(&s, nullptr, nullptr, StrL("foo"), true);
    InputCloseSearch(&s, nullptr, nullptr);

    InputReplaceAll(&s, nullptr, nullptr, StrL("bar foo"));
    // The edit left the closed search's matches alone.
    utassert(SearchMatcherLen(&s.search.matcher) == 2);
    Selection range = {};
    utassert(InputSearchNext(&s, nullptr, nullptr, &range));
    utassert(range.start == 4 && range.end == 7);

    InputReplaceAll(&s, nullptr, nullptr, StrL("foo foo foo"));
    InputSetSearchQuery(&s, nullptr, nullptr, StrL("foo"), true);
    utassert(SearchMatcherLen(&s.search.matcher) == 3);
}

// state.rs test_replace_text_in_ranges_drives_the_highlighter_once (#3260):
// a multi-cursor keystroke reaches the highlighter as one update. Upstream
// batches the per-edit envelopes into update_batch; the highlighter is driven
// once per frame here, from the envelope the text funnel leaves, and more
// than one splice collapses it to one whole-document update.
static void MultiCursorTypingLeavesOneHighlighterUpdate() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("ab\nab"));
    s.hasPendingEdit = false;
    InputSetSelectedRange(&s, nullptr, nullptr, 1, 1);
    InputAddCursorAt(&s, nullptr, nullptr, 4);
    uint64_t version = s.docVersion;
    InputReplaceTextInRange(&s, nullptr, nullptr, nullptr, StrL("x"));
    utassert(ValueIs(s, "axb\naxb"));
    utassert(s.docVersion > version);
    utassert(s.hasPendingEdit);
    utassert(s.pendingEdit.oldEndByte == -1 &&
             s.pendingEdit.newEndByte == len(InputValue(&s)));
}

// Offers one action named after the range it was asked about.
static int RangeActions(void* data, Arena* a, Str text, Selection sel,
                        CodeActionItem* out, int cap) {
    (void)data;
    (void)text;
    if (cap > 0 && out) {
        out[0].title = StrDup(a, fmt("%d..%d", sel.start, sel.end));
    }
    return 1;
}

static char gPerformedTitle[32] = {};

static bool PerformRange(void* data, InputState* s, App* app, Window* win,
                         const CodeActionItem* item) {
    (void)data;
    (void)s;
    (void)app;
    (void)win;
    int n = len(item->title) < 31 ? len(item->title) : 31;
    memcpy(gPerformedTitle, item->title.s, (size_t)n);
    gPerformedTitle[n] = 0;
    return true;
}

// completions.rs requesting_code_actions_again_replaces_the_open_menu
// (#3274): asking again with the menu up, after widening the selection,
// replaces the menu with the answer for the new range.
static void RequestingCodeActionsAgainReplacesTheOpenMenu() {
    InputState s;
    s.kind = InputKind::Editor;
    InputAddCodeActionProvider(&s, &RangeActions, nullptr, &PerformRange);
    Type(&s, "value");
    Act(&s, InputAction::SelectLeft);
    Act(&s, InputAction::ToggleCodeActions);
    utassert(s.codeActions.open && s.codeActions.items.len == 1);
    utassert(StrEq(s.codeActions.items[0].title, StrL("4..5")));
    // Widen the selection with the menu still open and ask again.
    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::SelectLeft, false));
    utassert(s.codeActions.open);
    Act(&s, InputAction::ToggleCodeActions);
    gPerformedTitle[0] = 0;
    utassert(InputPerform(&s, nullptr, nullptr, InputAction::Enter, false));
    utassert(StrEq(Str(gPerformedTitle), StrL("3..5")));
    utassert(ValueIs(s, "value"));
}

// A shaped run with proportional advances, wrapped before byte 9: what
// TextLayoutRangeRects answers for it. A letter is 7 wide, a space 3.5, the
// three bytes of one CJK glyph 14 and the tab 28; each row is 20 tall.
static float FakeAdvance(Str s, int i) {
    unsigned char c = (unsigned char)s.s[i];
    if (c == ' ') {
        return 3.5f;
    }
    if (c == '\t') {
        return 28.f;
    }
    if (c >= 0x80) {
        return (c & 0xC0) == 0xC0 ? 14.f : 0.f;
    }
    return 7.f;
}

static int FakeWhitespaceRects(void* ud, int lo, int hi, Bounds* out, int max) {
    Str s = *(Str*)ud;
    const int kWrapAt = 9;
    if (max < 1 || lo >= len(s)) {
        return 0;
    }
    int rowStart = lo >= kWrapAt ? kWrapAt : 0;
    float x = 0;
    for (int i = rowStart; i < lo; i++) {
        x += FakeAdvance(s, i);
    }
    float w = 0;
    for (int i = lo; i < hi; i++) {
        w += FakeAdvance(s, i);
    }
    // DirectWrite's hit test answers the glyph run's top, a little below
    // the line box's.
    out[0] = Bounds{x, (rowStart ? 20.f : 0.f) + 2.4f, w, 20.f};
    return 1;
}

static void CollectWhitespaceMark(void* ud, const WhitespaceMark* m) {
    auto* v = (Vec<WhitespaceMark>*)((void**)ud)[1];
    VecAppend(*v, *m);
}

static int CollectWhitespaceRects(void* ud, int lo, int hi, Bounds* out,
                                  int max) {
    return FakeWhitespaceRects(((void**)ud)[0], lo, hi, out, max);
}

// show_whitespaces marks each space and tab where the shaped run put it
// (LineLayout::with_whitespaces): a space's dot centred in the space's own
// advance, a tab's arrow at the tab's start, a wrapped character on its own
// row. The editor used to lay the marks on a 0.6em column grid, which put
// them past the line end or under a glyph as soon as an advance differed.
static void WhitespaceMarksFollowTheShapedGlyphs() {
    Str text = StrL("ab \xE4\xB8\xAD x\ty z");
    Vec<WhitespaceMark> marks;
    void* ud[2] = {&text, &marks};
    int n = WhitespaceMarksVisit(text, 2.f, CollectWhitespaceRects,
                                 CollectWhitespaceMark, ud);
    utassert(n == 4);
    utassert(marks.len == 4);
    if (marks.len == 4) {
        utassert(marks[0].off == 2 && !marks[0].tab);
        utassertnear(marks[0].x, 14.f + 1.75f - 1.f);
        // The line box's top, not the glyph run's: the mark is centred in
        // the box, and from the run's top it sat on the baseline.
        utassertnear(marks[0].y, 0.f);
        // After the wide glyph, not one grid column after the one before.
        utassert(marks[1].off == 6 && !marks[1].tab);
        utassertnear(marks[1].x, 31.5f + 1.75f - 1.f);
        utassert(marks[2].off == 8 && marks[2].tab);
        utassertnear(marks[2].x, 42.f);
        // The wrapped line's space, on the second row from its own start.
        utassert(marks[3].off == 10 && !marks[3].tab);
        utassertnear(marks[3].x, 7.f + 1.75f - 1.f);
        utassertnear(marks[3].y, 20.f);
        utassertnear(marks[3].h, 20.f);
    }
}

// layout_indent_guides: a guide every tab_size columns from column 0, at
// indent_width * offset / tab_size, where indent_width is the shaped width
// of tab_size spaces. With a font whose space is 7.5 wide, not 0.6em.
static void IndentGuidesStandAtTheMeasuredIndentWidth() {
    float xs[8] = {};
    utassert(IndentGuideXs(30.f, 8, 4, xs, 8) == 2);
    utassertnear(xs[0], 0.f);
    utassertnear(xs[1], 30.f);
    // A partial stop still starts one: offsets 0 and 4 of six columns.
    utassert(IndentGuideXs(30.f, 6, 4, xs, 8) == 2);
    utassert(IndentGuideXs(30.f, 3, 4, xs, 8) == 1);
    utassert(IndentGuideXs(30.f, 0, 4, xs, 8) == 0);
}

static void CollectGuideCounts(El* e, Vec<int>* out) {
    for (; e; e = e->next) {
        if (e->kind == ElKind::Text && e->indentGuideColor.a != 0) {
            VecAppend(*out, (int)e->indentGuideCount);
        }
        CollectGuideCounts(e->first, out);
    }
}

// Each row's own run carries its guides, counted the way indent_count
// counts (a tab is a whole stop), and an empty line carries the guides of
// the line above it — Rust's last_indents.
static void EachRowCarriesItsIndentGuides() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    InputState state;
    state.kind = InputKind::Textarea;
    InputSetValue(&state, StrL("a\n    b\n\n\tc\n  d"));
    InputEditorStyle style;
    style.indentGuide = Rgba{10, 20, 30, 255};
    style.indentWidth = 4;
    Vec<int> counts;
    CollectGuideCounts(Textarea::New(&cx, &state, style), &counts);
    utassert(counts.len == 4);
    if (counts.len == 4) {
        utassert(counts[0] == 4);
        utassert(counts[1] == 4);
        utassert(counts[2] == 4);
        utassert(counts[3] == 2);
    }
    VecReset(counts);
    ArenaDelete(arena);
    delete win;
}

void TestInputState() {
    TestSuite("input_state");
    WhitespaceMarksFollowTheShapedGlyphs();
    IndentGuidesStandAtTheMeasuredIndentWidth();
    EachRowCarriesItsIndentGuides();
    PausingACursorThatIsNotBlinkingDoesNotStartIt();
    SetValueOnUnfocusedInputStaysQuiet();
    BlurringAPausedCursorLeavesTheNextFocusBlinking();
    StoppingAPausedCursorEndsTheBlinkLoop();
    StoppingABlinkingCursorEndsTheBlinkLoop();
    InlineTokenContentValidatesRanges();
    InlineTokensAreAtomicForCaretAndHistory();
    InlineTokensRespectModeAndContent();
    TextareaTokenGapsBreakAtUtf8Characters();
    AnAltClickAddsACursorAndTypingWritesAtEach();
    DeletesAtEveryCursorAreOneUndoStep();
    TypingAtEveryCursorUndoesToEveryCursor();
    EscapeCollapsesTheExtraCursorsFirst();
    AddCursorBelowKeepsTheColumn();
    MovementFansOutOverEveryCursor();
    AColumnarSelectionIsOneSelectionPerRow();
    AShortRowDoesNotNarrowAColumnarSelection();
    ACrLfIsOneCursorBoundary();
    AColumnarSelectionFollowsTheWrappedRows();
    IndentMovesEveryCursorsLine();
    RangesAreReplacedHighestFirst();
    LanguagePairsAndSmartIndent();
    IndentationPatternsMatchPythonRules();
    GeneratedPairsAreTrackedThroughEditsAndHistory();
    TheThreeInputBuildersInstallPasteInterception();
    SingleLineIsCenteredInATallerFrame();
    PasteWithoutTextLeavesTheSelectionAlone();
    PasteTargetTracksEditsAndSelections();
    UnfoldingAtAPositionOpensExactlyWhatHidesIt();
    SingleLineRemovesNewlines();
    SetValueCaretAtEnd();
    ReplaceAllPreservesUndoHistory();
    SetSelectedRange();
    SetSelectedRangeClipsToUtf8Boundaries();
    AdjacentTypingCoalescesIntoOneUndo();
    CursorMovementSplitsTyping();
    BackwardAndForwardDeletesDoNotCoalesce();
    DirectionalCharacterDeletesCoalesce();
    SelectedReplacementIsAtomic();
    ForwardDeleteRestoresCursor();
    NoopEditPreservesRedo();
    MaskedRedoRestoresActualCursor();
    AMaskedValueStaysInTheField();
    AFocusedFieldGoingTakesItsRegistrationWithIt();
    WordMovement();
    DeleteToWordAndLineBoundaries();
    LineBoundaries();
    PageMovesByTheViewport();
    EveryProviderIsAsked();
    CodeActionCollectionsGrowToTheirAnswers();
    ResetDropsWhatTheLayerHeld();
    AHostCanPresentItsOwnItems();
    AHostCanTakeTheKeys();
    AnInsertIsNotTyping();
    AnEditListIsOneStep();
    ACodeActionCanBeMoreThanOneEdit();
    AnAcceptedItemBringsItsImport();
    CompletionAndActionEditListsGrowPastThirtyTwo();
    CompletionResponsesGrowPastTheOldBuffer();
    TheProviderSaysWhenTheMenuOpens();
    DocumentationIsResolvedOnce();
    TheSuggestionWaitsForTheDebounce();
    ASuggestionThatMissedItsMomentIsDropped();
    TabAcceptsAndEscapeDeclines();
    ALongInlineCompletionSurvivesAcceptance();
    TheDeltaEncodingIsUnpacked();
    ATokenOutsideTheLegendIsSkipped();
    OnlyTheVisibleTokensAreResolved();
    TheWindowIsBinarySearched();
    SemanticTokenResponsesGrowPastTheOldBuffer();
    AHoveredSymbolIsAskedAboutOnce();
    ASecondaryClickFollowsTheDefinition();
    TheActionAsksAboutTheCaretWithoutAHover();
    VerticalSelectionReachesDocumentEdges();
    EditsAndEscapeDropStaleProviderResponses();
    AHandledConfirmClosesTheMenus();
    ACancelledPreeditSeparatesTyping();
    ClosedSearchResyncsMatchesAfterEdits();
    MultiCursorTypingLeavesOneHighlighterUpdate();
    RequestingCodeActionsAgainReplacesTheOpenMenu();
    TheHostSeesTheDocumentFirst();
    DefinitionResponsesGrowPastTheOldBuffer();
    BoundariesStepCharacters();
    SelectionFollowsTheDragDirection();
    SelectWordAndLine();
    DraggingCannotEatIntoTheSelectedWord();
    ReadonlyRejectsUserEditsOnly();
    ACompositionReplacesItselfUntilItCommits();
    ACompositionUndoesAsOneThing();
    AnAbandonedCompositionLeavesNothingBehind();
    ConsecutiveCompositionsUndoSeparately();
    ACommitReplacesWhatWasMarked();
    EnterInsertsANewlineOnlyWhereItShould();
    MaskFormatsWhileTyping();
    TabIndentsOnlyWhereThereIsSomethingToIndent();
    TabIndentsEveryLineOfASelection();
    TheBlockPairMovesTheWholeLine();
    ActionForKey();
    LayoutModeRowsClamp();
    KindDoesNotFollowTheRowCount();
    ScrollToBringsTheCaretIntoView();
    AnEditRevealsAFarOffscreenCaret();
    AVerticalWalkDoesNotFightItself();
    TheOffsetStaysInsideTheContent();
    EmptyBottomHeightMatchesRust();
    CursorSurroundingPaddingMatchesRust();
    CodeEditorSurroundingUsesTheOverride();
    SearchNavigationRevealsTheMatchAfterAManualScroll();
    ASidewaysCaretPullsTheRunAcross();
    TheNumberKeysStepTheField();
    TypingAWordOpensTheMenu();
    TheMenuKeysMoveTheSelectionAndAccept();
    AnAcceptedItemWritesItsInsertText();
    TheCodeActionMenuRewritesWhatIsSelected();
    DocumentColorsAreAskedForAgainAfterAnEdit();
    DocumentColorResponsesUseTheRustLimit();
    TwoFindBarsHaveTwoPrevButtons();
    ReopeningFindSelectsItsQueryWithoutChangingUntouchedFrames();
    ReopeningSearchPanelPreservesThePreviousMatch();
    SetSearchQueryHighlightsWithoutThePanel();
    SearchShortcutPropagatesWhenTheEditorIsNotSearchable();
    TheUiInputFacadeKeepsTheSourceShapes();
    BaseInputCoreKeepsTheSourceModeAndPresentationSeams();
    DecorationsAreIndependentClippedAndTrackEdits();
    LineNumberColumnStaysAtThreeDigitsThenGrowsUpToSeven();
    GeometricCollectionsShareUtf8NormalizationAndEditAffinity();
    VisibleQueryPreservesLayersAndSkipsFoldedSpans();
    IntervalIndexCullsLargeCollectionsEvenWithASpanningRange();
    IntervalIndexMatchesLinearReferenceForOverlapsAndMutations();
    FrameOutlineIsContinuousAcrossDifferentLineWidths();
    FrameOutlineKeepsHorizontalSpaceBetweenTheStrokeAndText();
    FrameOutlineStrokeIsAlignedToPhysicalPixelsAtEachScaleFactor();
    FrameOutlineKeepsItsVerticalEdgesInsideTheContentMask();
    GeometricDecorationsTrackEditsHistoryReplacementAndOwnerLifetime();

    DiagnosticSetOwnsMetadataAndAnswersRanges();
    HighlighterContractsAreDependencyFreeAndFunctional();
    LspFacadesInstallCapabilitiesAndExposeOverlayState();
    SoftWrapBoundariesKeepTheVisualRowAffinity();
    ALongDocumentBuildsOnlyTheVisibleBand();
    TheActiveLineWashCoversTheLeftPadding();
    AClickInAScrolledEditorMapsThroughScrollY();
    AClickInAWrappedScrolledEditorIgnoresStaleWindowY();
    ScrollToCursorUsesDocumentYNotStaleWindowY();
    InputFocusCyclesThroughInputsAndAddons();
}
