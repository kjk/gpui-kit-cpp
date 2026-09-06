/* Ported from crates/ui/src/dock/state.rs and the layout it reads in
 * crates/ui/src/fixtures/layout.json.
 *
 * A saved dock layout is a tree of panel states: a split carries its sizes
 * and its axis, a tab group its active index, a leaf whatever the panel
 * wrote, and a tiles node a TileMeta per tile. `test_deserialize_item_state`
 * is the fixture read back; the rest is the round trip and the tiles' own
 * half of it. */

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

// A layout written out and read back says the same thing, tiles included.
static void ALayoutSurvivesTheRoundTrip() {
    Arena* a = ArenaNew();
    DockAreaState s;
    s.hasVersion = true;
    s.version = 2;
    s.center = s.NewNode(StrL("Tiles"));
    int one = s.NewNode(StrL("TabPanel"));
    int two = s.NewNode(StrL("TabPanel"));
    PanelStateNode& tiles = s.nodes[s.center];
    tiles.kind = PanelInfoKind::Tiles;
    VecAppend(tiles.children, one);
    VecAppend(tiles.children, two);
    VecAppend(tiles.metas, TileMeta{{16, 24, 300, 200}, 0});
    VecAppend(tiles.metas, TileMeta{{340, 24, 260, 160}, 3});
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
    utassert(StrEqI(node.panelName, "Tiles"));
    utassert(node.kind == PanelInfoKind::Tiles);
    utassert(node.children.len == 2);
    utassert(node.metas.len == 2);
    utassertnear(node.metas[0].bounds.x, 16.f);
    utassertnear(node.metas[0].bounds.h, 200.f);
    utassert(node.metas[0].zIndex == 0);
    utassertnear(node.metas[1].bounds.x, 340.f);
    utassertnear(node.metas[1].bounds.w, 260.f);
    utassert(node.metas[1].zIndex == 3);
    // A dock that is not there is left out rather than written as null, so
    // the ones that are come back and the others do not.
    utassert(back.left.present && !back.left.open);
    utassertnear(back.left.size, 180.f);
    utassert(!back.right.present && !back.bottom.present);
    StrFree(text);
    ArenaDelete(a);
}

// TileMeta::default: a 200x200 box ten pixels in, which is what a tile with
// nothing saved for it gets.
static void ATileWithNothingSavedGetsTheDefaultBox() {
    TileMeta meta;
    utassertnear(meta.bounds.x, 10.f);
    utassertnear(meta.bounds.y, 10.f);
    utassertnear(meta.bounds.w, 200.f);
    utassertnear(meta.bounds.h, 200.f);
    utassert(meta.zIndex == 0);
}

// The tiles' own half: where every tile sits, saved and put back.
static void TheTilesGoBackWhereTheyWere() {
    TilesState s;
    TilesAdd(&s, 0, {16, 16, 300, 200});
    TilesAdd(&s, 1, {340, 16, 260, 160});
    s.items[1].zIndex = 2;

    TileMeta metas[4] = {};
    int panels[4] = {};
    int n = TilesToMetas(&s, metas, panels, 4);
    utassert(n == 2);
    utassertnear(metas[1].bounds.x, 340.f);
    utassert(metas[1].zIndex == 2);
    utassert(panels[0] == 0 && panels[1] == 1);

    // Moved, and then put back where the metas say.
    s.items[0].bounds = {500, 500, 120, 120};
    s.items[1].zIndex = 0;
    TilesFromMetas(&s, metas, panels, n);
    utassertnear(s.items[0].bounds.x, 16.f);
    utassertnear(s.items[0].bounds.w, 300.f);
    utassert(s.items[1].zIndex == 2);
    utassert(s.dragging < 0 && s.resizing < 0);

    // The tiles are reordered as they come to the front, so a meta goes back
    // on the panel it was saved from rather than on whatever is in its slot.
    TilesBringToFront(&s, 0);
    utassert(s.items[0].panel == 1);
    TilesFromMetas(&s, metas, panels, n);
    utassert(s.items[0].panel == 0);
    utassertnear(s.items[0].bounds.x, 16.f);
    utassert(s.items[1].panel == 1);
    utassertnear(s.items[1].bounds.x, 340.f);

    // A tile the layout says nothing about keeps its place, after the ones it
    // does.
    TilesAdd(&s, 7, {0, 400, 100, 100});
    TilesFromMetas(&s, metas, panels, n);
    utassert(s.items.len == 3);
    utassert(s.items[2].panel == 7);
    utassertnear(s.items[2].bounds.y, 400.f);
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

static void ATilesCenterPersistsWithoutItsInternalSplit() {
    PaneTree tree(RootKind::Split);
    TilePanel tiles[] = {
        TilePanel::New(PanelId::FromU64(1), {20, 30, 400, 200})};
    tiles[0].zIndex = 7;
    tree.SetRootTiles(tiles, 1);
    tree.Normalize();
    utassert(tree.Root()->paneKind == PaneKind::Split);
    PanelSource source;
    source.panelName = [](void*, PanelId) { return StrL("Alpha"); };
    DockAreaState state;
    state.center = tree.ToState(source, &state);
    const PanelState& saved = state.nodes[state.center];
    utassert(StrEq(saved.panelName, StrL("Tiles")));
    utassert(saved.kind == PanelInfoKind::Tiles && saved.children.len == 1);
    utassert(StrEq(state.nodes[saved.children[0]].panelName, StrL("Alpha")));
    utassert(saved.metas.len == 1 && saved.metas[0].zIndex == 7);
    utassertnear(saved.metas[0].bounds.x, 20);
    utassertnear(saved.metas[0].bounds.w, 400);
    StrBuilder json;
    DockAreaStateWrite(&state, &json);
    Arena* arena = ArenaNew();
    DockAreaState loaded;
    utassert(DockAreaStateParse(arena, Str(json.els, json.len), &loaded));
    utassert(loaded.nodes[loaded.center].kind == PanelInfoKind::Tiles);
    ArenaDelete(arena);

    PaneTree tabs(RootKind::Split);
    PanelId panel = PanelId::FromU64(1);
    tabs.SetRootTabs(&panel, 1);
    tabs.Normalize();
    state.Clear();
    state.center = tabs.ToState(source, &state);
    utassert(state.nodes[state.center].kind == PanelInfoKind::Stack);
}

void TestDockState() {
    TestSuite("dock/state");
    TheFixtureLayoutReadsBack();
    ATilesCenterPersistsWithoutItsInternalSplit();
    ALayoutSurvivesTheRoundTrip();
    ATileWithNothingSavedGetsTheDefaultBox();
    TheTilesGoBackWhereTheyWere();
    SomethingThatIsNotALayoutIsRefused();
}
