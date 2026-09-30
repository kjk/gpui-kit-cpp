/* Ported from crates/ui/src/dock/tab_panel.rs.
 *
 * `split_placement_at` picks one of five zones from a position inside a tab
 * panel, and `DropPlaceholderBounds::for_placement` turns that answer into the
 * half of the panel a drop would take —
 * drop_placeholder_bounds_cover_each_target_placement is the Rust test for it.
 * The rest is what the tree does when a drop lands: a merge, a split, and the
 * pruning a group left empty triggers (remove_self_if_empty). */

#include "Test.h"

// What `window.subscribe(&area, ..)` is here: an entity the state's one event
// listener reports LayoutChanged to.
struct DockEventCounter {
    int layoutChanges = 0;

    static void OnEvent(DockEventCounter* self, Ctx*, const DockEvent* ev) {
        if (ev && ev->kind == DockEventKind::LayoutChanged) {
            self->layoutChanges++;
        }
    }
};

static void TheFiveDropZones() {
    Bounds b = {0, 0, 200, 100};
    // Left of 35%, right of 65%, then the same two thresholds vertically —
    // the horizontal ones are asked first, as in Rust.
    utassert(DockDropAt(b, 10, 50) == DockDrop::Left);
    utassert(DockDropAt(b, 190, 50) == DockDrop::Right);
    utassert(DockDropAt(b, 100, 10) == DockDrop::Top);
    utassert(DockDropAt(b, 100, 90) == DockDrop::Bottom);
    utassert(DockDropAt(b, 100, 50) == DockDrop::Center);
    // A corner is an edge, and the horizontal answer wins it.
    utassert(DockDropAt(b, 10, 10) == DockDrop::Left);
    // The bounds are not assumed to start at the origin.
    Bounds off = {100, 200, 200, 100};
    utassert(DockDropAt(off, 110, 250) == DockDrop::Left);
    utassert(DockDropAt(off, 200, 250) == DockDrop::Center);
}

static void ThePlaceholderCoversEachZone() {
    Bounds b = {0, 0, 200, 100};
    Bounds left = DockDropPlaceholder(b, DockDrop::Left);
    utassert(left.x == 0 && left.y == 0 && left.w == 100 && left.h == 100);
    Bounds right = DockDropPlaceholder(b, DockDrop::Right);
    utassert(right.x == 100 && right.y == 0 && right.w == 100 &&
             right.h == 100);
    Bounds top = DockDropPlaceholder(b, DockDrop::Top);
    utassert(top.x == 0 && top.y == 0 && top.w == 200 && top.h == 50);
    Bounds bottom = DockDropPlaceholder(b, DockDrop::Bottom);
    utassert(bottom.x == 0 && bottom.y == 50 && bottom.w == 200 &&
             bottom.h == 50);
    // A merge covers the whole panel, which is what says "no split".
    Bounds centre = DockDropPlaceholder(b, DockDrop::Center);
    utassert(centre.x == 0 && centre.y == 0 && centre.w == 200 &&
             centre.h == 100);
}

// Two tab groups side by side in one split, with two panels in the first.
static void Seed(DockState* s, int* a, int* b) {
    for (int i = 0; i < 3; i++) {
        DockPanelDef def;
        def.title = StrL("panel");
        DockAddPanelDef(s, def);
    }
    *a = DockNewTabs(s);
    DockTabsAdd(s, *a, 0);
    DockTabsAdd(s, *a, 1);
    *b = DockNewTabs(s);
    DockTabsAdd(s, *b, 2);
    int split = DockNewSplit(s, Axis::Horizontal);
    DockSplitAdd(s, split, *a, 300);
    DockSplitAdd(s, split, *b, 300);
    s->center = split;
    s->nodes[*a].bounds = {0, 0, 300, 400};
    s->nodes[*b].bounds = {300, 0, 300, 400};
}

static void ADropInTheMiddleMerges() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    utassert(DockMovePanelTo(&s, 1, b, DockDrop::Center));
    utassert(s.nodes[a].panel.len == 1);
    utassert(s.nodes[b].panel.len == 2);
    // The panel that arrived is the one showing.
    utassert(s.nodes[b].activeIx == 1);
    utassert(DockNodeOfPanel(&s, 1) == b);
    // Moving a panel onto the group it is already in changes nothing.
    utassert(!DockMovePanelTo(&s, 1, b, DockDrop::Center));
}

static void ADropOnAnEdgeSplits() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    int split = s.center;
    utassert(DockMovePanelTo(&s, 0, b, DockDrop::Right));
    // The split already ran this way, so the panel joined it rather than
    // nesting a second split inside it.
    utassert(s.center == split);
    utassert(s.nodes[split].child.len == 3);
    int fresh = s.nodes[split].child[2];
    utassert(s.nodes[fresh].panel.len == 1 && s.nodes[fresh].panel[0] == 0);
    // The two share what the target had.
    utassert(s.nodes[split].size[1] == 150);
    utassert(s.nodes[split].size[2] == 150);

    // Across the axis it does nest: a vertical drop inside a horizontal split
    // puts a new split where the target was.
    DockState s2;
    Seed(&s2, &a, &b);
    utassert(DockMovePanelTo(&s2, 0, b, DockDrop::Bottom));
    int outer = s2.center;
    utassert(s2.nodes[outer].child.len == 2);
    int nested = s2.nodes[outer].child[1];
    utassert(s2.nodes[nested].split);
    utassert(!AxisIsHorizontal(s2.nodes[nested].axis));
    // Bottom means after the target.
    utassert(s2.nodes[nested].child[0] == b);
    utassert(s2.nodes[s2.nodes[nested].child[1]].panel[0] == 0);
}

static void AnEmptyGroupLeavesTheSplit() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    int split = s.center;
    // Take both of the first group's panels away and the group goes with the
    // second one, then the split with one child left becomes that child.
    utassert(DockClosePanelAt(&s, a, 0));
    utassert(s.nodes[a].used);
    utassert(s.nodes[a].panel.len == 1);
    utassert(DockClosePanelAt(&s, a, 0));
    utassert(!s.nodes[a].used);
    utassert(!s.nodes[split].used);
    utassert(s.center == b);
    utassert(s.nodes[b].parent == -1);
}

static void ARootGroupStays() {
    DockState s;
    DockPanelDef def;
    def.title = StrL("panel");
    DockAddPanelDef(&s, def);
    int node = DockNewTabs(&s);
    DockTabsAdd(&s, node, 0);
    s.left.node = node;
    // The Dock on a side keeps its empty tab group — Rust says so by making
    // that TabPanel `closable = false`.
    utassert(DockClosePanelAt(&s, node, 0));
    utassert(s.nodes[node].used);
    utassert(s.nodes[node].panel.len == 0);
    utassert(s.left.node == node);
}

