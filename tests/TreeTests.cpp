/* Ported from crates/base/src/tree.rs.
 *
 * The four action handlers there are a few lines each over the selection and
 * the selected entry's folder state. This is that table. */

#include "Test.h"

// The chord, resolved in the tree's context, read as what the tree does.
static TreeAction ForChord(const char* spec) {
    TreeInitKeys();
    KeyChord c = {};
    utassert(KeyChordParse(Str(spec), &c));
    uint32_t ctx = KeyContextOf(TreeContext());
    return TreeActionOf(KeymapMatch(c, &ctx, 1).action);
}

static void TheKeyTable() {
    utassert(ForChord("up") == TreeAction::SelectPrev);
    utassert(ForChord("down") == TreeAction::SelectNext);
    utassert(ForChord("left") == TreeAction::Collapse);
    utassert(ForChord("right") == TreeAction::Expand);
    // Confirm has a handler in the tree (on_action_confirm, which toggles the
    // selected folder); enter is what carries it, as it does for the list.
    utassert(ForChord("enter") == TreeAction::Confirm);
    utassert(ForChord("space") == TreeAction::None);
}

static void TheSelectionWraps() {
    utassert(TreeSelectNext(0, 4) == 1);
    utassert(TreeSelectNext(3, 4) == 0);
    utassert(TreeSelectPrev(1, 4) == 0);
    utassert(TreeSelectPrev(0, 4) == 3);
}

static void NoSelectionCountsAsZeroBeforeStepping() {
    // Rust takes selected.unwrap_or(0) and *then* steps, so the two
    // directions land in different places from nothing: Up wraps off 0 to the
    // last entry, Down steps off 0 to the second.
    utassert(TreeSelectPrev(-1, 4) == 3);
    utassert(TreeSelectNext(-1, 4) == 1);
}

static void AnEmptyTreeHasNothingToSelect() {
    utassert(TreeSelectPrev(-1, 0) == -1);
    utassert(TreeSelectNext(-1, 0) == -1);
    // One entry is its own neighbour in both directions.
    utassert(TreeSelectPrev(0, 1) == 0);
    utassert(TreeSelectNext(0, 1) == 0);
}

static void LeftAndRightEachActInOneDirectionOnly() {
    // Left closes what is open; it does not open what is closed.
    utassert(TreeCollapses(true, true));
    utassert(!TreeCollapses(true, false));
    // Right opens what is closed; it does not close what is open.
    utassert(TreeExpands(true, false));
    utassert(!TreeExpands(true, true));
    // Neither touches a leaf.
    utassert(!TreeCollapses(false, true));
    utassert(!TreeExpands(false, false));
}

// A root with two children, the second of which has one of its own.
static void Seed(TreeState* s) {
    int root = TreeAddItem(s, StrL("root"), StrL("root"), -1);
    TreeAddItem(s, StrL("a"), StrL("a"), root);
    int b = TreeAddItem(s, StrL("b"), StrL("b"), root);
    TreeAddItem(s, StrL("b1"), StrL("b1"), b);
    int other = TreeAddItem(s, StrL("other"), StrL("other"), -1);
    (void)other;
    TreeRebuild(s);
}

static void OnlyOpenFoldersPutTheirChildrenOnScreen() {
    TreeState s;
    Seed(&s);
    // Everything starts closed, so the two roots are the whole list.
    utassert(s.entries.len == 2);
    utassert(TreeIndexOf(&s, StrL("root")) == 0);
    utassert(TreeIndexOf(&s, StrL("a")) == -1);
    // An item something else calls its parent is a folder; the rest are not.
    utassert(s.items[0].folder);
    utassert(!s.items[1].folder);
    utassert(s.items[3].depth == 2);

    bool expanded = false;
    utassert(TreeToggleExpandAt(&s, 0, &expanded));
    utassert(expanded);
    // The root's own children come in, but not the ones under the folder
    // that is still closed.
    utassert(s.entries.len == 4);
    utassert(TreeIndexOf(&s, StrL("b")) == 2);
    utassert(TreeIndexOf(&s, StrL("b1")) == -1);
    utassert(TreeIndexOf(&s, StrL("other")) == 3);

    utassert(TreeToggleExpandAt(&s, 2, &expanded));
    utassert(expanded);
    utassert(s.entries.len == 5);
    utassert(TreeIndexOf(&s, StrL("b1")) == 3);

    // Closing the root takes the whole subtree off screen at once.
    utassert(TreeToggleExpandAt(&s, 0, &expanded));
    utassert(!expanded);
    utassert(s.entries.len == 2);
}

