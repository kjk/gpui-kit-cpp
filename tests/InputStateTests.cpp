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
    item.nAdditionalEdits = additions.len;
    InputPresentCompletionItems(&completion, 0, {}, &item, 1);
    InputAcceptCompletion(&completion, nullptr, nullptr);
    utassert(InputValue(&completion).len == 41);
    utassert(InputValue(&completion).s[40] == 'z');

    InputState action;
    action.kind = InputKind::Editor;
    CodeActionItem codeAction = {};
    codeAction.title = StrL("many edits");
    codeAction.edits = additions.els;
    codeAction.nEdits = additions.len;
    InputPresentCodeActions(&action, &codeAction, 1);
    InputPerformCodeAction(&action, nullptr, nullptr);
    utassert(InputValue(&action).len == 40);
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
    if (typed.len > 0 && typed.s[0] == ':') {
        return CompletionTrigger::Open;
    }
    if (typed.len > 0 && typed.s[0] == '#') {
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
    utassert(value.len == 701);
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
    InputSetValue(&s, Str(text.els, text.len));
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
    gShownExternal = external && uri.len > 0;
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
    // What it found is kept as the last answer, which is what the action
    // goes by once the modifier has come up.
    utassert(s.hoverDef.lastLocations.len == 1);
    utassert(s.hoverDef.lastRange.start == 0 && s.hoverDef.lastRange.end == 8);
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

static void TheActionGoesByTheLastThingAHoverFound() {
    InputState s;
    s.kind = InputKind::Editor;
    InputSetValue(&s, StrL("Duration and more Duration here"));
    s.definitionProvider = &TestDefinitions;

    InputHoverDefinition(&s, 20);
    // The modifier comes up: the underline goes, the answer is remembered.
    InputClearHoverDefinition(&s);
    utassert(s.hoverDef.locations.len == 0);

    // The caret is nowhere near the symbol, so the action has nothing to do.
    InputSetSelectedRange(&s, nullptr, nullptr, 12, 12);
    InputGoToDefinition(&s, nullptr, nullptr);
    utassert(RangeIs(s, 12, 12));

    // Inside it, it follows.
    InputSetSelectedRange(&s, nullptr, nullptr, 20, 20);
    InputGoToDefinition(&s, nullptr, nullptr);
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

// A selection pushes every line it touches over, from the start of its first
// one, and keeps them selected.
static void TabIndentsEveryLineOfASelection() {
    InputState s;
    s.kind = InputKind::Textarea;
    InputSetValue(&s, StrL("one\ntwo\nthree"));
    // From the middle of "one" into the middle of "two".
    InputSetSelectedRange(&s, nullptr, nullptr, 1, 6);
    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::IndentInline, false));
    utassert(ValueIs(s, "    one\n    two\nthree"));
    utassert(RangeIs(s, 0, 14));

    utassert(
        InputPerform(&s, nullptr, nullptr, InputAction::OutdentInline, false));
    utassert(ValueIs(s, "one\ntwo\nthree"));
    utassert(RangeIs(s, 0, 6));

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
    utassert(RangeIs(s, 0, 14));

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
        if (query.len > item.label.len) {
            continue;
        }
        if (query.len > 0 && !StrEq(Str(item.label.s, query.len), query)) {
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

    // While it is up, backspacing asks again on the shorter word, and back
    // past the word closes it. An accepted menu is down and stays down.
    InputSetValue(&s, Str{});
    TypeChars(&s, "cor");
    utassert(s.completion.items.len == 1);
    InputPerform(&s, nullptr, nullptr, InputAction::Backspace, false);
    utassert(ValueIs(s, "co"));
    utassert(s.completion.open && s.completion.items.len == 3);
    InputPerform(&s, nullptr, nullptr, InputAction::Backspace, false);
    InputPerform(&s, nullptr, nullptr, InputAction::Backspace, false);
    utassert(!s.completion.open);
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

    // The chord again puts it away, which is what a toggle is.
    Act(&s, InputAction::ToggleCodeActions);
    utassert(!s.codeActions.open);

    // Down walks it and enter performs the one it is on.
    Act(&s, InputAction::ToggleCodeActions);
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
    if (text.len < 4) {
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
    utassert(InputSelectedValue(query).len == 0);
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
    utassert(masked.len == 15); // five UTF-8 bullets
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
    memcpy(otpState->value, "42", 2);
    otpState->len = 2;
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

    NativeMenu menu;
    InputDefaultNativeMenu(&state, &menu);
    utassert(menu.items.len >= 6);
    utassert(menu.items[0].disabled);
    utassert(menu.items[menu.items.len - 1].goToDefinition);
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
    for (int i = 1; i < line.len; i++) {
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

void TestInputState() {
    TestSuite("input_state");
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
    TheActionGoesByTheLastThingAHoverFound();
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
    AVerticalWalkDoesNotFightItself();
    TheOffsetStaysInsideTheContent();
    EmptyBottomHeightMatchesRust();
    CursorSurroundingPaddingMatchesRust();
    CodeEditorSurroundingUsesTheOverride();
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
    TheUiInputFacadeKeepsTheSourceShapes();
    BaseInputCoreKeepsTheSourceModeAndPresentationSeams();
    DecorationsAreIndependentClippedAndTrackEdits();
    DiagnosticSetOwnsMetadataAndAnswersRanges();
    HighlighterContractsAreDependencyFreeAndFunctional();
    LspFacadesInstallCapabilitiesAndExposeOverlayState();
    SoftWrapBoundariesKeepTheVisualRowAffinity();
    ALongDocumentBuildsOnlyTheVisibleBand();
    AClickInAScrolledEditorMapsThroughScrollY();
    AClickInAWrappedScrolledEditorIgnoresStaleWindowY();
    ScrollToCursorUsesDocumentYNotStaleWindowY();
}
