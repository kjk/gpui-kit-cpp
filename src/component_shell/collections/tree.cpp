// crates/component-shell/src/shell/collections/tree.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "base/tree.h"
#include "ui/icon.h"
#include "ui/list.h"

namespace gpui::component_shell::collections::tree {

struct ItemPayload {
    Str id;
    Str label;
};

// ItemOp. `disabled` is the shell's common behavior here, which the request
// carries as `disabled` rather than as a recorded op.
struct ItemOp {
    bool expanded = false;
};

struct TreePayload {
    Str id;
};

// TreeItem: Rust's nests its children and shares its expanded flag through
// an Rc. What a TreeItem element carries is this frame-arena node; the Tree
// flattens the nodes into the TreeState's parent-indexed item array.
struct Node {
    Str id;
    Str label;
    bool expanded = false;
    bool disabled = false;
    Node** children = nullptr;
    int count = 0;
};

// ItemFingerprint: the incoming data, flattened in pre-order with its depth,
// which is the nested structure without a nested type — and, being fields
// rather than a joined string, with no delimiter to collide on.
struct Fingerprint {
    Str id;
    Str label;
    bool expanded = false;
    bool disabled = false;
    int depth = 0;
};

// collections.rs test_probe, widened into a seam: see families.h.
static TreeRowProbe gRowProbe = nullptr;

// require_tree_item and require_item_style.
static bool RequireTreeItem(MaterializeRequest* request, const char* parent,
                            const char* actual) {
    static constexpr const char* kAllowed[] = {"TreeItem"};
    return RequireChild(request, parent, actual, kAllowed);
}

static El* MaterializeItem(MaterializeRequest* request) {
    const ItemPayload* payload = request->PayloadAs<ItemPayload>();
    if (!payload) return request->Fail(StrL("TreeItem incompatible payload"));
    Ctx* cx = request->cx;
    Node* node = ArenaNew<Node>(cx->a);
    node->id = payload->id;
    node->label = payload->label;
    EachMethod<ItemOp>(request,
                       [&](const ItemOp& op) { node->expanded = op.expanded; });
    node->disabled = request->disabled;
    request->styleTaken = true;
    if (request->HasStyle())
        return request
            ->Fail(StrL("TreeItem is data and does not support shell style"));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    node->children =
        count ? (Node**)Alloc(cx->a, (int)sizeof(Node*) * count) : nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireTreeItem(request, "TreeItem", children[i].componentName))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        Node* child = TakeCarriedAs<Node>(request, element, "TreeItem");
        if (!child) return nullptr;
        node->children[node->count++] = child;
    }
    return CarrierOf(cx, node);
}

// validate_unique_ids.
static const Node* FindDuplicate(Node* const* nodes, int count, Str* seen,
                                 int* nSeen) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < *nSeen; j++) {
            if (StrEq(seen[j], nodes[i]->id)) return nodes[i];
        }
        seen[(*nSeen)++] = nodes[i]->id;
        if (const Node* found =
                FindDuplicate(nodes[i]->children, nodes[i]->count, seen, nSeen))
            return found;
    }
    return nullptr;
}

static int CountNodes(Node* const* nodes, int count) {
    int total = count;
    for (int i = 0; i < count; i++)
        total += CountNodes(nodes[i]->children, nodes[i]->count);
    return total;
}

// The nodes as TreeState items (parent-indexed, pre-order) and as their
// fingerprint, which is the same walk.
static void Flatten(Node* const* nodes, int count, int parent, int depth,
                    TreeItem* items, Fingerprint* prints, int* at) {
    for (int i = 0; i < count; i++) {
        const Node* node = nodes[i];
        int ix = (*at)++;
        TreeItem& item = items[ix];
        item = TreeItem{};
        item.id = node->id;
        item.label = node->label;
        item.parent = parent;
        item.depth = depth;
        item.folder = node->count > 0;
        item.expanded = node->expanded;
        item.disabled = node->disabled;
        prints[ix] = {node->id, node->label, node->expanded, node->disabled,
                      depth};
        Flatten(node->children, node->count, ix, depth + 1, items, prints, at);
    }
}

// The retained tree: the native state and the fingerprint of the data it
// was last given. Rust also keeps the incoming roots, whose shared expanded
// flags are the native expansion; here the native state's items are.
struct Host {
    App* app = nullptr;
    Entity<TreeState> native = {};
    Arena* arena = nullptr;
    Vec<Fingerprint> fingerprint;