static void ALeafDoesNotToggle() {
    TreeState s;
    Seed(&s);
    TreeToggleExpandAt(&s, 0, nullptr);
    // Entry 1 is the leaf `a`.
    utassert(!TreeToggleExpandAt(&s, 1, nullptr));
    // And neither does a row that is not there.
    utassert(!TreeToggleExpandAt(&s, 99, nullptr));
    utassert(s.entries.len == 4);
}

static void RevealOpensEveryFolderAboveIt() {
    TreeState s;
    Seed(&s);
    // `b1` is two folders deep and has no row at all to begin with.
    utassert(TreeIndexOf(&s, StrL("b1")) == -1);
    utassert(TreeRevealItem(&s, StrL("b1")) == 3);
    utassert(s.items[0].expanded);
    utassert(s.items[2].expanded);
    // An id the tree does not hold reveals nothing.
    utassert(TreeRevealItem(&s, StrL("nope")) == -1);
}

static void CollapsingPastTheSelectionPullsItBack() {
    TreeState s;
    Seed(&s);
    TreeRevealItem(&s, StrL("b1"));
    s.selected = 3;
    // The rows the selection pointed at are gone, so it lands on the last
    // one that is left rather than off the end.
    TreeToggleExpandAt(&s, 0, nullptr);
    utassert(s.entries.len == 2);
    utassert(s.selected == 1);
}

static void EntriesExposeItemDepthAndInteractionState() {
    TreeState s;
    Seed(&s);
    TreeToggleExpandAt(&s, 0, nullptr);
    TreeEntry root = TreeEntryAt(&s, 0);
    TreeEntry child = TreeEntryAt(&s, 1);
    utassert(root.item && root.IsRoot() && root.IsFolder());
    utassert(root.IsExpanded() && !root.IsDisabled());
    utassert(child.item && child.depth == 1 && !child.IsRoot());
    utassert(!TreeEntryAt(&s, 99).item);

    TreeEntryState state = {true, false};
    utassert(state.IsSelected());
    utassert(!state.IsRightClicked());
}

static void ReplacingItemsResetsBothInteractionIndices() {
    TreeState s;
    Seed(&s);
    s.selected = 1;
    s.rightClicked = 0;
    TreeItem only;
    only.id = StrL("Cargo.toml");
    only.label = only.id;
    TreeSetItems(&s, nullptr, &only, 1);
    utassert(s.items.len == 1 && s.entries.len == 1);
    utassert(s.selected == -1 && s.rightClicked == -1);
    utassert(base::StrEq(TreeEntryAt(&s, 0).item->id, StrL("Cargo.toml")));
}

static void TheStateOwnsTheStringsItIsGiven() {
    // LoadDir used to StrDup into TreeAddItem, which stored the pointers
    // as handed in; nothing freed them. The state now copies, so a stack
    // buffer is enough and overwriting it must not change the row.
    TreeState s;
    TempStr id = StrDupTemp(StrL("root"));
    TempStr label = StrDupTemp(StrL("Root"));
    utassert(TreeAddItem(&s, id, label, -1) == 0);
    id.s[0] = 'X';
    label.s[0] = 'Y';
    utassert(base::StrEq(s.items[0].id, StrL("root")));
    utassert(base::StrEq(s.items[0].label, StrL("Root")));

    TreeItem only = {};
    only.id = StrL("Cargo.toml");
    only.label = only.id;
    TreeSetItems(&s, nullptr, &only, 1);
    utassert(s.items.len == 1);
    utassert(base::StrEq(s.items[0].id, StrL("Cargo.toml")));
    utassert(base::StrEq(s.items[0].label, StrL("Cargo.toml")));
}

static void SelectingHiddenItemExpandsItsAncestors() {
    TreeState s;
    Seed(&s);
    utassert(TreeIndexOf(&s, StrL("b1")) == -1);
    TreeSetSelectedItem(&s, nullptr, StrL("b1"));
    utassert(s.selected == 3);
    utassert(base::StrEq(TreeEntryItem(&s, s.selected)->id, StrL("b1")));
    utassert(s.items[0].expanded && s.items[2].expanded);
    TreeSetSelectedItem(&s, nullptr, {});
    utassert(s.selected == -1);
}

