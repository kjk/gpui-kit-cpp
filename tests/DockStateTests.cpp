/* Ported from crates/ui/src/dock/state.rs,
 * crates/base/src/dock/state_convert.rs, and the layout they read in
 * crates/ui/src/fixtures/layout.json.
 *
 * A saved dock layout is a tree of panel states: a split carries its sizes
 * and its axis, a tab group its active index, a leaf whatever the panel
 * wrote. `test_deserialize_item_state` is the fixture read back; the rest is
 * the round trip and PaneTree::FromState. */

#include "Test.h"

// The same shape as the Rust fixture, cut to the parts its test asserts on:
// a vertical stack of two tab panels in the centre, and the three docks.
static const char* kLayoutJson = R"JSON({
  "center": {
    "panel_name": "StackPanel",
    "children": [
      {
        "panel_name": "TabPanel",
        "children": [
          {
            "panel_name": "StoryContainer",
            "children": [],
            "info": { "panel": "ButtonStory" }
          }
        ],
        "info": { "tabs": { "active_index": 0 } }
      },
      {
        "panel_name": "TabPanel",
        "children": [
          {
            "panel_name": "StoryContainer",
            "children": [],
            "info": { "panel": "PopupStory" }
          }
        ],
        "info": { "tabs": { "active_index": 0 } }
      }
    ],
    "info": { "stack": { "sizes": [704.0, 263.0], "axis": 1 } }
  },
  "left_dock": {
    "panel": {
      "panel_name": "TabPanel",
      "children": [
        {
          "panel_name": "StoryContainer",
          "children": [],
          "info": { "panel": "ListStory" }
        }
      ],
      "info": { "tabs": { "active_index": 0 } }
    },
    "placement": "left",
    "size": 350.0,
    "open": true
  },
  "right_dock": {
    "panel": {
      "panel_name": "TabPanel",
      "children": [
        {
          "panel_name": "StoryContainer",
          "children": [],
          "info": { "panel": "ImageStory" }
        }
      ],
      "info": { "tabs": { "active_index": 0 } }
    },
    "placement": "right",
    "size": 320.0,
    "open": true
  },
  "bottom_dock": {
    "panel": {
      "panel_name": "TabPanel",
      "children": [
        {
          "panel_name": "StoryContainer",
          "children": [],
          "info": { "panel": "TextStory" }
        },
        {
          "panel_name": "StoryContainer",
          "children": [],
          "info": { "panel": "IconStory" }
        }
      ],
      "info": { "tabs": { "active_index": 0 } }
    },
    "placement": "bottom",
    "size": 200.0,
    "open": true
  }
})JSON";

// test_deserialize_item_state.
static void TheFixtureLayoutReadsBack() {
    Arena* a = ArenaNew();
    DockAreaState s;
    utassert(DockAreaStateParse(a, Str(kLayoutJson), &s));
    // The fixture carries no version, and Rust's is None.
    utassert(!s.hasVersion);

    const PanelStateNode& center = s.nodes[s.center];
    utassert(StrEqI(center.panelName, "StackPanel"));
    utassert(center.children.len == 2);
    utassert(center.kind == PanelInfoKind::Stack);
    utassert(center.sizes.len == 2);
    utassertnear(center.sizes[0], 704.f);
    utassertnear(center.sizes[1], 263.f);
    // axis 1 is vertical.
    utassert(!AxisIsHorizontal(center.axis));

    const PanelStateNode& first = s.nodes[center.children[0]];
    utassert(StrEqI(first.panelName, "TabPanel"));
    utassert(first.kind == PanelInfoKind::Tabs);
    utassert(first.activeIndex == 0);
    const PanelStateNode& second = s.nodes[center.children[1]];
    utassert(StrEqI(second.panelName, "TabPanel"));
    utassert(second.children.len == 1);
    utassert(StrEqI(s.nodes[second.children[0]].panelName, "StoryContainer"));

    utassert(s.left.present);
    utassert(s.left.open);
    utassertnear(s.left.size, 350.f);
    utassert(s.left.placement == DockPlacement::Left);
    const PanelStateNode& left = s.nodes[s.left.node];
    utassert(StrEqI(left.panelName, "TabPanel"));
    utassert(left.children.len == 1);
    utassert(StrEqI(s.nodes[left.children[0]].panelName, "StoryContainer"));

    utassert(s.bottom.present);
    utassert(s.bottom.open);
    utassertnear(s.bottom.size, 200.f);
    utassert(s.bottom.placement == DockPlacement::Bottom);
    utassert(s.nodes[s.bottom.node].children.len == 2);

    utassert(s.right.present);
    utassert(s.right.open);
    utassertnear(s.right.size, 320.f);
    utassert(s.right.placement == DockPlacement::Right);
    utassert(s.nodes[s.right.node].children.len == 1);
    ArenaDelete(a);
}