// insert_panel_at: a drop that landed on a tab takes that tab's place in the
// row, which is what reorders a group's own tabs.
static void ADropOnATabTakesItsPlace() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    // The second tab of A dropped on the first: it goes in front of it.
    utassert(DockMovePanelTo(&s, 1, a, DockDrop::Center, 0));
    utassert(s.nodes[a].panel.len == 2);
    utassert(s.nodes[a].panel[0] == 1);
    utassert(s.nodes[a].panel[1] == 0);
    // insert_panel_at ends with set_active_ix: what was dropped is showing.
    utassert(s.nodes[a].activeIx == 0);

    // A panel from another group inserted at a place in this one.
    utassert(DockMovePanelTo(&s, 2, a, DockDrop::Center, 1));
    utassert(s.nodes[a].panel.len == 3);
    utassert(s.nodes[a].panel[1] == 2);
    utassert(DockNodeOfPanel(&s, 2) == a);
    // Its old group emptied and left the split, so the split is gone with it.
    utassert(!s.nodes[b].used);
}

// The index is worked out before the panel is detached, which is Rust's
// order: a tab dragged rightwards inside its own row lands one place short of
// where it was let go.
static void AReorderCountsFromBeforeTheDetach() {
    DockState s;
    for (int i = 0; i < 4; i++) {
        DockPanelDef def;
        def.title = StrL("panel");
        DockAddPanelDef(&s, def);
    }
    int node = DockNewTabs(&s);
    for (int i = 0; i < 4; i++) {
        DockTabsAdd(&s, node, i);
    }
    s.center = node;
    // The first tab dropped on the third: 0 comes out, and 2 is where 3 was.
    utassert(DockMovePanelTo(&s, 0, node, DockDrop::Center, 2));
    utassert(s.nodes[node].panel[0] == 1);
    utassert(s.nodes[node].panel[1] == 2);
    utassert(s.nodes[node].panel[2] == 0);
    utassert(s.nodes[node].panel[3] == 3);
}

// The two same-group rules: a drop with no tab named is nothing, and a group
// of one dropped on its own edge is the layout it already has.
static void ADropOnItsOwnGroupNeedsATabOrAnEdge() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    utassert(!DockMovePanelTo(&s, 0, a, DockDrop::Center));
    // B holds one panel, so splitting it with itself is refused.
    utassert(!DockMovePanelTo(&s, 2, b, DockDrop::Right));
    // A holds two, so one of them can split it.
    utassert(DockMovePanelTo(&s, 1, a, DockDrop::Right));
}

// Which Dock a node belongs to, which is what a click on a collapsed one has
// to know before it can open it again.
static void ANodeKnowsWhichDockItIsIn() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    int side = DockNewTabs(&s);
    s.bottom.node = side;
    utassert(DockPlacementOfNode(&s, a) == DockPlacement::Center);
    utassert(DockPlacementOfNode(&s, side) == DockPlacement::Bottom);
    // A group inside a split inside a Dock is still in that Dock.
    int inner = DockNewTabs(&s);
    int split = DockNewSplit(&s, Axis::Vertical);
    DockSplitAdd(&s, split, inner, 100);
    s.left.node = split;
    utassert(DockPlacementOfNode(&s, inner) == DockPlacement::Left);
}

// ScrollHandle::scroll_to_item: a tab off either end of the bar is brought
// just inside it, and one already inside asks for nothing.
static void ATabOffTheEndIsBroughtIntoView() {
    Bounds strip = {100, 0, 200, 30};
    // Already inside.
    utassertnear(DockTabScrollTo(40, strip, Bounds{120, 0, 60, 30}), 40.f);
    // Off the right edge by 50: the offset grows by exactly that.
    utassertnear(DockTabScrollTo(40, strip, Bounds{260, 0, 90, 30}), 90.f);
    // Off the left edge by 30: the offset shrinks by exactly that.
    utassertnear(DockTabScrollTo(40, strip, Bounds{70, 0, 60, 30}), 10.f);
    // Nothing measured yet is nothing to scroll to.
    utassertnear(DockTabScrollTo(40, strip, Bounds{}), 40.f);
}

// DockArea::dump and load: the tree written out and built back, with the
// panels matched by the name they were registered under.
static void ALayoutSurvivesDumpAndLoad() {
    Arena* arena = ArenaNew();
    DockState s;
    static const char* kNames[] = {"AlphaPanel", "BetaPanel", "GammaPanel"};
    for (int i = 0; i < 3; i++) {
        DockPanelDef def;
        def.name = Str(kNames[i]);
        def.title = Str(kNames[i]);
        DockAddPanelDef(&s, def);
    }
    int tabs = DockNewTabs(&s);
    DockTabsAdd(&s, tabs, 0);
    DockTabsAdd(&s, tabs, 1);
    s.nodes[tabs].activeIx = 1;
    int other = DockNewTabs(&s);
    DockTabsAdd(&s, other, 2);
    int split = DockNewSplit(&s, Axis::Vertical);
    DockSplitAdd(&s, split, tabs, 240);
    DockSplitAdd(&s, split, other, 160);
    s.center = split;
    int sideNode = DockNewTabs(&s);
    DockTabsAdd(&s, sideNode, 2);
    s.left.node = sideNode;
    s.left.size = 210;
    s.left.open = false;

    DockAreaState state;
    DockDump(&s, &state);
    // The tree is PanelStates: a split is a StackPanel, a group a TabPanel,
    // and a panel a leaf under its registered name.
    utassert(StrEqI(state.nodes[state.center].panelName, "StackPanel"));
    utassert(state.nodes[state.center].kind == PanelInfoKind::Stack);
    utassertnear(state.nodes[state.center].sizes[0], 240.f);
    utassert(!AxisIsHorizontal(state.nodes[state.center].axis));
    const PanelStateNode& first =
        state.nodes[state.nodes[state.center].children[0]];
    utassert(first.kind == PanelInfoKind::Tabs);
    utassert(first.activeIndex == 1);
    utassert(StrEqI(state.nodes[first.children[0]].panelName, "AlphaPanel"));
    utassert(state.left.present && !state.left.open);
    utassertnear(state.left.size, 210.f);

    // Loaded back into a dock that knows the same panels: the same tree.
    DockState back;
    for (int i = 0; i < 3; i++) {
        DockPanelDef def;
        def.name = Str(kNames[i]);
        def.title = Str(kNames[i]);
        DockAddPanelDef(&back, def);
    }
    utassert(DockLoad(&back, &state, arena));
    utassert(back.nodes[back.center].split);
    utassert(back.nodes[back.center].child.len == 2);
    utassertnear(back.nodes[back.center].size[0], 240.f);
    int loadedTabs = back.nodes[back.center].child[0];
    utassert(back.nodes[loadedTabs].panel.len == 2);
    utassert(back.nodes[loadedTabs].panel[0] == 0);
    utassert(back.nodes[loadedTabs].activeIx == 1);
    utassert(back.left.node >= 0 && !back.left.open);
    utassertnear(back.left.size, 210.f);
    // Nothing new was registered: every name was one it already had.
    utassert(back.panels.len == 3);
    ArenaDelete(arena);
}

