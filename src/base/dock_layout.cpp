#include "base/dock_layout.h"
#include "base/dock_state.h"

namespace gpui {

static uint64_t gNextPaneNodeId = 1;

static const char kTabPanelName[] = "TabPanel";

PaneNode* PaneNode::Split(NodeId id, Axis axis) {
    PaneNode* node = new PaneNode();
    node->nodeId = id;
    node->paneKind = PaneKind::Split;
    node->axis = axis;
    return node;
}

PaneNode* PaneNode::Tabs(NodeId id) {
    PaneNode* node = new PaneNode();
    node->nodeId = id;
    node->paneKind = PaneKind::Tabs;
    return node;
}

PaneRef PaneNode::Kind() const {
    PaneRef ref;
    ref.kind = paneKind;
    ref.axis = axis;
    ref.children = &children;
    ref.sizes = &sizes;
    ref.sizeKnown = &sizeKnown;
    ref.panels = &panels;
    ref.activeIx = activeIx;
    return ref;
}

void PaneNode::Walk(Func1<const PaneNode*> visit) const {
    if (visit.IsValid()) {
        visit.Call(this);
    }
    if (paneKind == PaneKind::Split) {
        for (int i = 0; i < children.len; i++) {
            children[i]->Walk(visit);
        }
    }
}

bool PaneNode::Empty() const {
    if (paneKind == PaneKind::Split) {
        return children.len == 0;
    }
    return panels.len == 0;
}

PaneNode::~PaneNode() {
    for (int i = 0; i < children.len; i++) {
        delete children[i];
    }
    VecReset(children);
    VecReset(sizes);
    VecReset(sizeKnown);
    VecReset(panels);
}

InsertTarget InsertTarget::Tabs(NodeId node, int ix, bool activate) {
    InsertTarget target;
    target.kind = InsertTargetKind::Tabs;
    target.node = node;
    target.ix = ix;
    target.activate = activate;
    return target;
}

InsertTarget InsertTarget::Split(NodeId node, Placement placement,
                                 const float* size) {
    InsertTarget target;
    target.kind = InsertTargetKind::Split;
    target.node = node;
    target.placement = placement;
    target.hasSize = size != nullptr;
    target.size = size ? *size : 0;
    return target;
}

PaneTree::PaneTree(RootKind kind) {
    rootKind = kind;
    root = PaneNode::Split(AllocateNodeId(), Axis::Horizontal);
}

PaneTree::~PaneTree() {
    delete root;
}

NodeId PaneTree::AllocateNodeId() {
    NodeId id = NodeId::FromU64(gNextPaneNodeId++);
    if (id.value == 0) {
        id = NodeId::FromU64(gNextPaneNodeId++);
    }
    return id;
}

static PaneNode* FindNodeRec(PaneNode* node, NodeId id) {
    if (!node) {
        return nullptr;
    }
    if (node->nodeId == id) {
        return node;
    }
    if (node->paneKind == PaneKind::Split) {
        for (int i = 0; i < node->children.len; i++) {
            if (PaneNode* found = FindNodeRec(node->children[i], id)) {
                return found;
            }
        }
    }
    return nullptr;
}

PaneNode* PaneTree::FindNode(NodeId id) {
    return FindNodeRec(root, id);
}

const PaneNode* PaneTree::FindNode(NodeId id) const {
    return FindNodeRec(root, id);
}

static bool FindPanelNodeRec(const PaneNode* node, PanelId panel, NodeId* out) {
    if (!node) {
        return false;
    }
    if (node->paneKind == PaneKind::Tabs) {
        for (int i = 0; i < node->panels.len; i++) {
            if (node->panels[i] == panel) {
                if (out) {
                    *out = node->nodeId;
                }
                return true;
            }
        }
    } else {
        for (int i = 0; i < node->children.len; i++) {
            if (FindPanelNodeRec(node->children[i], panel, out)) {
                return true;
            }
        }
    }
    return false;
}

bool PaneTree::FindPanelNode(PanelId panel, NodeId* out) const {
    return FindPanelNodeRec(root, panel, out);
}

bool PaneTree::ContainsPanel(PanelId panel) const {
    return FindPanelNode(panel, nullptr);
}

static void CollectNodeIds(Vec<NodeId>* out, const PaneNode* node) {
    VecAppend(*out, node->nodeId);
}

void PaneTree::NodeIds(Vec<NodeId>* out) const {
    if (out && root) {
        root->Walk(MkFunc1(CollectNodeIds, out));
    }
}

static void CollectPanelsRec(const PaneNode* node, Vec<PanelId>* out) {
    if (node->paneKind == PaneKind::Tabs) {
        for (int i = 0; i < node->panels.len; i++) {
            VecAppend(*out, node->panels[i]);
        }
    } else {
        for (int i = 0; i < node->children.len; i++) {
            CollectPanelsRec(node->children[i], out);
        }
    }
}

void PaneTree::Panels(Vec<PanelId>* out) const {
    if (out && root) {
        CollectPanelsRec(root, out);
    }
}

static void SetRoot(PaneTree* tree, PaneNode* node) {
    delete tree->root;
    tree->root = node;
}

NodeId PaneTree::SetRootSplit(Axis axis) {
    PaneNode* node = PaneNode::Split(AllocateNodeId(), axis);
    SetRoot(this, node);
    return node->nodeId;
}

NodeId PaneTree::SetRootTabs(const PanelId* values, int count, int active) {
    PaneNode* node = PaneNode::Tabs(AllocateNodeId());
    for (int i = 0; values && i < count; i++) {
        VecAppend(node->panels, values[i]);
    }
    node->activeIx = active;
    SetRoot(this, node);
    return node->nodeId;
}

static bool AppendChild(PaneNode* parent, PaneNode* child, const float* size) {
    if (!parent || parent->paneKind != PaneKind::Split || !child) {
        return false;
    }
    VecAppend(parent->children, child);
    VecAppend(parent->sizes, size ? *size : 0);
    VecAppend(parent->sizeKnown, size ? 1 : 0);
    return true;
}

NodeId PaneTree::AddSplit(NodeId parent, Axis axis, const float* size) {
    PaneNode* child = PaneNode::Split(AllocateNodeId(), axis);
    if (!AppendChild(FindNode(parent), child, size)) {
        delete child;
        return {};
    }
    return child->nodeId;
}

NodeId PaneTree::AddTabs(NodeId parent, const PanelId* values, int count,
                         const float* size) {
    PaneNode* child = PaneNode::Tabs(AllocateNodeId());
    for (int i = 0; values && i < count; i++) {
        VecAppend(child->panels, values[i]);
    }
    if (!AppendChild(FindNode(parent), child, size)) {
        delete child;
        return {};
    }
    return child->nodeId;
}

static bool FindParentRec(PaneNode* node, NodeId id, PaneNode** parent,
                          int* childIx) {
    if (!node || node->paneKind != PaneKind::Split) {
        return false;
    }
    for (int i = 0; i < node->children.len; i++) {
        if (node->children[i]->nodeId == id) {
            if (parent) {
                *parent = node;
            }
            if (childIx) {
                *childIx = i;
            }
            return true;
        }
        if (FindParentRec(node->children[i], id, parent, childIx)) {
            return true;
        }
    }
    return false;
}

static void RemoveAt(Vec<PanelId>* values, int ix) {
    for (int i = ix; i + 1 < values->len; i++) {
        (*values)[i] = (*values)[i + 1];
    }
    if (values->len > 0) {
        values->len--;
    }
}

bool PaneTree::DetachPanel(PanelId panel) {
    NodeId nodeId;
    if (!FindPanelNode(panel, &nodeId)) {
        return false;
    }
    PaneNode* node = FindNode(nodeId);
    if (node->paneKind == PaneKind::Tabs) {
        for (int i = 0; i < node->panels.len; i++) {
            if (node->panels[i] != panel) {
                continue;
            }
            RemoveAt(&node->panels, i);
            if (i < node->activeIx) {
                node->activeIx--;
            }
            return true;
        }
    }
    return false;
}

bool PaneTree::ApplyInsert(PanelId panel, InsertTarget target) {
    PaneNode* node = FindNode(target.node);
    if (!node) {
        return false;
    }
    if (target.kind == InsertTargetKind::Tabs) {
        if (node->paneKind != PaneKind::Tabs) {
            return false;
        }
        int at = target.ix < 0 ? node->panels.len
                               : std::min(target.ix, node->panels.len);
        if (!VecInsertAt(node->panels, at, panel)) {
            return false;
        }
        if (target.activate) {
            node->activeIx = at;
        } else if (at <= node->activeIx && node->panels.len > 1) {
            node->activeIx++;
        }
        return true;
    }
    const float* size = target.hasSize ? &target.size : nullptr;
    return InsertBeside(target.node, panel, target.placement, size);
}

bool PaneTree::InsertBeside(NodeId at, PanelId panel, Placement placement,
                            const float* size) {
    PaneNode* target = FindNode(at);
    if (!target) {
        return false;
    }
    PaneNode* group = PaneNode::Tabs(AllocateNodeId());
    VecAppend(group->panels, panel);
    bool before = placement == Placement::Left || placement == Placement::Top;
    Axis axis = PlacementAxis(placement);
    PaneNode* parent = nullptr;
    int ix = -1;
    if (FindParentRec(root, at, &parent, &ix) && parent->axis == axis) {
        int insertAt = before ? ix : ix + 1;
        float newSize = size ? *size : 0;
        uint8_t known = size ? 1 : 0;
        if (!size && parent->sizeKnown[ix]) {
            newSize = parent->sizes[ix] * 0.5f;
            parent->sizes[ix] = newSize;
            known = 1;
        }
        VecInsertAt(parent->children, insertAt, group);
        VecInsertAt(parent->sizes, insertAt, newSize);
        VecInsertAt(parent->sizeKnown, insertAt, known);
        return true;
    }

    PaneNode* wrapper = PaneNode::Split(AllocateNodeId(), axis);
    if (before) {
        AppendChild(wrapper, group, size);
        AppendChild(wrapper, target, nullptr);
    } else {
        AppendChild(wrapper, target, nullptr);
        AppendChild(wrapper, group, size);
    }
    if (parent) {
        parent->children[ix] = wrapper;
    } else {
        root = wrapper;
    }
    // Ownership of target moved under wrapper; no deletion occurs here.
    return true;
}

static bool NormalizeNode(PaneNode* node) {
    bool changed = false;
    if (node->paneKind == PaneKind::Tabs) {
        int clamped =
            node->panels.len > 0
                ? std::max(0, std::min(node->activeIx, node->panels.len - 1))
                : 0;
        if (node->activeIx != clamped) {
            node->activeIx = clamped;
            changed = true;
        }
        return changed;
    }
    if (node->paneKind != PaneKind::Split) {
        return false;
    }

    for (int i = 0; i < node->children.len; i++) {
        changed |= NormalizeNode(node->children[i]);
    }

    // Rule 1: remove empty containers from their parent.
    for (int i = 0; i < node->children.len;) {
        if (!node->children[i]->Empty()) {
            i++;
            continue;
        }
        delete node->children[i];
        for (int k = i; k + 1 < node->children.len; k++) {
            node->children[k] = node->children[k + 1];
            node->sizes[k] = node->sizes[k + 1];
            node->sizeKnown[k] = node->sizeKnown[k + 1];
        }
        node->children.len--;
        node->sizes.len--;
        node->sizeKnown.len--;
        changed = true;
    }

    // Rule 2: replace a one-child split child with that child, preserving the
    // outer slot and the replacement child's own stable id.
    for (int i = 0; i < node->children.len; i++) {
        PaneNode* split = node->children[i];
        if (split->paneKind != PaneKind::Split || split->children.len != 1) {
            continue;
        }
        PaneNode* replacement = split->children[0];
        split->children.len = 0;
        delete split;
        node->children[i] = replacement;
        changed = true;
    }

    // Rule 3: splice same-axis splits into their parent. Build new parallel
    // arrays once so every pointer and optional size remains aligned.
    bool hasSameAxis = false;
    for (int i = 0; i < node->children.len; i++) {
        PaneNode* child = node->children[i];
        hasSameAxis |=
            child->paneKind == PaneKind::Split && child->axis == node->axis;
    }
    if (hasSameAxis) {
        Vec<PaneNode*> children;
        Vec<float> sizes;
        Vec<uint8_t> known;
        for (int i = 0; i < len(node->children); i++) {
            PaneNode* child = node->children[i];
            if (child->paneKind != PaneKind::Split ||
                child->axis != node->axis) {
                VecAppend(children, child);
                VecAppend(sizes, node->sizes[i]);
                VecAppend(known, node->sizeKnown[i]);
                continue;
            }
            float total = 0;
            bool allKnown = true;
            for (int k = 0; k < len(child->children); k++) {
                allKnown &= child->sizeKnown[k] != 0;
                total += child->sizes[k];
            }
            bool scale = node->sizeKnown[i] && allKnown && total > 0;
            float ratio = scale ? node->sizes[i] / total : 1.f;
            for (int k = 0; k < len(child->children); k++) {
                VecAppend(children, child->children[k]);
                VecAppend(sizes, child->sizes[k] * ratio);
                VecAppend(known, child->sizeKnown[k]);
            }
            child->children.len = 0;
            delete child;
        }
        node->children = children;
        node->sizes = sizes;
        node->sizeKnown = known;
        changed = true;
    }
    return changed;
}

void PaneTree::Normalize() {
    for (int pass = 0; pass < 64; pass++) {
        bool changed = NormalizeNode(root);
        if (rootKind == RootKind::Any && root->paneKind == PaneKind::Split &&
            root->children.len == 1) {
            PaneNode* old = root;
            root = old->children[0];
            old->children.len = 0;
            delete old;
            changed = true;
        } else if (rootKind == RootKind::Split &&
                   root->paneKind != PaneKind::Split) {
            PaneNode* old = root;
            root = PaneNode::Split(AllocateNodeId(), Axis::Horizontal);
            AppendChild(root, old, nullptr);
            changed = true;
        }
        if (!changed) {
            break;
        }
    }
}

static bool NormalizedNode(const PaneNode* node, NodeId rootId) {
    if (node->paneKind == PaneKind::Tabs) {
        return (node->panels.len == 0 ||
                (node->activeIx >= 0 && node->activeIx < node->panels.len)) &&
               (node->nodeId == rootId || node->panels.len > 0);
    }
    if (node->children.len != node->sizes.len ||
        node->children.len != node->sizeKnown.len) {
        return false;
    }
    if (node->nodeId != rootId && node->children.len <= 1) {
        return false;
    }
    for (int i = 0; i < node->children.len; i++) {
        const PaneNode* child = node->children[i];
        if ((child->paneKind == PaneKind::Split && child->axis == node->axis) ||
            child->Empty() || !NormalizedNode(child, rootId)) {
            return false;
        }
    }
    return true;
}

bool PaneTree::IsNormalized() const {
    if (!root) {
        return false;
    }
    if (rootKind == RootKind::Split && root->paneKind != PaneKind::Split) {
        return false;
    }
    if (rootKind == RootKind::Any && root->paneKind == PaneKind::Split &&
        root->children.len == 1) {
        return false;
    }
    return NormalizedNode(root, root->nodeId);
}

EditResult PaneTree::InsertPanel(PanelId panel, InsertTarget target) {
    bool changed = ApplyInsert(panel, target);
    Normalize();
    return EditResult{changed};
}

EditResult PaneTree::RemovePanel(PanelId panel) {
    bool changed = DetachPanel(panel);
    Normalize();
    return EditResult{changed};
}

EditResult PaneTree::MovePanel(PanelId panel, InsertTarget target) {
    bool detached = DetachPanel(panel);
    bool inserted = ApplyInsert(panel, target);
    Normalize();
    return EditResult{detached || inserted};
}

EditResult PaneTree::Split(NodeId at, PanelId panel, Placement placement,
                           const float* size) {
    return InsertPanel(panel, InsertTarget::Split(at, placement, size));
}

EditResult PaneTree::SetActive(NodeId id, int ix) {
    PaneNode* node = FindNode(id);
    if (!node || node->paneKind != PaneKind::Tabs || ix < 0 ||
        node->activeIx == ix) {
        Normalize();
        return {};
    }
    node->activeIx = ix;
    Normalize();
    return EditResult{true};
}

EditResult PaneTree::SetSizes(NodeId id, const float* values,
                              const uint8_t* known, int count) {
    PaneNode* node = FindNode(id);
    if (!node || node->paneKind != PaneKind::Split || !values ||
        count != node->children.len) {
        Normalize();
        return {};
    }
    bool changed = false;
    for (int i = 0; i < count; i++) {
        uint8_t isKnown = known ? known[i] : 1;
        changed |= node->sizes[i] != values[i] || node->sizeKnown[i] != isKnown;
        node->sizes[i] = values[i];
        node->sizeKnown[i] = isKnown;
    }
    Normalize();
    return EditResult{changed};
}

static DockLayout* NewLayout(PaneKind kind, Axis axis) {
    DockLayout* layout = new DockLayout();
    layout->kind = kind;
    layout->axis = axis;
    return layout;
}

DockLayout* DockLayout::HSplit() {
    return NewLayout(PaneKind::Split, Axis::Horizontal);
}

DockLayout* DockLayout::VSplit() {
    return NewLayout(PaneKind::Split, Axis::Vertical);
}

DockLayout* DockLayout::Tabs() {
    return NewLayout(PaneKind::Tabs, Axis::Horizontal);
}

DockLayout* DockLayout::Child(DockLayout* child, const float* size) {
    if (kind == PaneKind::Split && child) {
        VecAppend(children, child);
        VecAppend(sizes, size ? *size : 0);
        VecAppend(sizeKnown, size ? 1 : 0);
    }
    return this;
}

DockLayout* DockLayout::Panel(PanelId id, DockPanelDef view) {
    if (kind == PaneKind::Tabs) {
        VecAppend(panelIds, id);
        VecAppend(panelViews, view);
    }
    return this;
}

DockLayout* DockLayout::ActiveIndex(int ix) {
    if (kind == PaneKind::Tabs) {
        activeIx = ix;
    }
    return this;
}

DockLayout::~DockLayout() {
    for (int i = 0; i < children.len; i++) {
        delete children[i];
    }
    VecReset(children);
    VecReset(sizes);
    VecReset(sizeKnown);
    VecReset(panelIds);
    VecReset(panelViews);
}

static PaneNode* BuildLayoutNode(PaneTree* tree, const DockLayout* layout,
                                 Vec<DockPanelDef>* collected) {
    NodeId id = tree->AllocateNodeId();
    if (layout->kind == PaneKind::Split) {
        PaneNode* node = PaneNode::Split(id, layout->axis);
        for (int i = 0; i < layout->children.len; i++) {
            PaneNode* child =
                BuildLayoutNode(tree, layout->children[i], collected);
            const float* size =
                layout->sizeKnown[i] ? &layout->sizes[i] : nullptr;
            AppendChild(node, child, size);
        }
        return node;
    }
    if (layout->kind == PaneKind::Tabs) {
        PaneNode* node = PaneNode::Tabs(id);
        node->activeIx = layout->activeIx;
        for (int i = 0; i < layout->panelIds.len; i++) {
            VecAppend(node->panels, layout->panelIds[i]);
            if (collected && i < layout->panelViews.len) {
                VecAppend(*collected, layout->panelViews[i]);
            }
        }
        return node;
    }
    return PaneNode::Tabs(id);
}

PaneTree* PaneTree::FromLayout(DockLayout* layout, RootKind kind,
                               Vec<DockPanelDef>* panels) {
    if (!layout) {
        return nullptr;
    }
    PaneTree* tree = new PaneTree(kind);
    PaneNode* built = BuildLayoutNode(tree, layout, panels);
    if (kind == RootKind::Split && built->paneKind != PaneKind::Split) {
        PaneNode* wrapper =
            PaneNode::Split(tree->AllocateNodeId(), Axis::Horizontal);
        AppendChild(wrapper, built, nullptr);
        built = wrapper;
    }
    SetRoot(tree, built);
    tree->Normalize();
    return tree;
}

static void CollectTabPanels(const DockAreaState* st, int ix,
                             const PaneBuilder& builder, Vec<PanelId>* out);

static PaneNode* BuildStateNode(PaneTree* tree, const DockAreaState* st, int ix,
                                const PaneBuilder& builder) {
    if (!st || ix < 0 || ix >= st->nodes.len) {
        return PaneNode::Tabs(tree->AllocateNodeId());
    }
    const PanelStateNode& sn = st->nodes[ix];
    NodeId id = tree->AllocateNodeId();
    if (sn.kind == PanelInfoKind::Stack) {
        PaneNode* node = PaneNode::Split(id, sn.axis);
        for (int i = 0; i < sn.children.len; i++) {
            PaneNode* child = BuildStateNode(tree, st, sn.children[i], builder);
            const float* size =
                i < sn.sizes.len && sn.sizes[i] > 0 ? &sn.sizes[i] : nullptr;
            AppendChild(node, child, size);
        }
        return node;
    }
    if (sn.kind == PanelInfoKind::Tabs) {
        PaneNode* node = PaneNode::Tabs(id);
        CollectTabPanels(st, ix, builder, &node->panels);
        node->activeIx = sn.activeIndex;
        return node;
    }
    PaneNode* node = PaneNode::Tabs(id);
    if (StrEq(sn.panelName, kTabPanelName)) {
        return node;
    }
    if (builder.build) {
        VecAppend(node->panels, builder.build(builder.data, &sn));
    }
    return node;
}

static void CollectTabPanels(const DockAreaState* st, int ix,
                             const PaneBuilder& builder, Vec<PanelId>* out) {
    if (!st || !out || ix < 0 || ix >= st->nodes.len) {
        return;
    }
    const PanelStateNode& sn = st->nodes[ix];
    for (int i = 0; i < sn.children.len; i++) {
        int childIx = sn.children[i];
        if (childIx < 0 || childIx >= st->nodes.len) {
            continue;
        }
        const PanelStateNode& child = st->nodes[childIx];
        if (child.kind == PanelInfoKind::Tabs) {
            CollectTabPanels(st, childIx, builder, out);
            continue;
        }
        if (child.kind == PanelInfoKind::Panel &&
            StrEq(child.panelName, kTabPanelName)) {
            continue;
        }
        if (builder.build) {
            VecAppend(*out, builder.build(builder.data, &child));
        }
    }
}

void PaneTree::FromState(const DockAreaState* st, int nodeIx,
                         const PaneBuilder& builder) {
    PaneNode* built = BuildStateNode(this, st, nodeIx, builder);
    if (rootKind == RootKind::Split && built &&
        built->paneKind != PaneKind::Split) {
        PaneNode* wrapper = PaneNode::Split(AllocateNodeId(), Axis::Horizontal);
        AppendChild(wrapper, built, nullptr);
        built = wrapper;
    }
    SetRoot(this, built);
    Normalize();
}

} // namespace gpui