    ~Host() {
        if (app && native.IsValid()) EntityDrop(app, native.id);
        VecReset(fingerprint);
        if (arena) ArenaDelete(arena);
    }

    bool Matches(const Fingerprint* prints, int count) const {
        if (len(fingerprint) != count) return false;
        for (int i = 0; i < count; i++) {
            const Fingerprint& a = fingerprint[i];
            const Fingerprint& b = prints[i];
            if (!StrEq(a.id, b.id) || !StrEq(a.label, b.label) ||
                a.expanded != b.expanded || a.disabled != b.disabled ||
                a.depth != b.depth)
                return false;
        }
        return true;
    }

    void Remember(const Fingerprint* prints, int count) {
        Arena* fresh = ArenaNew();
        VecClear(fingerprint);
        for (int i = 0; i < count; i++) {
            Fingerprint print = prints[i];
            print.id = StrDup(fresh, prints[i].id);
            print.label = StrDup(fresh, prints[i].label);
            VecAppend(fingerprint, print);
        }
        if (arena) ArenaDelete(arena);
        arena = fresh;
    }
};

// preserve_expansion: an incoming item whose id the native tree already has
// takes the native expansion.
static void PreserveExpansion(TreeItem* incoming, int count,
                              const TreeState* previous) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < len(previous->items); j++) {
            if (StrEq(previous->items[j].id, incoming[i].id)) {
                incoming[i].expanded = previous->items[j].expanded;
                break;
            }
        }
    }
}

// Tree::new(&state, |ix, entry, selected, ..| ListItem ..).
static El* Row(void*, Ctx* cx, int, const TreeEntry& entry,
               TreeEntryState state) {
    const TreeItem* item = entry.item;
    if (!item) return nullptr;
    Arena* a = cx->a;
    bool selected = state.IsSelected();
    if (gRowProbe) gRowProbe(item->id, item->label, selected);
    IconName icon = !entry.IsFolder()    ? IconName::File
                    : entry.IsExpanded() ? IconName::FolderOpen
                                         : IconName::Folder;
    El* content = Div(a)
                      ->FlexRow()
                      ->ItemsCenter()
                      ->Gap(8)
                      ->Child(IconEl(a, icon, 16))
                      ->Child(TextEl(a, item->label));
    return component::ListItem::New(cx, content)
        ->Selected(selected)
        ->IntoEl(item->id, {}, {})
        ->W(kFill)
        ->PadX(12)
        ->PadL(16.f * (float)entry.depth + 12.f);
}