// PanelRegistry::build_panel answering with an InvalidPanel: the layout keeps
// its shape, and dumping it again writes the name it could not build.
static void APanelNothingAnswersToBecomesInvalid() {
    Arena* arena = ArenaNew();
    DockAreaState state;
    int tabs = state.NewNode(StrL("TabPanel"));
    state.nodes[tabs].kind = PanelInfoKind::Tabs;
    int known = state.NewNode(StrL("AlphaPanel"));
    int missing = state.NewNode(StrL("GitGraphPanel"));
    VecAppend(state.nodes[tabs].children, known);
    VecAppend(state.nodes[tabs].children, missing);
    state.nodes[tabs].activeIndex = 1;
    state.center = tabs;

    DockState s;
    DockPanelDef def;
    def.name = StrL("AlphaPanel");
    def.title = StrL("Alpha");
    DockAddPanelDef(&s, def);
    utassert(DockLoad(&s, &state, arena));
    // The missing one was registered on the spot, under the name asked for.
    utassert(s.panels.len == 2);
    utassert(StrEqI(s.panels[1].name, "GitGraphPanel"));
    utassert(s.nodes[s.center].panel.len == 2);
    utassert(s.nodes[s.center].activeIx == 1);

    // Written out again, the layout still names it — Rust's InvalidPanel
    // dumps the state it came from rather than losing the panel.
    DockAreaState again;
    DockDump(&s, &again);
    const PanelStateNode& group = again.nodes[again.center];
    utassert(StrEqI(again.nodes[group.children[1]].panelName, "GitGraphPanel"));
    ArenaDelete(arena);
}

static void ALockedDockMovesNothing() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    s.locked = true;
    utassert(!DockMovePanelTo(&s, 1, b, DockDrop::Center));
    utassert(s.nodes[a].panel.len == 2);
}

// Panel::visible: a hidden panel has no tab, does not count towards the one
// panel a group may not be emptied past, and is not the active panel even
// when it sits at the active index.
static void AHiddenPanelIsNotThere() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    utassert(DockVisibleCount(&s, a) == 2);
    utassert(DockActiveIx(&s, a) == 0);
    s.panels[s.nodes[a].panel[0]].visible = false;
    utassert(DockVisibleCount(&s, a) == 1);
    // `active_panel`: the first visible one stands in for the hidden one.
    utassert(DockActiveIx(&s, a) == 1);
}

// TabPanel::is_locked / is_last_panel: the last visible panel of a dock is
// neither draggable nor closable, and a group with no split above it is a
// root that cannot be taken apart at all.
static void TheLastPanelStays() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    // Two panels in `a`, and a split above it holding two children.
    utassert(!DockIsLastPanel(&s, a));
    utassert(DockNodeDraggable(&s, a));
    // `b` holds one panel, but its split holds two children, so it is not
    // the last panel — Rust walks the parent chain before it counts.
    utassert(!DockIsLastPanel(&s, b));

    // A lone group with nothing above it is both last and locked.
    DockState solo;
    DockPanelDef def;
    def.title = StrL("only");
    DockAddPanelDef(&solo, def);
    int only = DockNewTabs(&solo);
    DockTabsAdd(&solo, only, 0);
    solo.center = only;
    utassert(DockIsLastPanel(&solo, only));
    utassert(DockNodeLocked(&solo, only));
    utassert(!DockNodeDraggable(&solo, only));
    utassert(!DockNodeDroppable(&solo, only));
}

// StackPanel::left_top_tab_panel / right_top_tab_panel: which group carries
// each dock toggle. The right one follows the split's axis — the last child
// across, the first one down.
static void TheTogglesPickTheirGroup() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    utassert(DockLeftTopTabs(&s, s.center) == a);
    utassert(DockRightTopTabs(&s, s.center) == b);
    // Down the page instead: the top child answers for both.
    s.nodes[s.center].axis = Axis::Vertical;
    utassert(DockLeftTopTabs(&s, s.center) == a);
    utassert(DockRightTopTabs(&s, s.center) == a);
}

// Dock::set_collapsible: a dock that may not be collapsed is opened by the
// same call, so one shut beforehand cannot be left unreachable.
static void ANonCollapsibleDockOpens() {
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    s.left.node = a;
    s.left.open = false;
    DockSetCollapsible(&s, DockPlacement::Left, false);
    utassert(s.left.open);
    utassert(!s.left.collapsible);
}

// normalize.rs. The shapes an edit never makes but a saved file may hold.
static void NormalizeCollapsesWhatAnEditNeverMakes() {
    DockState s;
    for (int i = 0; i < 3; i++) {
        DockPanelDef def;
        def.title = StrL("panel");
        DockAddPanelDef(&s, def);
    }
    int a = DockNewTabs(&s);
    DockTabsAdd(&s, a, 0);
    int b = DockNewTabs(&s);
    DockTabsAdd(&s, b, 1);
    int empty = DockNewTabs(&s);
    // An inner split along the same axis as the one holding it, a split with
    // one child, and a group with nothing in it.
    int inner = DockNewSplit(&s, Axis::Horizontal);
    DockSplitAdd(&s, inner, a, 100);
    DockSplitAdd(&s, inner, b, 300);
    int single = DockNewSplit(&s, Axis::Vertical);
    int c = DockNewTabs(&s);
    DockTabsAdd(&s, c, 2);
    DockSplitAdd(&s, single, c, 200);
    int root = DockNewSplit(&s, Axis::Horizontal);
    DockSplitAdd(&s, root, inner, 400);
    DockSplitAdd(&s, root, single, 200);
    DockSplitAdd(&s, root, empty, 100);
    s.center = root;

    DockNormalize(&s);

    // The same-axis split was spliced in, the single-child split is its
    // child, and the empty group is gone: three children, all tab groups.
    utassert(s.center == root);
    utassert(s.nodes[root].child.len == 3);
    for (int i = 0; i < s.nodes[root].child.len; i++) {
        utassert(!s.nodes[s.nodes[root].child[i]].split);
        utassert(s.nodes[s.nodes[root].child[i]].parent == root);
    }
    utassert(!s.nodes[inner].used);
    utassert(!s.nodes[single].used);
    utassert(!s.nodes[empty].used);
    // distribute_slot: the two spliced children share the 400 the split they
    // were in had, in the proportions they had inside it.
    utassert(s.nodes[root].size[0] == 100.f);
    utassert(s.nodes[root].size[1] == 300.f);
    // And the single-child split's slot went to the child that replaced it —
    // plus the empty group's 100, since taking a node out of a split here
    // hands its space to a neighbour rather than shrinking the split, which
    // is what the edits have always done.
    utassert(s.nodes[root].size[2] == 300.f);

    // Idempotent: normalize(normalize(t)) == normalize(t).
    int before = s.nodes[root].child.len;
    float first = s.nodes[root].size[0];
    DockNormalize(&s);
    utassert(s.nodes[root].child.len == before);
    utassert(s.nodes[root].size[0] == first);
}

