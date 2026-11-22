#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// crates/base/examples/showcase/components/tree.rs. The page is the base
// tree with a row of its own: `Tree::new(&self.tree).w_64().h_48()` and an
// `item(..)` closure that draws the indent, the chevron and the label.
struct TreeRow {
    const char* label;
    int parent; // -1 root
};

static const TreeRow kTree[] = {
    {"src", -1},   {"components", 0}, {"button.rs", 1},   {"tree.rs", 1},
    {"lib.rs", 0}, {"examples", -1},  {"showcase.rs", 5}, {"Cargo.toml", -1},
};

static const int kTreeCount = (int)(sizeof(kTree) / sizeof(kTree[0]));

// The items go in once, in the order they are declared, since a child has to
// be added after its parent. `src` and `components` start open, the way the
// Rust page's fixture does.
static Entity<TreeState> ShowcaseTreeState(ShowcaseApp* app, Ctx* cx) {
    if (!app->tree.IsValid()) {
        app->tree = EntityNewState<TreeState>(cx->app);
    }
    TreeState* s = app->tree.Get(cx);
    if (!s || len(s->items) > 0) {
        return app->tree;
    }
    // A row is 32 here, not the ListItem's 34: the page draws its own.
    s->rowH = 32;
    // TreeAddItem copies the strings it is given, so each temporary id may be
    // discarded as soon as its row is added.
    for (int i = 0; i < kTreeCount; i++) {
        TempStr id = fmt("sc-tree-%d", i);
        TreeAddItem(s, id, Str(kTree[i].label), kTree[i].parent);
    }
    for (int i = 0; i < len(s->items); i++) {
        s->items[i].expanded = i == 0 || i == 1;
    }
    TreeRebuild(s);
    return app->tree;
}

// One row: the indent, a chevron for a folder, and the label.
static El* ShowcaseTreeRow(void*, Ctx* cx, int, const TreeEntry& entry,
                           TreeEntryState entryState) {
    const TreeItem* it = entry.item;
    if (!it) {
        return nullptr;
    }
    Arena* a = cx->a;
    // The rows fill the tree: Rust's item is mx_1 inside a size_full list, so
    // the selected background runs the width of the box less 4px a side.
    El* row = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->H(32)
                  ->PadX(8)
                  ->ItemsCenter()
                  ->Gap(4)
                  ->HoverBg(ExampleRgb(0xf5f5f5));
    if (entryState.IsSelected()) {
        row->Bg(ExampleRgb(0xf0f0f0));
    }
    if (it->depth > 0) {
        row->Child(Div(a)->W((float)it->depth * 12));
    }
    // A 12px chevron, right when the folder is collapsed and down when it is
    // open — the two SVGs the Rust example inlines. The box is there for a
    // file too, so the labels line up.
    El* icon = Div(a)->W(12)->H(12)->ItemsCenter()->JustifyCenter();
    if (it->folder) {
        IconName chevron =
            it->expanded ? IconName::ChevronDown : IconName::ChevronRight;
        icon->Child(IconEl(a, chevron, 12)->Fg(ScInk()));
    }
    row->Child(icon);
    row->Child(ScTxt(cx, it->label, 14, ScInk()));
    return row;
}

El* ShowcaseTree(ShowcaseApp* app, Ctx* cx) {
    Entity<TreeState> state = ShowcaseTreeState(app, cx);
    // The tree is the base element, so the wheel over it scrolls the tree and
    // the keyboard walks it — `Tree::new(&self.tree).w_64().h_48()`.
    return TreeList::New(cx, StrL("sc-tree"), state, 192, &ShowcaseTreeRow, app)
        ->W(256)
        // Rust's row is `mx_1` inside a `size_full` list and the box itself
        // is `py_1`; there are no margins here, so the inset either side is
        // the box's padding instead — the same 4px, and the selected
        // background still stops short of the border.
        ->Pad(4)
        ->Border(1, ExampleRgb(0xd4d4d4));
}

SHOWCASE_PAGE(CompTree, ShowcaseTree);