static El* MaterializeTree(MaterializeRequest* request) {
    const TreePayload* payload = request->PayloadAs<TreePayload>();
    if (!payload) return request->Fail(StrL("Tree incompatible payload"));
    Ctx* cx = request->cx;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    Node** roots =
        count ? (Node**)Alloc(cx->a, (int)sizeof(Node*) * count) : nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireTreeItem(request, "Tree", children[i].componentName))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        roots[i] = TakeCarriedAs<Node>(request, element, "TreeItem");
        if (!roots[i]) return nullptr;
    }
    int total = CountNodes(roots, count);
    Str* seen = total ? (Str*)Alloc(cx->a, (int)sizeof(Str) * total) : nullptr;
    int nSeen = 0;
    if (const Node* duplicate = FindDuplicate(roots, count, seen, &nSeen)) {
        return request
            ->Fail(fmt("TreeItem id `%s` is duplicated; ids must be "
                       "unique within a Tree",
                       duplicate->id));
    }
    TreeItem* items =
        total ? (TreeItem*)Alloc(cx->a, (int)sizeof(TreeItem) * total)
              : nullptr;
    Fingerprint* prints =
        total ? (Fingerprint*)Alloc(cx->a, (int)sizeof(Fingerprint) * total)
              : nullptr;
    int at = 0;
    Flatten(roots, count, -1, 0, items, prints, &at);

    Str key = StrDup(cx->a, fmt("shell-tree:%s", payload->id));
    Host* host = UseKeyedState<Host>(cx, key, StrL("shell-tree")).Get(cx);
    if (!host) return Div(cx->a);
    if (!host->native.IsValid()) {
        host->app = cx->app;
        host->native = EntityNewState<TreeState>(cx->app);
        TreeSetItems(host->native.Get(cx), nullptr, items, total);
        host->Remember(prints, total);
    } else if (!host->Matches(prints, total)) {
        TreeState* native = host->native.Get(cx);
        if (native) {
            PreserveExpansion(items, total, native);
            const TreeItem* selected = TreeEntryItem(native, native->selected);
            Str selectedId = selected ? StrDup(cx->a, selected->id) : Str{};
            TreeSetItems(native, nullptr, items, total);
            native->selected =
                selectedId.s ? TreeIndexOf(native, selectedId) : -1;
        }
        host->Remember(prints, total);
    }
    // The tree builds the rows its viewport can show, so it needs the
    // viewport's height as a number before layout, where GPUI's uniform_list
    // reads its bounds at prepaint. The number is the height the tree was
    // laid out at last frame (UseLaidOutHeight); the first frame takes a
    // definite height from the script's style, or 320. The style then
    // refines the root, which is `size_full()` beneath it as in Rust, so the
    // box is laid out by its parent and the script rather than by the number.
    ElRefiner style = request->TakeStyle();
    float h = 320;
    if (style.IsSet()) {
        El* probe = Div(cx->a);
        style.Apply(probe);
        if (probe->style.height > 0 && probe->style.height != kAuto)
            h = probe->style.height;
    }
    LaidOutHeight* laid = UseLaidOutHeight(cx, payload->id, h);
    if (laid) h = laid->built;
    El* root = TreeList::New(cx, payload->id, host->native, h, &Row, nullptr);
    root->H(kFill);
    style.Apply(root);
    TrackLaidOutHeight(cx, root, laid);
    return root;
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool ConstructItem(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::String ||
        len(StrTrimAscii(args[0].string)) == 0 ||
        len(StrTrimAscii(args[1].string)) == 0)
        return build->Fail(StrL("TreeItem expects non-empty id and label"));
    ItemPayload* payload = build->New<ItemPayload>();
    payload->id = args[0].string;
    payload->label = args[1].string;
    return true;
}

// bool_method("TreeItem", "expanded", ..).
static bool RecordExpanded(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("TreeItem.expanded expects one boolean"));
    build->New<ItemOp>()->expanded = args[0].boolean;
    return true;
}

static bool ConstructTree(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrimAscii(args[0].string)) == 0)
        return build->Fail(StrL("Tree expects non-empty id"));
    build->New<TreePayload>()->id = args[0].string;
    return true;
}

static constexpr ArgumentDescriptor kItemArgs[] = {{"id", SchemaString()},
                                                   {"label", SchemaString()}};
static constexpr ArgumentDescriptor kExpandedArgs[] = {
    {"expanded", SchemaBoolean()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kTreeArgs[] = {{"id", SchemaString()}};

static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"TreeItem", kItemArgs, &ConstructItem}};
static constexpr MethodDescriptor kItemMethods[] = {
    {"expanded", kExpandedArgs, "Sets native tree item state.",
     &RecordExpanded},
    {"disabled", kDisabledArgs, "Sets native tree item state.",
     &RecordCommonBehavior},
};
static constexpr ComponentDescriptor kTreeItem = {
    "TreeItem", kItemConstructors, kItemMethods,
    "Typed native tree data item with a Tree-wide unique id, nested TreeItem "
    "children, and initial expanded/disabled state; style is rejected.",
    &MaterializeItem};

static constexpr ConstructorDescriptor kTreeConstructors[] = {
    {"Tree", kTreeArgs, &ConstructTree}};
static constexpr ComponentDescriptor kTree = {
    "Tree",
    kTreeConstructors,
    {},
    "Native retained tree keyed only by a stable id that must be unique among "
    "Trees in the same window; label/structure/disabled data syncs by unique "
    "item id while native expansion, selection, focus, and scroll state "
    "persist.",
    &MaterializeTree};

} // namespace gpui::component_shell::collections::tree

namespace gpui::component_shell {

void SetTreeRowProbe(TreeRowProbe probe) {
    collections::tree::gRowProbe = probe;
}

bool RegisterCollectionsTree(shell::ComponentRegistry* registry,
                             shell::RegistryError* error) {
    return registry->Register(&collections::tree::kTreeItem, error) &&
           registry->Register(&collections::tree::kTree, error);
}

} // namespace gpui::component_shell