static void NormalizeClampsTheActiveTab() {
    DockState s;
    DockPanelDef def;
    def.title = StrL("panel");
    DockAddPanelDef(&s, def);
    int tabs = DockNewTabs(&s);
    DockTabsAdd(&s, tabs, 0);
    s.center = tabs;
    // A saved active index past what the group holds.
    s.nodes[tabs].activeIx = 4;
    DockNormalize(&s);
    utassert(s.nodes[tabs].activeIx == 0);
}

static void NormalizeKeepsARoot() {
    DockState s;
    int root = DockNewSplit(&s, Axis::Horizontal);
    s.center = root;
    // An empty root split and an empty root group both stay: the centre item
    // and the three docks are always there, which is Rust's RootKind::Split.
    DockNormalize(&s);
    utassert(s.center == root && s.nodes[root].used);
    int tabs = DockNewTabs(&s);
    s.left.node = tabs;
    DockNormalize(&s);
    utassert(s.left.node == tabs && s.nodes[tabs].used);
}

// is_node_visible. A container whose panels are all hidden is not there —
// and neither is the slot it had.
static void AHiddenGroupIsNotASlot() {
    DockState s;
    for (int i = 0; i < 3; i++) {
        DockPanelDef def;
        def.title = StrL("panel");
        DockAddPanelDef(&s, def);
    }
    int a = DockNewTabs(&s);
    DockTabsAdd(&s, a, 0);
    int b = DockNewTabs(&s);
    DockTabsAdd(&s, b, 1);
    int c = DockNewTabs(&s);
    DockTabsAdd(&s, c, 2);
    int split = DockNewSplit(&s, Axis::Horizontal);
    DockSplitAdd(&s, split, a, 200);
    DockSplitAdd(&s, split, b, 200);
    DockSplitAdd(&s, split, c, 200);
    s.center = split;
    utassert(DockNodeVisible(&s, split));
    utassert(DockNodeVisible(&s, c));

    // The trailing group's only panel goes: the group has nothing to show,
    // and the split still has.
    s.panels[2].visible = false;
    utassert(!DockNodeVisible(&s, c));
    utassert(DockNodeVisible(&s, split));
    // A split of hidden children is hidden too, which is what keeps the slot
    // one level up from being held open by an empty one.
    s.panels[0].visible = false;
    s.panels[1].visible = false;
    utassert(!DockNodeVisible(&s, split));
}

struct DockPresentationProbe {
    int titles = 0;
};

static El* DockProbeTitle(Ctx* cx, void* data) {
    DockPresentationProbe* probe = (DockPresentationProbe*)data;
    probe->titles++;
    return Div(cx->a)->Id(StrL("probe-title"));
}

// panel.rs and tab_panel.rs: the concrete handle must preserve the complete
// presentation function table, and the themed renderer must honor both the
// custom title and the active panel's inner-padding policy.
static void TheUiPanelHandleCrossesTheBaseSeam() {
    App app;
    Arena* arena = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = arena;

    Entity<DockState> state = EntityNewState<DockState>(&app);
    DockPresentationProbe probe;
    component::PanelView panel;
    panel.name = StrL("Probe");
    panel.title = StrL("fallback");
    panel.titleEl = DockProbeTitle;
    panel.data = &probe;
    panel.innerPadding = false;
    panel.titleBar = false;

    component::PanelHandle handle = component::panel_handle(panel);
    utassert(component::PanelHandle::Of(handle.Get()) == handle.Get());
    utassert(handle.Get()->titleEl == DockProbeTitle);
    utassert(!handle.Get()->innerPadding);
    utassert(!handle.Get()->titleBar);

    DockState* dock = state.Get(&app);
    DockAddPanelDef(dock, handle.IntoPanelView());
    int tabs = DockNewTabs(dock);
    DockTabsAdd(dock, tabs, 0);
    dock->center = tabs;

    component::DockSkin skin = component::DockSkin::New(state);
    DockTabGroup group;
    group.cx = &cx;
    group.state = state;
    group.node = tabs;
    const DockRenderer* renderer = skin.Renderer();
    utassert(renderer->tabBar(&cx, renderer->data, &group) == nullptr);

    DockPanelDef second;
    second.title = StrL("Second");
    DockAddPanelDef(dock, second);
    DockTabsAdd(dock, tabs, 1);
    utassert(renderer->tabBar(&cx, renderer->data, &group) != nullptr);
    utassert(probe.titles == 1);

    skin.SetPanelStyle(&app, nullptr, component::PanelStyle::TabBar);
    skin.SetToggleButtonVisible(&app, nullptr, false);
    utassert(skin.GetPanelStyle(&app) == component::PanelStyle::TabBar);
    utassert(!skin.IsToggleButtonVisible(&app));

    El* content = renderer->tabContentFrame(&cx, renderer->data, &group);
    utassert(content && content->style.pad.top == 0);
    El* preview = renderer->dragPreview(&cx, renderer->data, handle.Get());
    utassert(preview && preview->style.width == kDockDragPreviewW);
    utassert(probe.titles == 2);

    dock->panels[0].innerPadding = true;
    content = renderer->tabContentFrame(&cx, renderer->data, &group);
    utassert(content && content->style.pad.top == 8);

    ArenaDelete(arena);
    EntityDropAll(&app);
}

static PanelId PurePanel(uint64_t value) {
    return PanelId::FromU64(value);
}

// layout::{node,tree,builder,edit,normalize}: the renderer-independent tree
// keeps identity through edits and returns in canonical shape every time.
static void ThePurePaneTreeMatchesTheSourceAlgebra() {
    PaneTree tree(RootKind::Split);
    NodeId root = tree.Root()->Id();
    PanelId one = PurePanel(1);
    PanelId two = PurePanel(2);
    NodeId tabs = tree.AddTabs(root, &one, 1);
    tree.Normalize();
    utassert(tree.FindNode(tabs) != nullptr);
    utassert(tree.FindPanelNode(one, nullptr));
    utassert(tree.IsNormalized());

    float requested = 240;
    EditResult split = tree.Split(tabs, two, Placement::Right, &requested);
    utassert(split.Changed());
    utassert(tree.FindNode(tabs) != nullptr);
    PaneRef top = tree.Root()->Kind();
    utassert(top.kind == PaneKind::Split);
    utassert(top.children && top.children->len == 2);
    utassert(top.sizeKnown && (*top.sizeKnown)[1]);
    utassertnear((*top.sizes)[1], 240);

    NodeId other;
    utassert(tree.FindPanelNode(two, &other));
    utassert(tree.MovePanel(one, InsertTarget::Tabs(other)).Changed());
    utassert(tree.FindNode(tabs) == nullptr);
    utassert(tree.ContainsPanel(one) && tree.ContainsPanel(two));
    utassert(tree.IsNormalized());

    // A background insertion before the active tab keeps that displayed
    // panel displayed by advancing its index.
    PanelId three = PurePanel(3);
    utassert(tree.InsertPanel(three, InsertTarget::Tabs(other, 0, false))
                 .Changed());
    const PaneNode* group = tree.FindNode(other);
    utassert(group && group->activeIx == 2);
    utassert(group->panels[group->activeIx] == one);
    utassert(!tree.SetActive(other, -1).Changed());
    utassert(group->activeIx == 2);

    // A stale or wrong-kind target is a no-op.
    utassert(!tree.SetSizes(other, &requested, nullptr, 1).Changed());
}