// revealing_item_under_later_root_keeps_other_subtrees (#3265): revealing an
// item under the second root opens only its ancestors, and the first root's
// open subtree keeps its rows and its own closed folder.
static void RevealingUnderALaterRootKeepsOtherSubtrees() {
    TreeState s;
    int docs = TreeAddItem(&s, StrL("docs"), StrL("docs"), -1);
    int guide = TreeAddItem(&s, StrL("docs/guide"), StrL("guide"), docs);
    TreeAddItem(&s, StrL("docs/guide/a"), StrL("a"), guide);
    int src = TreeAddItem(&s, StrL("src"), StrL("src"), -1);
    int ui = TreeAddItem(&s, StrL("src/ui"), StrL("ui"), src);
    TreeAddItem(&s, StrL("src/ui/tree.rs"), StrL("tree.rs"), ui);
    s.items[docs].expanded = true;
    TreeRebuild(&s);
    utassert(s.entries.len == 3);

    utassert(TreeRevealItem(&s, StrL("src/ui/tree.rs")) == 4);
    const char* ids[] = {"docs", "docs/guide", "src", "src/ui",
                         "src/ui/tree.rs"};
    const int depths[] = {0, 1, 0, 1, 2};
    utassert(s.entries.len == 5);
    for (int i = 0; i < 5 && i < s.entries.len; i++) {
        TreeEntry e = TreeEntryAt(&s, i);
        utassert(e.item && StrEq(e.item->id, Str(ids[i])));
        utassert(e.depth == depths[i]);
    }
    utassert(TreeEntryAt(&s, 0).IsFolder());
    utassert(!s.items[guide].expanded);

    utassert(TreeToggleExpandAt(&s, 1, nullptr));
    utassert(TreeIndexOf(&s, StrL("docs/guide/a")) == 2);
}

static El* PlainTreeRow(void*, Ctx* cx, int, const TreeEntry&, TreeEntryState) {
    return Div(cx->a)->W(kFill)->H(24);
}

// uniform_list virtualizes at prepaint from the bounds layout gave it. The
// tree builds its rows before layout, so it builds them for the height its
// box was laid out at last frame: a tree with no height fills its box and
// builds with 320 until it has been laid out, then with the box less its
// padding; a fixed height is the box's and the first frame's.
static void ATreeWithoutAHeightFillsItsBox() {
    App app;
    Window win;
    win.app = &app;
    Arena* a = ArenaNew();
    Ctx cx{&app, &win, a, {}};
    Entity<TreeState> state = EntityNewState<TreeState>(&app);
    TreeState* s = state.Get(&app);
    for (int i = 0; i < 100; i++) {
        char id[16];
        snprintf(id, sizeof(id), "n%d", i);
        TreeAddItem(s, Str(id), Str(id), -1);
    }
    TreeRebuild(s);

    PaintCtx paint = {};
    auto frame = [&](Str name, float fixed) {
        a->Reset();
        El* box = TreeList::New(&cx, name, state, fixed, &PlainTreeRow, nullptr)
                      ->Pad(4);
        El* col = Div(a)->FlexCol()->W(200)->H(400)->Child(box);
        LayoutEl(nullptr, col, 0, 0, 200, 400, 14, Rgba{});
        if (box->prePaint) {
            box->prePaint(&paint, box, box->customUser);
        }
        return box;
    };

    El* box = frame(StrL("fill"), 0);
    utassertnear(s->viewportH, 320.f);
    utassertnear(box->h, 400.f);
    frame(StrL("fill"), 0);
    utassertnear(s->viewportH, 400.f - 8.f);

    box = frame(StrL("fixed"), 200);
    utassertnear(s->viewportH, 200.f);
    utassertnear(box->h, 200.f);
    frame(StrL("fixed"), 200);
    utassertnear(s->viewportH, 200.f - 8.f);

    // Without a window there is no last frame to read: the tree builds with
    // the number it has.
    Ctx bare{&app, nullptr, a, {}};
    a->Reset();
    TreeList::New(&bare, StrL("bare"), state, 0, &PlainTreeRow, nullptr);
    utassertnear(s->viewportH, 320.f);

    ArenaDelete(a);
    EntityDropAll(&app);
}

void TestTree() {
    TestSuite("tree");
    TheKeyTable();
    TheSelectionWraps();
    NoSelectionCountsAsZeroBeforeStepping();
    AnEmptyTreeHasNothingToSelect();
    LeftAndRightEachActInOneDirectionOnly();
    OnlyOpenFoldersPutTheirChildrenOnScreen();
    ALeafDoesNotToggle();
    RevealOpensEveryFolderAboveIt();
    RevealingUnderALaterRootKeepsOtherSubtrees();
    CollapsingPastTheSelectionPullsItBack();
    EntriesExposeItemDepthAndInteractionState();
    ReplacingItemsResetsBothInteractionIndices();
    TheStateOwnsTheStringsItIsGiven();
    SelectingHiddenItemExpandsItsAncestors();
    ATreeWithoutAHeightFillsItsBox();
}