// A layout written out and read back says the same thing.
static void ALayoutSurvivesTheRoundTrip() {
    Arena* a = ArenaNew();
    DockAreaState s;
    s.hasVersion = true;
    s.version = 2;
    s.center = s.NewNode(StrL("TabPanel"));
    int one = s.NewNode(StrL("Alpha"));
    int two = s.NewNode(StrL("Beta"));
    s.nodes[one].kind = PanelInfoKind::Panel;
    s.nodes[two].kind = PanelInfoKind::Panel;
    PanelStateNode& tabs = s.nodes[s.center];
    tabs.kind = PanelInfoKind::Tabs;
    tabs.activeIndex = 1;
    VecAppend(tabs.children, one);
    VecAppend(tabs.children, two);
    s.left.present = true;
    s.left.node = s.NewNode(StrL("TabPanel"));
    s.left.placement = DockPlacement::Left;
    s.left.size = 180;
    s.left.open = false;

    StrBuilder sb;
    DockAreaStateWrite(&s, &sb);
    Str text = sb.TakeStr();

    DockAreaState back;
    utassert(DockAreaStateParse(a, text, &back));
    utassert(back.hasVersion && back.version == 2);
    const PanelStateNode& node = back.nodes[back.center];
    utassert(StrEqI(node.panelName, "TabPanel"));
    utassert(node.kind == PanelInfoKind::Tabs);
    utassert(node.activeIndex == 1);
    utassert(node.children.len == 2);
    utassert(StrEqI(back.nodes[node.children[0]].panelName, "Alpha"));
    utassert(StrEqI(back.nodes[node.children[1]].panelName, "Beta"));
    // A dock that is not there is left out rather than written as null, so
    // the ones that are come back and the others do not.
    utassert(back.left.present && !back.left.open);
    utassertnear(back.left.size, 180.f);
    utassert(!back.right.present && !back.bottom.present);
    StrFree(text);
    ArenaDelete(a);
}

// Text that is not a layout is refused rather than half-read.
static void SomethingThatIsNotALayoutIsRefused() {
    Arena* a = ArenaNew();
    DockAreaState s;
    utassert(!DockAreaStateParse(a, StrL("not json"), &s));
    utassert(!DockAreaStateParse(a, StrL("{}"), &s));
    utassert(!DockAreaStateParse(a, StrL("[1, 2]"), &s));
    utassert(s.center < 0);
    ArenaDelete(a);
}

struct RecordingBuilder {
    Str names[8] = {};
    int n = 0;
    static PanelId Build(void* data, const PanelStateNode* state) {
        auto* self = (RecordingBuilder*)data;
        if (self->n < 8) {
            self->names[self->n] = state->panelName;
        }
        self->n++;
        return PanelId::FromU64((uint64_t)self->n);
    }
};

