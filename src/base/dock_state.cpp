#include "base/dock_state.h"
#include "base/dock_registry.h"
#include "base/dock_layout.h"

namespace gpui {

static int PaneNodeToState(const PaneNode* node, const PanelSource& source,
                           DockAreaState* out) {
    Str name = node->paneKind == PaneKind::Split  ? StrL("StackPanel")
               : node->paneKind == PaneKind::Tabs ? StrL("TabPanel")
                                                  : StrL("Tiles");
    int ix = out->NewNode(name);
    out->nodes[ix]
        .kind = node->paneKind == PaneKind::Split  ? PanelInfoKind::Stack
                : node->paneKind == PaneKind::Tabs ? PanelInfoKind::Tabs
                                                   : PanelInfoKind::Tiles;
    out->nodes[ix].axis = node->axis;
    out->nodes[ix].activeIndex = node->activeIx;
    if (node->paneKind == PaneKind::Split) {
        for (int i = 0; i < node->children.len; i++) {
            int child = PaneNodeToState(node->children[i], source, out);
            VecAppend(out->nodes[ix].children, child);
            // The persisted schema has no optional size; upstream writes
            // zero for an unresolved slot. Resolve sizes before saving when
            // compatibility with readers predating the tree is required.
            float size = i < node->sizeKnown.len && node->sizeKnown[i]
                             ? node->sizes[i]
                             : 0;
            VecAppend(out->nodes[ix].sizes, size);
        }
    } else {
        bool tiles = node->paneKind == PaneKind::Tiles;
        int count = tiles ? node->tiles.len : node->panels.len;
        for (int i = 0; i < count; i++) {
            PanelId panel = tiles ? node->tiles[i].panel : node->panels[i];
            int child = out->NewNode(source.panelName
                                         ? source.panelName(source.data, panel)
                                         : StrL(""));
            if (source.dump)
                source.dump(source.data, panel, &out->nodes[child]);
            VecAppend(out->nodes[ix].children, child);
            if (tiles) {
                TileMeta meta;
                meta.bounds = node->tiles[i].bounds;
                meta.zIndex = node->tiles[i].zIndex;
                VecAppend(out->nodes[ix].metas, meta);
            }
        }
    }
    return ix;
}

int PaneTree::ToState(const PanelSource& source, DockAreaState* out) const {
    if (!root || !out) return -1;
    const PaneNode* persisted = root;
    // state_convert.rs::persisted_root: the Split wrapper is an in-memory
    // invariant. Older readers require a bare Tiles center on disk.
    if (rootKind == RootKind::Split && root->paneKind == PaneKind::Split &&
        root->children.len == 1 &&
        root->children[0]->paneKind == PaneKind::Tiles) {
        persisted = root->children[0];
    }
    return PaneNodeToState(persisted, source, out);
}

void DockAreaState::Clear() {
    for (int i = 0; i < nodes.len; i++) {
        VecReset(nodes[i].children);
        VecReset(nodes[i].sizes);
        VecReset(nodes[i].metas);
    }
    VecClear(nodes);
    hasVersion = false;
    version = 0;
    center = -1;
    left = DockSideState{};
    right = DockSideState{};
    bottom = DockSideState{};
}

int DockAreaState::NewNode(Str panelName) {
    PanelStateNode node;
    node.panelName = panelName;
    VecAppend(nodes, node);
    return nodes.len - 1;
}

// Bounds, the way GPUI writes one: an origin and a size, each a pair.
static Bounds ParseBounds(const JsonValue* v) {
    Bounds b = {};
    const JsonValue* origin = JsonGet(v, "origin");
    const JsonValue* size = JsonGet(v, "size");
    b.x = (float)JsonNumber(JsonGet(origin, "x"));
    b.y = (float)JsonNumber(JsonGet(origin, "y"));
    b.w = (float)JsonNumber(JsonGet(size, "width"));
    b.h = (float)JsonNumber(JsonGet(size, "height"));
    return b;
}

static void WriteBounds(JsonWriter* w, const char* key, Bounds b) {
    w->BeginObject(key);
    w->BeginObject("origin");
    w->Number("x", b.x);
    w->Number("y", b.y);
    w->EndObject();
    w->BeginObject("size");
    w->Number("width", b.w);
    w->Number("height", b.h);
    w->EndObject();
    w->EndObject();
}

// One node and everything under it. Answers the node's index, or -1.
static int ParseNode(Arena* a, const JsonValue* v, DockAreaState* out) {
    if (!v || v->kind != JsonKind::Object) {
        return -1;
    }
    int ix = out->NewNode(StrDup(a, JsonString(JsonGet(v, "panel_name"))));
    if (ix < 0) {
        return -1;
    }
    // The children are read before the info, so a node's own fields are
    // written after the recursion has finished with the array.
    const JsonValue* children = JsonGet(v, "children");
    Vec<int> childIx;
    for (const JsonValue* c = children ? children->first : nullptr; c;
         c = c->next) {
        int child = ParseNode(a, c, out);
        if (child >= 0) {
            VecAppend(childIx, child);
        }
    }
    // The recursion appended nodes of its own, so the reference is taken
    // after it has finished growing the pool.
    PanelStateNode& node = out->nodes[ix];
    for (int i = 0; i < childIx.len; i++) {
        VecAppend(node.children, childIx[i]);
    }
    VecReset(childIx);

    // PanelInfo is an externally tagged enum: one member, named for the kind.
    const JsonValue* info = JsonGet(v, "info");
    const JsonValue* stack = JsonGet(info, "stack");
    const JsonValue* tabs = JsonGet(info, "tabs");
    const JsonValue* tiles = JsonGet(info, "tiles");
    if (stack) {
        node.kind = PanelInfoKind::Stack;
        const JsonValue* sizes = JsonGet(stack, "sizes");
        for (const JsonValue* s = sizes ? sizes->first : nullptr; s;
             s = s->next) {
            VecAppend(node.sizes, (float)JsonNumber(s));
        }
        // 0 is horizontal and 1 is vertical, which is what Rust writes.
        node.axis = (int)JsonNumber(JsonGet(stack, "axis")) == 0
                        ? Axis::Horizontal
                        : Axis::Vertical;
    } else if (tabs) {
        node.kind = PanelInfoKind::Tabs;
        node.activeIndex = (int)JsonNumber(JsonGet(tabs, "active_index"));
    } else if (tiles) {
        node.kind = PanelInfoKind::Tiles;
        const JsonValue* metas = JsonGet(tiles, "metas");
        for (const JsonValue* m = metas ? metas->first : nullptr; m;
             m = m->next) {
            TileMeta meta;
            meta.bounds = ParseBounds(JsonGet(m, "bounds"));
            meta.zIndex = (int)JsonNumber(JsonGet(m, "z_index"));
            VecAppend(node.metas, meta);
        }
    } else {
        node.kind = PanelInfoKind::Panel;
        // Whatever the panel wrote is kept as it reads, so a round trip does
        // not lose what this port does not understand.
        const JsonValue* panel = JsonGet(info, "panel");
        if (panel && panel->kind == JsonKind::String) {
            node.info = StrDup(a, panel->str);
        } else if (panel && panel->kind != JsonKind::Null) {
            StrBuilder encoded;
            JsonWriter writer;
            writer.out = &encoded;
            writer.Value(nullptr, panel);
            Str text = encoded.TakeStr();
            node.info = StrDup(a, text);
            node.infoIsJson = true;
            StrFree(text);
        }
    }
    return ix;
}

static DockPlacement PlacementOf(Str s) {
    if (StrEqI(s, "left")) {
        return DockPlacement::Left;
    }
    if (StrEqI(s, "right")) {
        return DockPlacement::Right;
    }
    if (StrEqI(s, "bottom")) {
        return DockPlacement::Bottom;
    }
    return DockPlacement::Center;
}

static const char* PlacementName(DockPlacement p) {
    switch (p) {
        case DockPlacement::Left:
            return "left";
        case DockPlacement::Right:
            return "right";
        case DockPlacement::Bottom:
            return "bottom";
        default:
            return "center";
    }
}

static void ParseDock(Arena* a, const JsonValue* v, DockAreaState* out,
                      DockSideState* side, DockPlacement fallback) {
    if (!v || v->kind != JsonKind::Object) {
        return;
    }
    side->present = true;
    side->node = ParseNode(a, JsonGet(v, "panel"), out);
    const JsonValue* placement = JsonGet(v, "placement");
    side->placement = placement ? PlacementOf(JsonString(placement)) : fallback;
    side->size = (float)JsonNumber(JsonGet(v, "size"));
    side->open = JsonBool(JsonGet(v, "open"), true);
}

bool DockAreaStateParse(Arena* a, Str json, DockAreaState* out) {
    out->Clear();
    JsonValue* root = JsonParse(a, json);
    if (!root || root->kind != JsonKind::Object) {
        return false;
    }
    const JsonValue* version = JsonGet(root, "version");
    if (version && version->kind == JsonKind::Number) {
        out->hasVersion = true;
        out->version = (int)version->num;
    }
    out->center = ParseNode(a, JsonGet(root, "center"), out);
    ParseDock(a, JsonGet(root, "left_dock"), out, &out->left,
              DockPlacement::Left);
    ParseDock(a, JsonGet(root, "right_dock"), out, &out->right,
              DockPlacement::Right);
    ParseDock(a, JsonGet(root, "bottom_dock"), out, &out->bottom,
              DockPlacement::Bottom);
    return out->center >= 0;
}

static void WriteNode(JsonWriter* w, const char* key, const DockAreaState* s,
                      int ix) {
    if (ix < 0 || ix >= s->nodes.len) {
        w->Null(key);
        return;
    }
    const PanelStateNode& node = s->nodes[ix];
    w->BeginObject(key);
    w->String("panel_name", node.panelName);
    w->BeginArray("children");
    for (int i = 0; i < node.children.len; i++) {
        WriteNode(w, nullptr, s, node.children[i]);
    }
    w->EndArray();
    w->BeginObject("info");
    switch (node.kind) {
        case PanelInfoKind::Stack:
            w->BeginObject("stack");
            w->BeginArray("sizes");
            for (int i = 0; i < node.sizes.len; i++) {
                w->Number(nullptr, node.sizes[i]);
            }
            w->EndArray();
            w->Number("axis", AxisIsHorizontal(node.axis) ? 0 : 1);
            w->EndObject();
            break;
        case PanelInfoKind::Tabs:
            w->BeginObject("tabs");
            w->Number("active_index", node.activeIndex);
            w->EndObject();
            break;
        case PanelInfoKind::Tiles:
            w->BeginObject("tiles");
            w->BeginArray("metas");
            for (int i = 0; i < node.metas.len; i++) {
                w->BeginObject(nullptr);
                WriteBounds(w, "bounds", node.metas[i].bounds);
                w->Number("z_index", node.metas[i].zIndex);
                w->EndObject();
            }
            w->EndArray();
            w->EndObject();
            break;
        case PanelInfoKind::Panel:
        default:
            if (node.info.s && node.infoIsJson) {
                w->Raw("panel", node.info);
            } else if (node.info.s) {
                w->String("panel", node.info);
            } else {
                w->Null("panel");
            }
            break;
    }
    w->EndObject();
    w->EndObject();
}

static void WriteDock(JsonWriter* w, const char* key, const DockAreaState* s,
                      const DockSideState& side) {
    if (!side.present) {
        // serde skips a dock that is not there rather than writing a null.
        return;
    }
    w->BeginObject(key);
    WriteNode(w, "panel", s, side.node);
    w->String("placement", Str(PlacementName(side.placement)));
    w->Number("size", side.size);
    w->Bool("open", side.open);
    w->EndObject();
}

void DockAreaStateWrite(const DockAreaState* s, StrBuilder* out) {
    JsonWriter w;
    w.out = out;
    w.BeginObject(nullptr);
    if (s->hasVersion) {
        w.Number("version", s->version);
    }
    WriteNode(&w, "center", s, s->center);
    WriteDock(&w, "left_dock", s, s->left);
    WriteDock(&w, "right_dock", s, s->right);
    WriteDock(&w, "bottom_dock", s, s->bottom);
    w.EndObject();
}

// ─── the live tree and the saved one ──────────────────────────────────────

// One node of the live tree, written out. Answers the state node's index.
static int DumpNode(const DockState* s, DockAreaState* out, int node) {
    if (node < 0 || node >= s->nodes.len || !s->nodes[node].used) {
        return -1;
    }
    const DockNode& n = s->nodes[node];
    if (n.split) {
        int ix = out->NewNode(StrL("StackPanel"));
        if (ix < 0) {
            return -1;
        }
        Vec<int> children;
        Vec<float> sizes;
        for (int i = 0; i < n.child.len; i++) {
            int child = DumpNode(s, out, n.child[i]);
            if (child >= 0) {
                VecAppend(children, child);
                VecAppend(sizes, n.size[i]);
            }
        }
        // DumpNode appended nodes of its own, so the reference is taken
        // after the recursion has finished growing the pool.
        PanelStateNode& sn = out->nodes[ix];
        sn.kind = PanelInfoKind::Stack;
        sn.axis = n.axis;
        for (int i = 0; i < children.len; i++) {
            VecAppend(sn.children, children[i]);
            VecAppend(sn.sizes, sizes[i]);
        }
        VecReset(children);
        VecReset(sizes);
        return ix;
    }
    int ix = out->NewNode(StrL("TabPanel"));
    if (ix < 0) {
        return -1;
    }
    // A panel is a leaf under the name it was registered with, which is what
    // the registry is asked for when this is read back.
    Vec<int> children;
    for (int i = 0; i < n.panel.len; i++) {
        int panelIx = n.panel[i];
        if (panelIx < 0 || panelIx >= s->panels.len) {
            continue;
        }
        int leaf = out->NewNode(s->panels[panelIx].name);
        if (leaf < 0) {
            break;
        }
        out->nodes[leaf].kind = PanelInfoKind::Panel;
        if (s->panels[panelIx].dump) {
            s->panels[panelIx].dump(s->panels[panelIx].data, &out->nodes[leaf]);
        }
        VecAppend(children, leaf);
    }
    PanelStateNode& sn = out->nodes[ix];
    sn.kind = PanelInfoKind::Tabs;
    sn.activeIndex = n.activeIx < children.len ? n.activeIx : 0;
    for (int i = 0; i < children.len; i++) {
        VecAppend(sn.children, children[i]);
    }
    VecReset(children);
    return ix;
}

static void DumpSide(const DockState* s, DockAreaState* out,
                     const DockSide& side, DockPlacement placement,
                     DockSideState* to) {
    if (side.node < 0) {
        return;
    }
    to->node = DumpNode(s, out, side.node);
    if (to->node < 0) {
        return;
    }
    to->present = true;
    to->placement = placement;
    to->size = side.size;
    to->open = side.open;
}

void DockDump(const DockState* s, DockAreaState* out) {
    out->Clear();
    // DockArea::version, written back as it came: a layout that named one
    // keeps naming it, so whatever gates on it still can.
    out->hasVersion = s->hasVersion;
    out->version = s->version;
    out->center = DumpNode(s, out, s->center);
    DumpSide(s, out, s->left, DockPlacement::Left, &out->left);
    DumpSide(s, out, s->right, DockPlacement::Right, &out->right);
    DumpSide(s, out, s->bottom, DockPlacement::Bottom, &out->bottom);
}

// PanelRegistry::build_panel: the panel this name means, or an InvalidPanel
// registered on the spot so the layout keeps its shape and says what is
// missing from it.
static int PanelForName(DockState* s, const PanelStateNode* saved, Arena* a,
                        El* (*invalidRender)(Ctx* cx, void* data), App* app,
                        Window* win, Entity<DockState> dockArea) {
    Str name = saved ? saved->panelName : StrL("");
    int found = DockPanelByName(s, name);
    if (found >= 0) {
        return found;
    }
    if (app && saved) {
        PanelBuildContext context;
        context.dockArea = dockArea;
        context.state = saved;
        context.info = &saved->kind;
        DockPanelDef built;
        PanelRegistry* registry = PanelRegistryGlobal(app);
        if (registry && registry
                            ->BuildPanel(name, &context, win, app, &built)) {
            return DockAddPanelDef(s, built);
        }
    }
    DockPanelDef def;
    def.name = StrDup(a, name);
    def.title = def.name;
    def.render = invalidRender;
    auto* info = ArenaNew<DockInvalidPanel>(a);
    if (info) {
        info->name = def.name;
    }
    def.data = info;
    return DockAddPanelDef(s, def);
}

// One saved node, built into the live tree. Answers the live node, or -1.
static int LoadNode(DockState* s, const DockAreaState* st, int ix, Arena* a,
                    El* (*invalidRender)(Ctx* cx, void* data), App* app,
                    Window* win, Entity<DockState> dockArea) {
    if (ix < 0 || ix >= st->nodes.len) {
        return -1;
    }
    const PanelStateNode& sn = st->nodes[ix];
    if (sn.kind == PanelInfoKind::Stack) {
        int node = DockNewSplit(s, sn.axis);
        if (node < 0) {
            return -1;
        }
        for (int i = 0; i < sn.children.len; i++) {
            int child = LoadNode(s, st, sn.children[i], a, invalidRender, app,
                                 win, dockArea);
            if (child >= 0) {
                DockSplitAdd(
                    s, node, child,
                    i < sn.sizes.len ? sn.sizes[i] : kDockPanelMinSize);
            }
        }
        return node;
    }
    // Tabs, or a leaf panel on its own: either way the live tree wants a tab
    // group, since a panel here is never a node of its own.
    int node = DockNewTabs(s);
    if (node < 0) {
        return -1;
    }
    if (sn.kind == PanelInfoKind::Tabs) {
        for (int i = 0; i < sn.children.len; i++) {
            const PanelStateNode& leaf = st->nodes[sn.children[i]];
            // Rust flattens a tab group's children into their panels and
            // ignores anything that is not one.
            if (leaf.kind != PanelInfoKind::Panel) {
                continue;
            }
            int panelIx =
                PanelForName(s, &leaf, a, invalidRender, app, win, dockArea);
            if (panelIx >= 0) {
                DockTabsAdd(s, node, panelIx);
            }
        }
        s->nodes[node].activeIx =
            sn.activeIndex < s->nodes[node].panel.len ? sn.activeIndex : 0;
        return node;
    }
    int panelIx = PanelForName(s, &sn, a, invalidRender, app, win, dockArea);
    if (panelIx >= 0) {
        DockTabsAdd(s, node, panelIx);
    }
    return node;
}

static void LoadSide(DockState* s, const DockAreaState* st,
                     const DockSideState& from, Arena* a,
                     El* (*invalidRender)(Ctx* cx, void* data), DockSide* to,
                     App* app, Window* win, Entity<DockState> dockArea) {
    *to = DockSide{};
    if (!from.present) {
        to->node = -1;
        return;
    }
    to->node = LoadNode(s, st, from.node, a, invalidRender, app, win, dockArea);
    to->size = from.size > 0 ? from.size : to->size;
    to->open = from.open;
}

bool DockLoad(DockState* s, const DockAreaState* st, Arena* a,
              El* (*invalidRender)(Ctx* cx, void* data), App* app, Window* win,
              Entity<DockState> dockArea) {
    if (!s || !st || st->center < 0) {
        return false;
    }
    s->hasVersion = st->hasVersion;
    s->version = st->version;
    // The panels the host registered stay; the tree they were in does not.
    for (int i = 0; i < s->nodes.len; i++) {
        s->nodes[i] = DockNode{};
    }
    s->center = -1;
    s->zoomPanel = -1;
    s->dropNode = -1;
    s->menuNode = -1;
    s->left = DockSide{};
    s->right = DockSide{};
    s->bottom = DockSide{};
    s->left.node = -1;
    s->right.node = -1;
    s->bottom.node = -1;
    s->center =
        LoadNode(s, st, st->center, a, invalidRender, app, win, dockArea);
    LoadSide(s, st, st->left, a, invalidRender, &s->left, app, win, dockArea);
    LoadSide(s, st, st->right, a, invalidRender, &s->right, app, win, dockArea);
    LoadSide(s, st, st->bottom, a, invalidRender, &s->bottom, app, win,
             dockArea);
    // A file may hold any tree at all — an empty group, a split of one, a
    // split inside a split of the same axis — and the edits from here on
    // assume the canonical shape.
    DockNormalize(s);
    return s->center >= 0;
}

int TilesToMetas(const TilesState* s, TileMeta* out, int* outPanels, int cap) {
    int n = 0;
    for (int i = 0; i < s->items.len && n < cap; i++) {
        out[n].bounds = s->items[i].bounds;
        out[n].zIndex = s->items[i].zIndex;
        if (outPanels) {
            outPanels[n] = s->items[i].panel;
        }
        n++;
    }
    return n;
}

void TilesFromMetas(TilesState* s, const TileMeta* metas, const int* panels,
                    int n) {
    Vec<TileItem> rebuilt;
    int count = 0;
    for (int i = 0; i < n; i++) {
        VecAppend(rebuilt, TileItem{});
        int panel = panels ? panels[i] : i;
        // The tile showing that panel, wherever it has ended up in the list.
        int at = TilesIndexOfPanel(s, panel);
        rebuilt[count] = at >= 0 ? s->items[at] : TileItem{};
        rebuilt[count].panel = panel;
        rebuilt[count].bounds = metas[i].bounds;
        rebuilt[count].zIndex = metas[i].zIndex;
        count++;
    }
    // A tile the layout says nothing about keeps its place, after the ones it
    // does — the same as a panel the saved tree has no child for.
    for (int i = 0; i < s->items.len; i++) {
        bool saved = false;
        for (int k = 0; k < count; k++) {
            if (rebuilt[k].panel == s->items[i].panel) {
                saved = true;
                break;
            }
        }
        if (!saved) {
            VecAppend(rebuilt, s->items[i]);
            count++;
        }
    }
    VecClear(s->items);
    for (int i = 0; i < count; i++) {
        VecAppend(s->items, rebuilt[i]);
    }
    VecReset(rebuilt);
    s->dragging = -1;
    s->resizing = -1;
    s->side = TileSide::None;
}

} // namespace gpui
