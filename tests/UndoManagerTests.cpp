/* Ported from crates/base/src/input/base/undo_manager.rs, mod tests.
 *
 * All four Rust cases are here. Rust's `undo()` clones the change list out;
 * this one hands back the transaction that moved to the other stack, so a
 * case that asserts on `.len()` asserts on `->len` and one that reads a
 * change reads it in place. */

#include "Test.h"

static Change TypingChange(int offset, const char* text) {
    Str s = Str(text);
    int end = offset + s.len;
    Change c = {};
    c.oldRange = SelectionAt(offset);
    c.oldText = StrDup(Str{});
    c.newRange = Selection{offset, end};
    c.newText = StrDup(s);
    c.selBefore = SelectionAt(offset);
    c.selAfter = SelectionAt(end);
    return c;
}

static void AdjacentTypingTransactionsCoalesce() {
    UndoManager m;
    UndoRecordTransaction(&m, TypingChange(0, "a"), EditIntent::Typing);
    UndoRecordTransaction(&m, TypingChange(1, "b"), EditIntent::Typing);

    const UndoTransaction* t = UndoPopUndo(&m);
    utassert(t && t->len == 2);
    utassert(UndoPopUndo(&m) == nullptr);
}

static void ExplicitTransactionCollectsMultipleChanges() {
    UndoManager m;
    UndoBeginTransaction(&m);
    UndoRecordTransaction(&m, TypingChange(0, "a"), EditIntent::Typing);
    UndoRecordTransaction(&m, TypingChange(1, "b"), EditIntent::Typing);
    UndoCommitTransaction(&m);

    // One undo entry that holds both changes; the caller replays them in
    // reverse application order.
    const UndoTransaction* t = UndoPopUndo(&m);
    utassert(t && t->len == 2);
    utassert(t && StrEq(t->changes[0].newText, StrL("a")));
    utassert(t && StrEq(t->changes[1].newText, StrL("b")));
    utassert(UndoPopUndo(&m) == nullptr);
}

static void LimitsTheNumberOfRetainedTransactions() {
    UndoManager m;
    for (int offset = 0; offset < 1100; offset++) {
        UndoRecordTransaction(&m, TypingChange(offset, "a"),
                              EditIntent::Atomic);
    }
    // MAX_UNDO_TRANSACTIONS is 1000; the oldest fall off the front.
    for (int i = 0; i < 1000; i++) {
        utassert(UndoPopUndo(&m) != nullptr);
    }
    utassert(UndoPopUndo(&m) == nullptr);
}

static void SplitsACoalescedTransactionBeforeItGrowsTooLarge() {
    UndoManager m;
    for (int offset = 0; offset < 1100; offset++) {
        UndoRecordTransaction(&m, TypingChange(offset, "a"),
                              EditIntent::Typing);
    }
    // MAX_CHANGES_PER_TRANSACTION is 1000: the run splits at 1000, and the
    // remaining 100 make the second transaction, popped first.
    const UndoTransaction* t = UndoPopUndo(&m);
    utassert(t && t->len == 100);
    t = UndoPopUndo(&m);
    utassert(t && t->len == 1000);
    utassert(UndoPopUndo(&m) == nullptr);
}

// Not a Rust case: a no-op edit records nothing but still breaks the run, so
// the history it already had survives. state.rs's
// `test_undo_manager_noop_edit_breaks_coalescing_without_clearing_history`
// asserts the same thing through a whole InputState.
static void NoopEditBreaksCoalescingWithoutClearingHistory() {
    UndoManager m;
    UndoRecordTransaction(&m, TypingChange(0, "a"), EditIntent::Typing);

    Change noop = {};
    noop.oldRange = SelectionAt(1);
    noop.newRange = SelectionAt(1);
    noop.oldText = StrDup(Str{});
    noop.newText = StrDup(Str{});
    UndoRecordTransaction(&m, noop, EditIntent::Typing);

    UndoRecordTransaction(&m, TypingChange(1, "b"), EditIntent::Typing);

    // The boundary split them, so they are two steps rather than one.
    const UndoTransaction* t = UndoPopUndo(&m);
    utassert(t && t->len == 1);
    t = UndoPopUndo(&m);
    utassert(t && t->len == 1);
    utassert(UndoPopUndo(&m) == nullptr);
}

void TestUndoManager() {
    TestSuite("undo_manager");
    AdjacentTypingTransactionsCoalesce();
    ExplicitTransactionCollectsMultipleChanges();
    LimitsTheNumberOfRetainedTransactions();
    SplitsACoalescedTransactionBeforeItGrowsTooLarge();
    NoopEditBreaksCoalescingWithoutClearingHistory();
}