// An older file whose centre is a tiles node is a tab group: writers never
// emit tiles, and the metas are ignored.
static void ASavedTilesNodeReadsAsTabs() {
    static const char* kTilesJson = R"JSON({
  "center": {
    "panel_name": "Tiles",
    "children": [
      {
        "panel_name": "Alpha",
        "children": [],
        "info": { "panel": null }
      },
      {
        "panel_name": "Beta",
        "children": [],
        "info": { "panel": null }
      }
    ],
    "info": {
      "tiles": {
        "metas": [
          {
            "bounds": {
              "origin": { "x": 16, "y": 24 },
              "size": { "width": 300, "height": 200 }
            },
            "z_index": 0
          }
        ]
      }
    }
  }
})JSON";
    Arena* a = ArenaNew();
    DockAreaState s;
    utassert(DockAreaStateParse(a, Str(kTilesJson), &s));
    const PanelStateNode& node = s.nodes[s.center];
    utassert(StrEqI(node.panelName, "Tiles"));
    utassert(node.kind == PanelInfoKind::Tabs);
    utassert(node.activeIndex == 0);
    utassert(node.children.len == 2);
    utassert(StrEqI(s.nodes[node.children[0]].panelName, "Alpha"));
    utassert(StrEqI(s.nodes[node.children[1]].panelName, "Beta"));

    StrBuilder sb;
    DockAreaStateWrite(&s, &sb);
    Str text = sb.TakeStr();
    utassert(!StrContains(text, StrL("\"tiles\"")));
    utassert(StrContains(text, StrL("\"tabs\"")));
    StrFree(text);

    RecordingBuilder rec;
    PaneBuilder builder;
    builder.data = &rec;
    builder.build = RecordingBuilder::Build;
    PaneTree tree(RootKind::Any);
    tree.FromState(&s, s.center, builder);
    utassert(tree.Root() && tree.Root()->paneKind == PaneKind::Tabs);
    utassert(tree.Root()->panels.len == 2);
    utassert(rec.n == 2);
    utassert(StrEq(rec.names[0], StrL("Alpha")));
    utassert(StrEq(rec.names[1], StrL("Beta")));
    ArenaDelete(a);
}

static int AddPanelNode(DockAreaState* s, Str name) {
    int ix = s->NewNode(name);
    s->nodes[ix].kind = PanelInfoKind::Panel;
    return ix;
}

static int AddTabsNode(DockAreaState* s, const int* children, int n,
                       int active) {
    int ix = s->NewNode(StrL("TabPanel"));
    s->nodes[ix].kind = PanelInfoKind::Tabs;
    s->nodes[ix].activeIndex = active;
    for (int i = 0; i < n; i++) {
        VecAppend(s->nodes[ix].children, children[i]);
    }
    return ix;
}

// nested_tab_groups_are_flattened.
static void NestedTabGroupsAreFlattened() {
    DockAreaState s;
    int alpha = AddPanelNode(&s, StrL("Alpha"));
    int beta = AddPanelNode(&s, StrL("Beta"));
    int inner0 = AddTabsNode(&s, &alpha, 1, 0);
    int inner1 = AddTabsNode(&s, &beta, 1, 0);
    int nested[2] = {inner0, inner1};
    s.center = AddTabsNode(&s, nested, 2, 1);

    RecordingBuilder rec;
    PaneBuilder builder;
    builder.data = &rec;
    builder.build = RecordingBuilder::Build;
    PaneTree tree(RootKind::Any);
    tree.FromState(&s, s.center, builder);
    utassert(rec.n == 2);
    utassert(StrEq(rec.names[0], StrL("Alpha")));
    utassert(StrEq(rec.names[1], StrL("Beta")));
    utassert(tree.Root() && tree.Root()->paneKind == PaneKind::Tabs);
    utassert(tree.Root()->panels.len == 2);
    utassert(tree.Root()->activeIx == 1);
}

// a_bare_panel_leaf_is_wrapped_in_a_tab_group.
static void ABarePanelLeafIsWrappedInATabGroup() {
    DockAreaState s;
    s.center = AddPanelNode(&s, StrL("Alpha"));
    RecordingBuilder rec;
    PaneBuilder builder;
    builder.data = &rec;
    builder.build = RecordingBuilder::Build;
    PaneTree tree(RootKind::Any);
    tree.FromState(&s, s.center, builder);
    utassert(rec.n == 1);
    utassert(tree.Root() && tree.Root()->paneKind == PaneKind::Tabs);
    utassert(tree.Root()->panels.len == 1);
}