static void PurePaneTreeNormalizesSizes() {
    PaneTree tree(RootKind::Split);
    NodeId root = tree.Root()->Id();
    float outer = 400;
    NodeId inner = tree.AddSplit(root, Axis::Horizontal, &outer);
    float first = 100;
    float second = 300;
    PanelId one = PurePanel(11);
    PanelId two = PurePanel(12);
    tree.AddTabs(inner, &one, 1, &first);
    tree.AddTabs(inner, &two, 1, &second);
    tree.Normalize();
    utassert(tree.FindNode(inner) == nullptr);
    utassert(tree.Root()->children.len == 2);
    utassertnear(tree.Root()->sizes[0], 100);
    utassertnear(tree.Root()->sizes[1], 300);

    // Across the parent axis the target is wrapped, and keeps its id.
    NodeId oneNode;
    utassert(tree.FindPanelNode(one, &oneNode));
    PanelId three = PurePanel(13);
    utassert(tree.Split(oneNode, three, Placement::Bottom).Changed());
    utassert(tree.FindNode(oneNode) != nullptr);
    utassert(tree.IsNormalized());
}

static void DockLayoutDescribesWithoutBuildingUi() {
    float slot = 300;
    DockLayout* layout =
        DockLayout::HSplit()
            ->Child(DockLayout::Tabs()->Panel(PurePanel(21)), &slot)
            ->Child(DockLayout::Tabs()
                        ->Panel(PurePanel(22))
                        ->Panel(PurePanel(23))
                        ->ActiveIndex(1));
    PaneTree* tree = PaneTree::FromLayout(layout, RootKind::Split);
    utassert(tree && tree->Root()->paneKind == PaneKind::Split);
    utassert(tree->Root()->children.len == 2);
    utassert(tree->Root()->sizeKnown[0]);
    utassertnear(tree->Root()->sizes[0], 300);
    utassert(tree->Root()->children[1]->activeIx == 1);
    Vec<PanelId> panels;
    tree->Panels(&panels);
    utassert(len(panels) == 3);
    VecReset(panels);
    delete tree;
    delete layout;
}

static DockPanelDef BuildRegisteredPanel(const PanelBuildContext* context,
                                         Window*, App*, void* data) {
    int* calls = (int*)data;
    (*calls)++;
    DockPanelDef panel;
    panel.title = StrL("restored");
    if (context && context->state) {
        panel.name = context->state->panelName;
    }
    return panel;
}

static void ThePanelRegistryRebuildsPersistedPanels() {
    App app;
    int calls = 0;
    register_panel(&app, StrL("Probe"), BuildRegisteredPanel, &calls);
    Arena* arena = ArenaNew();
    DockAreaState saved;
    int tabs = saved.NewNode(StrL("TabPanel"));
    saved.nodes[tabs].kind = PanelInfoKind::Tabs;
    int leaf = saved.NewNode(StrL("Probe"));
    saved.nodes[leaf].kind = PanelInfoKind::Panel;
    VecAppend(saved.nodes[tabs].children, leaf);
    saved.center = tabs;

    Entity<DockState> state = EntityNewState<DockState>(&app);
    utassert(DockLoad(state.Get(&app), &saved, arena, nullptr, &app, nullptr,
                      state));
    utassert(calls == 1);
    utassert(state.Get(&app)->panels.len == 1);
    utassert(StrEqI(state.Get(&app)->panels[0].title, "restored"));

    EntityDropAll(&app);
    ArenaDelete(arena);
}

static void SourceDockGeometryFacadesAreExact() {
    DockSizing left = DockSizing::New(DockPlacement::Left)
                          .WithAreaWidth(1000)
                          .WithOppositeDockSize(300);
    utassertnear(left.Clamp(900), 600);
    DockSizing bottom = DockSizing::New(DockPlacement::Bottom)
                            .WithAreaBounds({0, 0, 800, 600});
    utassertnear(bottom.SizeFromPointer({400, 400}), 200);

    Placement placement = Placement::Top;
    utassert(split_placement_at({0, 0, 200, 100}, {10, 50}, &placement));
    utassert(placement == Placement::Left);
    utassert(!split_placement_at({0, 0, 200, 100}, {100, 50}, &placement));
    DropPlaceholderBounds half =
        DropPlaceholderBounds::ForPlacement({120, 80, 400, 300}, &placement);
    // `placement` remains Left after the center query above.
    utassertnear(half.origin.x, 0);
    utassertnear(half.size.w, 200);
    DragPanel drag = DragPanel::New(PurePanel(1), NodeId::FromU64(7));
    utassert(drag.dragSessionId != 0);
}

static El* FindNamedDk(El* root, const char* name) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && base::StrEqI(root->id, name)) {
        return root;
    }
    for (El* c = root->first; c; c = c->next) {
        if (El* hit = FindNamedDk(c, name)) {
            return hit;
        }
    }
    return nullptr;
}

// Every part of an area is named among the area's own parts -- a handle is
// `split-{node}-{ix}` and not the area's name spelled out again -- so two
// areas on one page need the area's name over them to stay apart. That is
// what stopped the dock moving onto the fold for as long as the handle read
// its own number back out of the window while the frame was being built.
static void TwoAreasHaveTwoSplitHandles() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = arena;

    Entity<DockState> one = EntityNewState<DockState>(&app);
    Entity<DockState> two = EntityNewState<DockState>(&app);
    int a = 0, b = 0;
    Seed(one.Get(&cx), &a, &b);
    Seed(two.Get(&cx), &a, &b);

    El* page = Div(arena);
    El* left = DockArea::New(&cx, StrL("left"), one, nullptr);
    El* right = DockArea::New(&cx, StrL("right"), two, nullptr);
    page->Child(left)->Child(right);
    IdsCollect(page);

    // The scope is off the stack again once an area is built.
    utassert(cx.path == 0);

    // Seed builds the split as node 2, over the two tab groups.
    const char* name = "split-2-0";
    El* hL = FindNamedDk(left, name);
    El* hR = FindNamedDk(right, name);
    utassert(hL && hR);
    if (hL && hR) {
        utassert(hL->clickId != 0 && hR->clickId != 0);
        utassert(hL->clickId != hR->clickId);
    }
    // A handle knows whether it is the one being dragged from its own
    // element state, which is keyed the way `with_element_state` is -- so
    // the same handle in two areas is two states, and neither is the area's.
    Entity<SharedHandleState> hsL, hsR;
    {
        IdScope area(&cx, StrL("left"));
        hsL = ResizeHandleStateFor(&cx, Str(name));
    }
    {
        IdScope area(&cx, StrL("right"));
        hsR = ResizeHandleStateFor(&cx, Str(name));
    }
    utassert(hsL.id != hsR.id);
    utassert(hsL.Get(&cx) && !ResizeHandleStateIsActive(hsL.Get(&cx)->Get()));

    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

