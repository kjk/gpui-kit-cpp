/* TaffyTree — taffy/src/tree/taffy_tree.rs */

#include "taffy/compute.h"

#include <cstring>

namespace taffy {

using base::Str;

// ─── node slots ──────────────────────────────────────────────────────────
//
// Rust stores nodes in a slotmap and a NodeId is that slotmap's key. Here a
// NodeId is `(generation << 32) | (index + 1)`, so a zero NodeId is never a
// live node and a stale id does not resolve to whatever took its slot.

static NodeId MakeId(int32_t index, uint32_t generation) {
    return NodeId{((uint64_t)generation << 32) | (uint64_t)(index + 1)};
}

static int32_t IdIndex(NodeId id) {
    return (int32_t)((uint32_t)(id.raw & 0xffffffffu)) - 1;
}

static uint32_t IdGeneration(NodeId id) {
    return (uint32_t)(id.raw >> 32);
}

NodeData* TaffyTree::Get(NodeId node) const {
    int32_t idx = IdIndex(node);
    if (idx < 0 || idx >= slots.len) {
        return nullptr;
    }
    NodeData* d = slots[idx];
    if (!d || !d->alive || d->generation != IdGeneration(node)) {
        return nullptr;
    }
    return d;
}

void TaffyTree::Init(int capacity) {
    VecReset(slots);
    VecReset(freeSlots);
    liveCount = 0;
    allocs = 0;
    useRounding = true;
    if (capacity > 0) {
        base::VecReserve(slots, capacity);
    }
}

void TaffyTree::Free() {
    for (int i = 0; i < slots.len; i++) {
        NodeData* d = slots[i];
        if (!d) {
            continue;
        }
        VecReset(d->children);
        delete d;
    }
    VecReset(slots);
    VecReset(freeSlots);
    liveCount = 0;
}

// A slot for a new node, either recycled or appended.
static int32_t AllocSlot(TaffyTree* tree) {
    if (tree->freeSlots.len > 0) {
        int32_t idx = tree->freeSlots[tree->freeSlots.len - 1];
        tree->freeSlots.len--;
        return idx;
    }
    VecAppend(tree->slots, nullptr);
    return tree->slots.len - 1;
}

static NodeId InsertNode(TaffyTree* tree, const Style& style) {
    int32_t idx = AllocSlot(tree);
    NodeData* d = tree->slots[idx];
    uint32_t generation = 1;
    if (d) {
        generation = d->generation + 1;
    } else {
        d = new NodeData();
        tree->slots[idx] = d;
        tree->allocs++;
    }

    // Reset field by field rather than assigning a fresh NodeData: the
    // children array is a `Vec<NodeId>` whose assignment operator frees its
    // storage, and keeping that storage is the whole point of recycling a
    // slot. Its length going to zero is what empties it.
    d->style = style;
    d->unroundedLayout = Layout{};
    d->finalLayout = Layout{};
    d->hasContext = false;
    d->context = nullptr;
    // The present bits, not `Cache{}`: an entry is only read while its bit
    // is set, so dropping the bits empties the cache. Assigning a fresh one
    // wrote a few hundred bytes of zeroes per node per frame for contents
    // nothing may look at.
    d->cache.presentMask = 0;
    d->cache.isEmpty = true;
    d->generation = generation;
    d->alive = true;
    d->children.len = 0;
    d->parent = NodeId{};
    d->hasParent = false;

    tree->liveCount++;
    return MakeId(idx, generation);
}

// ─── building ────────────────────────────────────────────────────────────

NodeId TaffyTree::NewLeaf(const Style& style) {
    return InsertNode(this, style);
}

NodeId TaffyTree::NewLeafWithContext(const Style& style, void* context) {
    NodeId id = InsertNode(this, style);
    NodeData* d = Get(id);
    d->hasContext = true;
    d->context = context;
    return id;
}

NodeId TaffyTree::NewWithChildren(const Style& style, const NodeId* children,
                                  int n) {
    NodeId id = InsertNode(this, style);
    NodeData* d = Get(id);
    for (int i = 0; i < n; i++) {
        NodeData* c = Get(children[i]);
        if (!c) {
            continue;
        }
        c->parent = id;
        c->hasParent = true;
        VecAppend(d->children, children[i]);
    }
    return id;
}

void TaffyTree::Clear() {
    // Rebuild the free list from every allocated slot, alive or not. Appending
    // only the live ones left dead slots stranded when freeSlots had been
    // emptied, so the next InsertNode appended a fresh slot and `new`'d.
    VecReset(freeSlots);
    for (int i = 0; i < slots.len; i++) {
        NodeData* d = slots[i];
        if (!d) {
            continue;
        }
        d->alive = false;
        d->children.len = 0;
        d->hasParent = false;
        d->parent = NodeId{};
        VecAppend(freeSlots, (int32_t)i);
    }
    liveCount = 0;
}

void TaffyTree::Remove(NodeId node) {
    NodeData* d = Get(node);
    if (!d) {
        return;
    }
    if (d->hasParent) {
        NodeData* p = Get(d->parent);
        if (p) {
            int w = 0;
            for (int i = 0; i < p->children.len; i++) {
                if (p->children[i] != node) {
                    p->children[w++] = p->children[i];
                }
            }
            p->children.len = w;
        }
    }
    // Drop the "parent" back-reference from every child. Descendants stay
    // alive: the layout cache walks them with LayoutDropSubtree and gives
    // their records back. Killing them here put the same slot on freeSlots
    // twice (once from the walk, once from this recurse), and the second
    // InsertNode reused a still-parented node, leaving a stale id in the
    // tree and a fresh NodeData on every rebuild.
    for (int i = 0; i < d->children.len; i++) {
        NodeData* c = Get(d->children[i]);
        if (c) {
            c->hasParent = false;
        }
    }
    d->alive = false;
    d->children.len = 0;
    d->hasParent = false;
    liveCount--;
    VecAppend(freeSlots, IdIndex(node));
}

static void MarkReachable(const TaffyTree* tree, NodeId id, uint8_t* seen,
                          int nSlots) {
    NodeData* d = tree->Get(id);
    if (!d) {
        return;
    }
    int32_t idx = IdIndex(id);
    if (idx < 0 || idx >= nSlots || seen[idx]) {
        return;
    }
    seen[idx] = 1;
    for (int i = 0; i < d->children.len; i++) {
        MarkReachable(tree, d->children[i], seen, nSlots);
    }
}

void TaffyTree::EachUnreachable(NodeId root, void (*fn)(NodeId, void*),
                                void* user) {
    if (!fn || slots.len <= 0) {
        return;
    }
    uint8_t* seen = (uint8_t*)base::Alloc(nullptr, slots.len);
    if (!seen) {
        return;
    }
    memset(seen, 0, (size_t)slots.len);
    MarkReachable(this, root, seen, slots.len);
    Vec<NodeId> ids;
    for (int i = 0; i < slots.len; i++) {
        NodeData* d = slots[i];
        if (d && d->alive && !seen[i]) {
            VecAppend(ids, MakeId((int32_t)i, d->generation));
        }
    }
    base::Free(nullptr, seen);
    for (int i = 0; i < len(ids); i++) {
        fn(ids[i], user);
    }
    VecReset(ids);
}

void TaffyTree::SetNodeContext(NodeId node, void* context, bool hasContext) {
    NodeData* d = Get(node);
    if (!d) {
        return;
    }
    d->hasContext = hasContext;
    d->context = hasContext ? context : nullptr;
    MarkDirty(node);
}

void* TaffyTree::GetNodeContext(NodeId node) const {
    NodeData* d = Get(node);
    return d && d->hasContext ? d->context : nullptr;
}

void TaffyTree::AddChild(NodeId parent, NodeId child) {
    NodeData* p = Get(parent);
    NodeData* c = Get(child);
    if (!p || !c) {
        return;
    }
    c->parent = parent;
    c->hasParent = true;
    VecAppend(p->children, child);
    MarkDirty(parent);
}

bool TaffyTree::InsertChildAtIndex(NodeId parent, int childIndex,
                                   NodeId child) {
    NodeData* p = Get(parent);
    NodeData* c = Get(child);
    if (!p || !c) {
        return false;
    }
    if (childIndex > p->children.len || childIndex < 0) {
        return false;
    }
    c->parent = parent;
    c->hasParent = true;
    VecInsertAt(p->children, childIndex, child);
    MarkDirty(parent);
    return true;
}

void TaffyTree::SetChildren(NodeId parent, const NodeId* children, int n) {
    NodeData* p = Get(parent);
    if (!p) {
        return;
    }
    // Drop this node as parent from all its current children.
    for (int i = 0; i < p->children.len; i++) {
        NodeData* c = Get(p->children[i]);
        if (c) {
            c->hasParent = false;
        }
    }
    for (int i = 0; i < n; i++) {
        NodeData* c = Get(children[i]);
        if (!c) {
            continue;
        }
        // Detach from a previous parent first, so a node is never listed
        // twice.
        if (c->hasParent && c->parent != parent) {
            RemoveChild(c->parent, children[i]);
        }
        c->parent = parent;
        c->hasParent = true;
    }
    p->children.len = 0;
    for (int i = 0; i < n; i++) {
        VecAppend(p->children, children[i]);
    }
    MarkDirty(parent);
}

NodeId TaffyTree::RemoveChild(NodeId parent, NodeId child) {
    NodeData* p = Get(parent);
    if (!p) {
        return NodeId{};
    }
    for (int i = 0; i < p->children.len; i++) {
        if (p->children[i] == child) {
            return RemoveChildAtIndex(parent, i);
        }
    }
    return NodeId{};
}

NodeId TaffyTree::RemoveChildAtIndex(NodeId parent, int childIndex) {
    NodeData* p = Get(parent);
    if (!p || childIndex < 0 || childIndex >= p->children.len) {
        return NodeId{};
    }
    NodeId child = p->children[childIndex];
    for (int i = childIndex; i + 1 < p->children.len; i++) {
        p->children[i] = p->children[i + 1];
    }
    p->children.len--;
    NodeData* c = Get(child);
    if (c) {
        c->hasParent = false;
    }
    MarkDirty(parent);
    return child;
}

void TaffyTree::RemoveChildrenRange(NodeId parent, int start, int end) {
    NodeData* p = Get(parent);
    if (!p) {
        return;
    }
    if (start < 0) {
        start = 0;
    }
    if (end > p->children.len) {
        end = p->children.len;
    }
    if (end <= start) {
        return;
    }
    for (int i = start; i < end; i++) {
        NodeData* c = Get(p->children[i]);
        if (c) {
            c->hasParent = false;
        }
    }
    int n = end - start;
    for (int i = end; i < p->children.len; i++) {
        p->children[i - n] = p->children[i];
    }
    p->children.len -= n;
    MarkDirty(parent);
}

NodeId TaffyTree::ReplaceChildAtIndex(NodeId parent, int childIndex,
                                      NodeId newChild) {
    NodeData* p = Get(parent);
    if (!p || childIndex < 0 || childIndex >= p->children.len) {
        return NodeId{};
    }
    NodeData* nc = Get(newChild);
    if (nc) {
        nc->parent = parent;
        nc->hasParent = true;
    }
    NodeId oldChild = p->children[childIndex];
    p->children[childIndex] = newChild;
    NodeData* oc = Get(oldChild);
    if (oc) {
        oc->hasParent = false;
    }
    MarkDirty(parent);
    return oldChild;
}

// ─── reading ─────────────────────────────────────────────────────────────

NodeId TaffyTree::ChildAtIndex(NodeId parent, int childIndex) const {
    NodeData* p = Get(parent);
    if (!p || childIndex < 0 || childIndex >= p->children.len) {
        return NodeId{};
    }
    return p->children[childIndex];
}

NodeId TaffyTree::Parent(NodeId child, bool* hasParent) const {
    NodeData* c = Get(child);
    if (!c || !c->hasParent) {
        *hasParent = false;
        return NodeId{};
    }
    *hasParent = true;
    return c->parent;
}

void TaffyTree::SetStyle(NodeId node, const Style& style) {
    NodeData* d = Get(node);
    if (!d) {
        return;
    }
    d->style = style;
    MarkDirty(node);
}

// Every caller has a live node by contract, the way Rust's indexing does.
// A stale id would be a bug in the caller, so this returns the default style
// rather than reading through null.
static const Style& DefaultStyle() {
    static const Style kDefault;
    return kDefault;
}

const Style& TaffyTree::GetStyle(NodeId node) const {
    NodeData* d = Get(node);
    return d ? d->style : DefaultStyle();
}

static const Layout& DefaultLayout() {
    static const Layout kDefault;
    return kDefault;
}

const Layout& TaffyTree::GetLayout(NodeId node) const {
    NodeData* d = Get(node);
    if (!d) {
        return DefaultLayout();
    }
    return useRounding ? d->finalLayout : d->unroundedLayout;
}

const Layout& TaffyTree::UnroundedLayout(NodeId node) const {
    NodeData* d = Get(node);
    return d ? d->unroundedLayout : DefaultLayout();
}

void TaffyTree::MarkDirty(NodeId node) {
    NodeId cur = node;
    while (true) {
        NodeData* d = Get(cur);
        if (!d) {
            return;
        }
        // An already-dirty node means its ancestors are dirty too.
        if (!d->cache.Clear()) {
            return;
        }
        if (!d->hasParent) {
            return;
        }
        cur = d->parent;
    }
}

bool TaffyTree::Dirty(NodeId node) const {
    NodeData* d = Get(node);
    return d ? d->cache.IsEmpty() : true;
}

// ─── the trait surface ───────────────────────────────────────────────────

int TaffyTree::ChildCount(NodeId parent) const {
    NodeData* d = Get(parent);
    return d ? d->children.len : 0;
}

NodeId TaffyTree::GetChildId(NodeId parent, int index) const {
    NodeData* d = Get(parent);
    return d && index >= 0 && index < d->children.len ? d->children[index]
                                                      : NodeId{};
}

void TaffyTree::SetUnroundedLayout(NodeId node, const Layout& layout) {
    NodeData* d = Get(node);
    if (d) {
        d->unroundedLayout = layout;
    }
}

Layout TaffyTree::GetUnroundedLayout(NodeId node) const {
    NodeData* d = Get(node);
    return d ? d->unroundedLayout : Layout{};
}

void TaffyTree::SetFinalLayout(NodeId node, const Layout& layout) {
    NodeData* d = Get(node);
    if (d) {
        d->finalLayout = layout;
    }
}

Layout TaffyTree::GetFinalLayout(NodeId node) const {
    NodeData* d = Get(node);
    if (!d) {
        return Layout{};
    }
    return useRounding ? d->finalLayout : d->unroundedLayout;
}

const char* TaffyTree::GetDebugLabel(NodeId node) const {
    NodeData* d = Get(node);
    if (!d) {
        return "NONE";
    }
    if (d->style.display == Display::None) {
        return "NONE";
    }
    if (d->children.len == 0) {
        return "LEAF";
    }
    switch (d->style.display) {
        case Display::Block:
            return "BLOCK";
        case Display::FlowRoot:
            return "FLOW-ROOT";
        case Display::Grid:
            return "GRID";
        default:
            return IsRow(d->style.flexDirection) ? "FLEX ROW" : "FLEX COL";
    }
}

bool TaffyTree::CacheGet(NodeId node, const LayoutInput& input,
                         LayoutOutput* out) const {
    NodeData* d = Get(node);
    return d ? d->cache.Get(input, out) : false;
}

void TaffyTree::CacheStore(NodeId node, const LayoutInput& input,
                           const LayoutOutput& output) {
    NodeData* d = Get(node);
    if (d) {
        d->cache.Store(input, output);
    }
}

void TaffyTree::CacheClear(NodeId node) {
    NodeData* d = Get(node);
    if (d) {
        d->cache.Clear();
    }
}

// ─── compute_child_layout ────────────────────────────────────────────────

// What the leaf measure closure would have captured in Rust.
struct LeafMeasureCtx {
    TaffyTree* tree;
    NodeId node;
    void* nodeContext;
    const Style* style;
};

static SizeF LeafMeasureThunk(SizeFOpt knownDimensions,
                              SizeAvail availableSpace, void* ctx) {
    LeafMeasureCtx* c = (LeafMeasureCtx*)ctx;
    if (!c->tree->measureFn) {
        return SizeF::Zero();
    }
    return c->tree
        ->measureFn(knownDimensions, availableSpace, c->node, c->nodeContext,
                    c->style, c->tree->measureUserData);
}

LayoutOutput TaffyTree::ComputeBlockChildLayout(NodeId node, LayoutInput inputs,
                                                BlockContext* blockCtx) {
    // PerformHiddenLayout means an ancestor is display:none, so this node is
    // laid out hidden whatever its own display style says.
    if (inputs.runMode == RunMode::PerformHiddenLayout) {
        return ComputeHiddenLayout(this, node);
    }

    // Rust wraps the rest in `compute_cached_layout`, which has this one
    // caller; the cache check is inline here instead of behind a closure. The
    // node and the cache key are looked up once and used by both ends of it:
    // going through TaffyTree::CacheGet / CacheStore resolved the slot twice
    // and built the key twice, and this is the hottest function in a layout.
    NodeData* d = Get(node);
    if (!d) {
        return LayoutOutput::Hidden();
    }
    CacheKey key = CacheKey::From(inputs);
    LayoutOutput cached;
    if (d->cache.GetWithKey(key, inputs.runMode, &cached)) {
        return cached;
    }

    Display displayMode = d->style.display;
    bool hasChildren = d->children.len > 0;

    LayoutOutput out;
    if (displayMode == Display::None) {
        out = ComputeHiddenLayout(this, node);
    } else if (!hasChildren) {
        LeafMeasureCtx ctx = {this, node, d->hasContext ? d->context : nullptr,
                              &d->style};
        out = ComputeLeafLayout(inputs, d->style, calc, LeafMeasureThunk, &ctx);
    } else {
        switch (displayMode) {
            case Display::Block:
                out = ComputeBlockLayout(this, node, inputs, blockCtx);
                break;
            case Display::FlowRoot:
                out = ComputeBlockLayout(this, node, inputs, nullptr);
                break;
            case Display::Grid:
                out = ComputeGridLayout(this, node, inputs);
                break;
            default:
                out = ComputeFlexboxLayout(this, node, inputs);
                break;
        }
    }

    d->cache.StoreWithKey(key, inputs, out);
    return out;
}

LayoutOutput TaffyTree::ComputeChildLayout(NodeId node, LayoutInput inputs) {
    return ComputeBlockChildLayout(node, inputs, nullptr);
}

float TaffyTree::MeasureChildSize(NodeId node, SizeFOpt knownDimensions,
                                  SizeFOpt parentSize, SizeAvail availableSpace,
                                  SizingMode sizingMode, AbsoluteAxis axis,
                                  LineBool verticalMarginsAreCollapsible) {
    LayoutInput in;
    in.knownDimensions = knownDimensions;
    in.parentSize = parentSize;
    in.availableSpace = availableSpace;
    in.sizingMode = sizingMode;
    in.axis = ToRequestedAxis(axis);
    in.runMode = RunMode::ComputeSize;
    in.verticalMarginsAreCollapsible = verticalMarginsAreCollapsible;
    return GetAbs(ComputeChildLayout(node, in).size, axis);
}

SizeF TaffyTree::MeasureChildSizeBoth(NodeId node, SizeFOpt knownDimensions,
                                      SizeFOpt parentSize,
                                      SizeAvail availableSpace,
                                      SizingMode sizingMode,
                                      LineBool verticalMarginsAreCollapsible) {
    LayoutInput in;
    in.knownDimensions = knownDimensions;
    in.parentSize = parentSize;
    in.availableSpace = availableSpace;
    in.sizingMode = sizingMode;
    in.axis = RequestedAxis::Both;
    in.runMode = RunMode::ComputeSize;
    in.verticalMarginsAreCollapsible = verticalMarginsAreCollapsible;
    return ComputeChildLayout(node, in).size;
}

LayoutOutput TaffyTree::PerformChildLayout(
    NodeId node, SizeFOpt knownDimensions, SizeFOpt parentSize,
    SizeAvail availableSpace, SizingMode sizingMode,
    LineBool verticalMarginsAreCollapsible) {
    LayoutInput in;
    in.knownDimensions = knownDimensions;
    in.parentSize = parentSize;
    in.availableSpace = availableSpace;
    in.sizingMode = sizingMode;
    in.axis = RequestedAxis::Both;
    in.runMode = RunMode::PerformLayout;
    in.verticalMarginsAreCollapsible = verticalMarginsAreCollapsible;
    return ComputeChildLayout(node, in);
}

// ─── running layout ──────────────────────────────────────────────────────

void TaffyTree::ComputeLayoutWithMeasure(NodeId node, SizeAvail availableSpace,
                                         MeasureFn measure, void* userData) {
    measureFn = measure;
    measureUserData = userData;
    ComputeRootLayout(this, node, availableSpace);
    if (useRounding) {
        RoundLayout(this, node);
    }
    measureFn = nullptr;
    measureUserData = nullptr;
}

void TaffyTree::ComputeLayout(NodeId node, SizeAvail availableSpace) {
    ComputeLayoutWithMeasure(node, availableSpace, nullptr, nullptr);
}

// ─── debug print — taffy/src/util/print.rs ───────────────────────────────

static void PrintNode(TaffyTree* tree, NodeId node, bool hasSibling,
                      const char* linesString, int depth) {
    Layout layout = tree->GetFinalLayout(node);
    const char* displayStr = tree->GetDebugLabel(node);
    const char* fork = hasSibling ? "├── " : "└── ";

    // The formatter takes at most 32 directives per call and rejects a bare
    // `const char*`, so the line is built in two halves out of `Str`s.
    base::log(base::fmt(
        "%s%s%s [x: %-4g y: %-4g w: %-4g h: %-4g "
        "content_w: %-4g content_h: %-4g",
        Str(linesString), Str(fork), Str(displayStr), (double)layout.location.x,
        (double)layout.location.y, (double)layout.size.w, (double)layout.size.h,
        (double)layout.contentSize.w, (double)layout.contentSize.h));
    base::log(
        base::fmt(" border: l:%g r:%g t:%g b:%g, "
                  "padding: l:%g r:%g t:%g b:%g] (%llu)\n",
                  (double)layout.border.left, (double)layout.border.right,
                  (double)layout.border.top, (double)layout.border.bottom,
                  (double)layout.padding.left, (double)layout.padding.right,
                  (double)layout.padding.top, (double)layout.padding.bottom,
                  (unsigned long long)node.raw));

    // The dump indents with a box-drawing prefix per level. Beyond this depth
    // the prefix stops growing; Rust builds a String and has no bound.
    constexpr int kMaxDepth = 32;
    if (depth >= kMaxDepth) {
        return;
    }
    const char* bar = hasSibling ? "│   " : "    ";
    base::TempStr lines = base::fmt("%s%s", Str(linesString), Str(bar));

    int count = tree->ChildCount(node);
    for (int i = 0; i < count; i++) {
        PrintNode(tree, tree->GetChildId(node, i), i < count - 1, lines.s,
                  depth + 1);
    }
}

void TaffyTree::PrintTree(NodeId root) {
    base::log(StrL("TREE\n"));
    PrintNode(this, root, false, "", 0);
}

} // namespace taffy