// a_tab_panel_carrying_panel_info_is_read_as_an_empty_group.
static void ATabPanelCarryingPanelInfoIsAnEmptyGroup() {
    DockAreaState s;
    s.center = s.NewNode(StrL("TabPanel"));
    s.nodes[s.center].kind = PanelInfoKind::Panel;
    RecordingBuilder rec;
    PaneBuilder builder;
    builder.data = &rec;
    builder.build = RecordingBuilder::Build;
    PaneTree tree(RootKind::Any);
    tree.FromState(&s, s.center, builder);
    utassert(rec.n == 0);
    utassert(tree.Root() && tree.Root()->paneKind == PaneKind::Tabs);
    utassert(tree.Root()->panels.len == 0);
}

// a_split_root_is_forced_even_when_the_state_is_a_tab_group.
static void ASplitRootIsForcedWhenTheStateIsATabGroup() {
    DockAreaState s;
    int alpha = AddPanelNode(&s, StrL("Alpha"));
    s.center = AddTabsNode(&s, &alpha, 1, 0);
    RecordingBuilder rec;
    PaneBuilder builder;
    builder.data = &rec;
    builder.build = RecordingBuilder::Build;
    PaneTree tree(RootKind::Split);
    tree.FromState(&s, s.center, builder);
    utassert(tree.Root() && tree.Root()->paneKind == PaneKind::Split);
    utassert(tree.Root()->children.len == 1);
    utassert(tree.Root()->children[0]->paneKind == PaneKind::Tabs);
}

// an_empty_tab_group_serializes_as_tabs_not_as_a_panel.
static void AnEmptyTabGroupSerializesAsTabs() {
    PaneTree tree(RootKind::Any);
    tree.SetRootTabs(nullptr, 0);
    PanelSource source;
    DockAreaState state;
    state.center = tree.ToState(source, &state);
    utassert(state.center >= 0);
    utassert(StrEq(state.nodes[state.center].panelName, StrL("TabPanel")));
    utassert(state.nodes[state.center].kind == PanelInfoKind::Tabs);
}

// an_empty_center_still_serializes_as_a_stack_panel.
static void AnEmptyCenterSerializesAsAStackPanel() {
    PaneTree tree(RootKind::Split);
    PanelSource source;
    DockAreaState state;
    state.center = tree.ToState(source, &state);
    utassert(state.center >= 0);
    utassert(StrEq(state.nodes[state.center].panelName, StrL("StackPanel")));
    utassert(state.nodes[state.center].kind == PanelInfoKind::Stack);
}

// A RootKind::Split tree whose live root is tabs is wrapped before dump.
static void ASplitRootSerializesAsAStackPanel() {
    PaneTree tabs(RootKind::Split);
    PanelId panel = PanelId::FromU64(1);
    tabs.SetRootTabs(&panel, 1);
    tabs.Normalize();
    PanelSource source;
    source.panelName = [](void*, PanelId) { return StrL("Alpha"); };
    DockAreaState state;
    state.center = tabs.ToState(source, &state);
    utassert(state.nodes[state.center].kind == PanelInfoKind::Stack);
}

void TestDockState() {
    TestSuite("dock/state");
    TheFixtureLayoutReadsBack();
    ALayoutSurvivesTheRoundTrip();
    SomethingThatIsNotALayoutIsRefused();
    ASavedTilesNodeReadsAsTabs();
    NestedTabGroupsAreFlattened();
    ABarePanelLeafIsWrappedInATabGroup();
    ATabPanelCarryingPanelInfoIsAnEmptyGroup();
    ASplitRootIsForcedWhenTheStateIsATabGroup();
    AnEmptyTabGroupSerializesAsTabs();
    AnEmptyCenterSerializesAsAStackPanel();
    ASplitRootSerializesAsAStackPanel();
}