// a_dock_is_its_own_width_under_a_renderer_that_draws_no_chrome.
//
// A dock's box is base's, not its renderer's. The extent used to live in the
// themed skin's `dock` hook, so it was reachable only through that one
// renderer, and a null hook hands the content straight back. A dock that
// never states its extent is not a column beside the centre: it takes
// whatever the row gives it and the panes inside shrink to their content.
// Nothing failed, and nothing said why.
static void ADockIsItsOwnWidthUnderARendererThatDrawsNoChrome() {
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    Entity<DockState> state = EntityNewState<DockState>(&app);
    DockState* s = state.Get(&cx);
    int a = 0, b = 0;
    Seed(s, &a, &b);
    // The right dock holds its own group, at a width of its own.
    int dockNode = DockNewTabs(s);
    DockTabsAdd(s, dockNode, 0);
    s->right.node = dockNode;
    s->right.open = true;
    s->right.SetSize(200);

    // The bare renderer: every hook null, which is the position every
    // renderer that is not the themed skin starts from.
    El* area = DockArea::New(&cx, StrL("area"), state, nullptr);
    const RuntimeStyle& th = RuntimeStyleNow(&app);
    LayoutEl(&win->paint, area, 0, 0, 800, 600, th.fontSize, th.foreground);

    // The right dock has to be its own width, not the area's, and the centre
    // has to be what the docks leave.
    utassertnear(s->right.GetSize(), 200.f);
    El* dock = area->first;
    utassert(dock != nullptr);
    // The area is a row: centre first, then the right dock beside it.
    El* right = area->first;
    while (right && right->next) {
        right = right->next;
    }
    utassert(right != nullptr);
    utassertnear(right->w, 200.f);
    utassertnear(right->h, 600.f);
    utassertnear(area->first->w, 600.f);
    // A group has to fill its slot, or its panel gets no height.
    utassertnear(area->first->h, 600.f);

    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

// dock_size_change_emits_one_layout_event: only an effective size change is
// persisted, so setting a dock to the width it already has neither redraws
// nor announces.
static El* FindDockBoundsElement(El* el, Bounds* bounds) {
    if (el->boundsOut == bounds) return el;
    for (El* child = el->first; child; child = child->next) {
        if (El* found = FindDockBoundsElement(child, bounds)) return found;
    }
    return nullptr;
}

static void RestoredSplitSharesSurviveResizeAndTabChanges() {
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<DockState> state = EntityNewState<DockState>(&app);
    DockState* s = state.Get(&cx);
    int a = 0, b = 0;
    Seed(s, &a, &b);
    s->nodes[s->center].size[0] = 300;
    s->nodes[s->center].size[1] = 100;
    const RuntimeStyle& th = RuntimeStyleNow(&app);
    for (int frame = 0; frame < 4; frame++) {
        s->nodes[a].activeIx = frame % 2;
        float width = frame < 2 ? 800.f : 1200.f;
        El* area = DockArea::New(&cx, StrL("shares"), state, nullptr);
        LayoutEl(&win->paint, area, 0, 0, width, 600, th.fontSize,
                 th.foreground);
        float left = FindDockBoundsElement(area, &s->nodes[a].bounds)->w;
        float right = FindDockBoundsElement(area, &s->nodes[b].bounds)->w;
        utassert(left > 0 && right > 0);
        utassertnear(left / (left + right), .75f);
        utassertnear(s->nodes[s->center].size[0], 300);
    }
    // An unresolved slot takes the leftover without scaling its sibling.
    s->nodes[s->center].size[0] = 0;
    s->nodes[s->center].size[1] = 200;
    for (int frame = 0; frame < 2; frame++) {
        s->nodes[a].activeIx = frame;
        El* area = DockArea::New(&cx, StrL("shares"), state, nullptr);
        LayoutEl(&win->paint, area, 0, 0, 800, 600, th.fontSize, th.foreground);
        utassertnear(FindDockBoundsElement(area, &s->nodes[b].bounds)->w, 200);
    }
    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

// dock_area.rs: set_split_sizes_restores_a_share_and_reports_it. Sizes of
// [100, 300] on a 50/50 split land as a 25% share and emit exactly one
// LayoutChanged; a mismatched length leaves the split and the count alone.
static void SetSplitSizesRestoresAShareAndReportsIt() {
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<DockState> state = EntityNewState<DockState>(&app);
    DockState* s = state.Get(&cx);
    int a = 0, b = 0;
    Seed(s, &a, &b);
    Entity<DockEventCounter> counter = EntityNewState<DockEventCounter>(&app);
    s->onEvent = ListenTo(counter, &DockEventCounter::OnEvent);
    const RuntimeStyle& th = RuntimeStyleNow(&app);

    const float sizes[] = {100, 300};
    DockSetSplitSizes(s, &cx, s->center, sizes, 2);
    utassert(counter.Get(&app)->layoutChanges == 1);
    El* area = DockArea::New(&cx, StrL("sizes"), state, nullptr);
    LayoutEl(&win->paint, area, 0, 0, 600, 400, th.fontSize, th.foreground);
    float left = FindDockBoundsElement(area, &s->nodes[a].bounds)->w;
    float right = FindDockBoundsElement(area, &s->nodes[b].bounds)->w;
    utassert(left > 0 && right > 0);
    utassert(fabsf(left / (left + right) - .25f) < .01f);

    const float one[] = {10};
    DockSetSplitSizes(s, &cx, s->center, one, 1);
    utassert(counter.Get(&app)->layoutChanges == 1);
    utassertnear(s->nodes[s->center].size[0], 100);
    utassertnear(s->nodes[s->center].size[1], 300);

    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

static void ADockSizeChangeEmitsOneLayoutEvent() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    Entity<DockState> state = EntityNewState<DockState>(&app);
    DockState* s = state.Get(&cx);
    int a = 0, b = 0;
    Seed(s, &a, &b);
    int dockNode = DockNewTabs(s);
    DockTabsAdd(s, dockNode, 0);
    s->left.node = dockNode;
    s->left.SetSize(200);

    Entity<DockEventCounter> counter = EntityNewState<DockEventCounter>(&app);
    s->onEvent = ListenTo(counter, &DockEventCounter::OnEvent);

    DockSetDockSize(s, &cx, DockPlacement::Left, 320);
    DockSetDockSize(s, &cx, DockPlacement::Left, 320);
    utassertnear(s->left.GetSize(), 320.f);
    utassert(counter.Get(&app)->layoutChanges == 1);

    // A size the Dock clamps back to where it already was is not a change
    // either: the minimum is what it lands on both times.
    DockSetDockSize(s, &cx, DockPlacement::Left, 1);
    utassertnear(s->left.GetSize(), kDockPanelMinSize);
    utassert(counter.Get(&app)->layoutChanges == 2);
    DockSetDockSize(s, &cx, DockPlacement::Left, 2);
    utassert(counter.Get(&app)->layoutChanges == 2);

    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

// dock.rs:
// dragging_the_bottom_handle_below_the_minimum_snaps_to_the_nearer_end. Rust
// drives the skin's handle with simulated mouse events; the drag is base's
// here, so this walks the same pointer positions through it.
static void DraggingTheBottomHandleBelowTheMinimumSnapsToTheNearerEnd() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<DockState> state = EntityNewState<DockState>(&app);
    DockState* s = state.Get(&cx);
    int a = 0, b = 0;
    Seed(s, &a, &b);
    int dockNode = DockNewTabs(s);
    DockTabsAdd(s, dockNode, 0);
    s->bottom.node = dockNode;
    s->bottom.SetSize(200);
    s->bounds = {0, 0, 800, 600};

    auto drag = [&](const float* ys, int n) {
        s->resizing = true;
        s->resizingSide = DockPlacement::Bottom;
        for (int i = 0; i < n; i++) {
            DockResizeSide(s, &cx, DockPlacement::Bottom, 400.f, ys[i]);
        }
        DockState::OnResizeEnd(s, &cx, nullptr);
    };

    // From the 200px dock's handle, through the closed strip and back,
    // released 60px tall: nearer the strip than the minimum, so it closes.
    const float shut[] = {406.f, 580.f, 540.f};
    s->resizing = true;
    s->resizingSide = DockPlacement::Bottom;
    DockResizeSide(s, &cx, DockPlacement::Bottom, 400.f, 406.f);
    utassert(s->bottom.IsOpen() && s->bottom.LiveSize() < 0);
    DockResizeSide(s, &cx, DockPlacement::Bottom, 400.f, 580.f);
    // Below the strip it shows closed, at the strip.
    utassert(!s->bottom.IsOpen());
    utassertnear(s->bottom.LiveSize(), kClosedBottomStrip);
    drag(shut, 3);
    utassert(!s->bottom.IsOpen() && s->bottom.LiveSize() < 0);

    // From the closed strip's handle, released 80px tall: nearer the
    // minimum, so it opens at the minimum.
    const float open[] = {565.f, 520.f};
    drag(open, 2);
    utassert(s->bottom.IsOpen());
    utassertnear(s->bottom.GetSize(), kDockPanelMinSize);
    utassert(s->bottom.LiveSize() < 0);

    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

// dock.rs dragging_the_left_handle_resizes_only_the_left_dock, and the edge
// every dock resizes from: resize_handle(..).inside(edge) with the
// indicator appearance. Every dock's handle once shared one element id, so
// a press on the left handle let the right one claim the drag; one id per
// placement keeps their states and their payloads apart. The handle hugs
// the dock's inner edge, its hairline the dock's outermost pixel, and a
// shut bottom dock keeps its handle on top of the strip it shrank to.
static void DraggingTheLeftHandleResizesOnlyTheLeftDock() {
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<DockState> state = EntityNewState<DockState>(&app);
    DockState* s = state.Get(&cx);
    int a = 0, b = 0;
    Seed(s, &a, &b);
    DockSide* sides[3] = {&s->left, &s->right, &s->bottom};
    for (DockSide* side : sides) {
        int node = DockNewTabs(s);
        DockTabsAdd(s, node, 0);
        side->node = node;
        side->open = true;
        side->SetSize(200);
    }
    s->bottom.open = false;
    s->bounds = {0, 0, 800, 600};

    component::DockSkin skin = component::DockSkin::New(state);
    El* area = DockArea::New(&cx, StrL("area"), state, skin.Renderer());
    IdsCollect(area);
    const RuntimeStyle& th = RuntimeStyleNow(&app);
    LayoutEl(&win->paint, area, 0, 0, 800, 600, th.fontSize, th.foreground);

    El* left = FindNamedDk(area, "resize-handle-left");
    El* right = FindNamedDk(area, "resize-handle-right");
    El* bottom = FindNamedDk(area, "resize-handle-bottom");
    utassert(left && right && bottom);
    if (!left || !right || !bottom) {
        return;
    }
    utassert(left->clickId != right->clickId);
    utassert(left->drag.ix != right->drag.ix);
    utassert(base::StrEq(left->drag.kind, kDockResizeDrag));
    utassert(left->style.absolute);
    utassert(left->cursor == CursorKind::ColResize);
    utassert(bottom->cursor == CursorKind::RowResize);
    // The left dock spans [0, 200): its hairline is x in [199, 200). The
    // right one spans [600, 800) and hugs its leading edge.
    utassert(left->first && right->first && bottom->first);
    utassertnear(left->first->x, 199.f);
    utassertnear(left->first->w, 1.f);
    utassertnear(left->x + left->w, 200.f);
    utassertnear(right->first->x, 600.f);
    utassertnear(right->x, 600.f);
    // The shut bottom dock is its 29px strip, and its handle is on top of it.
    utassertnear(bottom->first->y, 600.f - kClosedBottomStrip);
    utassertnear(bottom->first->h, 1.f);

    // Past the drag threshold, then the move the claimed dock resizes to,
    // then the release.
    const float xs[2] = {204.f, 240.f};
    for (float x : xs) {
        DragMoveEvent move;
        move.drag = left->drag;
        move.event.x = x;
        move.event.y = 300.f;
        ListenerCall(&app, win, left->onDragMove, &move);
    }
    MouseUpEvent up;
    up.x = 240.f;
    up.y = 300.f;
    ListenerCall(&app, win, left->onMouseUpOut, &up);
    utassert(!s->resizing);
    utassertnear(s->right.GetSize(), 200.f);
    utassertnear(s->left.GetSize(), 240.f);

    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

static void SelectingAPanelByIdentityDoesNotMoveIt() {
    App app;
    Window win;
    win.app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, &win, arena, {}};
    DockState s;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    PanelId second = s.panels[1].id;
    PanelId unknown = PanelId::FromU64(0xffff);

    utassert(s.nodes[a].activeIx == 0);
    DockSelectPanel(&s, &cx, second);
    utassert(s.nodes[a].activeIx == 1);
    utassert(s.nodes[a].panel.len == 2);
    utassert(s.nodes[a].panel[0] == 0 && s.nodes[a].panel[1] == 1);
    DockSelectPanel(&s, &cx, unknown);
    utassert(s.nodes[a].activeIx == 1);

    ArenaDelete(arena);
}

// dock_area.rs a_move_of_an_unowned_panel_is_ignored: a panel this area does
// not own is not moved. Rust names it by a PanelId from nowhere; here a panel
// index out of range is the same question, and a drop whose drag came from
// another area -- the nested-dock case the check is for -- is the one that
// used to move one of this area's panels by that area's index.
static void AMoveOfAnUnownedPanelIsIgnored() {
    App app;
    Window win;
    win.app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, &win, arena, {}};
    DockState s;
    DockState other;
    int a = 0, b = 0;
    Seed(&s, &a, &b);
    Seed(&other, &a, &b);

    utassert(!DockMovePanelTo(&s, 9999999, a, DockDrop::Center));
    utassert(s.nodes[a].panel.len == 2 && s.nodes[b].panel.len == 1);

    DropEvent drop;
    drop.drag.kind = kDockPanelDrag;
    drop.drag.ix = 1;
    drop.drag.data = &other;
    drop.el = s.nodes[b].bounds;
    drop.x = 450;
    drop.y = 200;
    DockState::OnDropPanel(&s, &cx, &drop, b);
    DockState::OnDropTab(&s, &cx, &drop, DockPack(b, 0));
    DockState::OnDropTabBar(&s, &cx, &drop, b);
    utassert(s.nodes[a].panel.len == 2 && s.nodes[b].panel.len == 1);

    // The same drop from this area's own tab moves it.
    drop.drag.data = &s;
    DockState::OnDropPanel(&s, &cx, &drop, b);
    utassert(s.nodes[a].panel.len == 1 && s.nodes[b].panel.len == 2);
    ArenaDelete(arena);
}

// tab_panel.rs close-button tests, through the themed skin's tab bar:
// a_closable_panel_gets_a_close_button,
// a_non_closable_panel_gets_no_close_button, close_buttons_are_off_by_default,
// close_button_visibility_updates_after_the_skin_setting_changes,
// clicking_a_close_button_removes_only_that_tab and
// a_collapsed_group_draws_no_close_button.
static El* DockCloseButton(Ctx* cx, const DockRenderer* r,
                           const DockTabGroup* g, int ix) {
    El* bar = r->tabBar(cx, r->data, g);
    IdsCollect(bar);
    char name[32];
    snprintf(name, sizeof(name), "close-%d-%d", g->node, ix);
    return FindNamedDk(bar, name);
}

static void TabCloseButtonsFollowTheSkinAndThePanel() {
    App app;
    Window win;
    win.app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, &win, arena, {}};
    Entity<DockState> state = EntityNewState<DockState>(&app);
    DockState* s = state.Get(&app);
    int a = 0, b = 0;
    Seed(s, &a, &b);
    // Panel 1, in group a at tab 1, is the one under test; panel 0 at tab 0
    // is a non-closable filler that never draws one.
    s->panels[0].closable = false;

    component::DockSkin skin = component::DockSkin::New(state);
    const DockRenderer* r = skin.Renderer();
    DockTabGroup g;
    g.cx = &cx;
    g.state = state;
    g.node = a;

    // Off by default.
    utassert(!skin.IsCloseButtonVisible(&app));
    utassert(DockCloseButton(&cx, r, &g, 1) == nullptr);
    utassert(DockGroupIsPanelClosable(&g, 1));
    utassert(!DockGroupIsPanelClosable(&g, 0));

    // Visibility follows the skin setting both ways.
    bool expected[3] = {true, false, true};
    for (bool visible : expected) {
        skin.SetCloseButtonVisible(&app, nullptr, visible);
        utassert(skin.IsCloseButtonVisible(&app) == visible);
        utassert((DockCloseButton(&cx, r, &g, 1) != nullptr) == visible);
        // A non-closable panel's tab never draws one.
        utassert(DockCloseButton(&cx, r, &g, 0) == nullptr);
    }

    // A collapsed group offers none.
    g.collapsed = true;
    utassert(DockCloseButton(&cx, r, &g, 1) == nullptr);
    g.collapsed = false;

    // The click closes exactly that tab and does not select it: the button
    // stops the click the tab also listens for.
    El* close = DockCloseButton(&cx, r, &g, 1);
    utassert(close && close->stopClick && close->listener.IsValid());
    utassert(s->nodes[a].activeIx == 0);
    PanelId filler = s->panels[0].id;
    ClickEvent click;
    ListenerCall(&app, &win, close->listener, &click);
    utassert(s->nodes[a].panel.len == 1 && s->nodes[a].panel[0] == 0);
    utassert(s->panels[s->nodes[a].panel[0]].id.value == filler.value);
    utassert(s->nodes[a].activeIx == 0);

    WindowKeyedFree(&win);
    ArenaDelete(arena);
    EntityDropAll(&app);
}

void TestDock() {
    AMoveOfAnUnownedPanelIsIgnored();
    TabCloseButtonsFollowTheSkinAndThePanel();
    ADockIsItsOwnWidthUnderARendererThatDrawsNoChrome();
    RestoredSplitSharesSurviveResizeAndTabChanges();
    ADockSizeChangeEmitsOneLayoutEvent();
    SetSplitSizesRestoresAShareAndReportsIt();
    DraggingTheBottomHandleBelowTheMinimumSnapsToTheNearerEnd();
    DraggingTheLeftHandleResizesOnlyTheLeftDock();
    SelectingAPanelByIdentityDoesNotMoveIt();
    TheFiveDropZones();
    ThePlaceholderCoversEachZone();
    ADropInTheMiddleMerges();
    ADropOnAnEdgeSplits();
    AnEmptyGroupLeavesTheSplit();
    ARootGroupStays();
    ADropOnATabTakesItsPlace();
    AReorderCountsFromBeforeTheDetach();
    ADropOnItsOwnGroupNeedsATabOrAnEdge();
    ANodeKnowsWhichDockItIsIn();
    ATabOffTheEndIsBroughtIntoView();
    ALayoutSurvivesDumpAndLoad();
    APanelNothingAnswersToBecomesInvalid();
    ALockedDockMovesNothing();
    AHiddenPanelIsNotThere();
    TheLastPanelStays();
    TheTogglesPickTheirGroup();
    ANonCollapsibleDockOpens();
    NormalizeCollapsesWhatAnEditNeverMakes();
    NormalizeClampsTheActiveTab();
    NormalizeKeepsARoot();
    AHiddenGroupIsNotASlot();
    TheUiPanelHandleCrossesTheBaseSeam();
    ThePurePaneTreeMatchesTheSourceAlgebra();
    PurePaneTreeNormalizesSizes();
    DockLayoutDescribesWithoutBuildingUi();
    ThePanelRegistryRebuildsPersistedPanels();
    SourceDockGeometryFacadesAreExact();
    TwoAreasHaveTwoSplitHandles();
}
