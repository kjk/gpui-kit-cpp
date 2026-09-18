#ifndef GPUI_BASE_DOCK_LAYOUT_H_
#define GPUI_BASE_DOCK_LAYOUT_H_
/* Pure dock layout algebra — crates/base/src/dock/layout

   PaneTree is the retained, renderer-independent shape upstream introduced
   between panel state and DockArea entities. Containers have stable NodeIds,
   panels have stable PanelIds, every edit normalizes before returning, and a
   DockLayout describes a tree without constructing UI. The owning C++ tree
   uses heap child nodes because Vec<T> is for relocatable records only. */

#include "base/dock.h"

namespace gpui {

enum class RootKind : uint8_t {
    Split,
    Any
};

enum class PaneKind : uint8_t {
    Split,
    Tabs
};

struct PaneNode;

// Borrowed read-only projection of PaneNode::kind, matching Rust's PaneRef
// enum without copying a node's unbounded vectors.
struct PaneRef {
    PaneKind kind = PaneKind::Split;
    Axis axis = Axis::Horizontal;
    const Vec<PaneNode*>* children = nullptr;
    const Vec<float>* sizes = nullptr;
    const Vec<uint8_t>* sizeKnown = nullptr;
    const Vec<PanelId>* panels = nullptr;
    int activeIx = 0;
};

struct PaneNode {
    NodeId nodeId = {};
    PaneKind paneKind = PaneKind::Split;
    Axis axis = Axis::Horizontal;
    Vec<PaneNode*> children;
    // Option<Pixels>: sizeKnown[i] says whether sizes[i] is Some.
    Vec<float> sizes;
    Vec<uint8_t> sizeKnown;
    Vec<PanelId> panels;
    int activeIx = 0;

    static PaneNode* Split(NodeId id, Axis axis);
    static PaneNode* Tabs(NodeId id);
    NodeId Id() const { return nodeId; }
    PaneRef Kind() const;
    void Walk(Func1<const PaneNode*> visit) const;
    bool Empty() const;
    ~PaneNode();
};

enum class InsertTargetKind : uint8_t {
    Tabs,
    Split
};

struct InsertTarget {
    InsertTargetKind kind = InsertTargetKind::Tabs;
    NodeId node = {};
    int ix = -1;
    bool activate = true;
    Placement placement = Placement::Right;
    bool hasSize = false;
    float size = 0;
    static InsertTarget Tabs(NodeId node, int ix = -1, bool activate = true);
    static InsertTarget Split(NodeId node, Placement placement,
                              const float* size = nullptr);
};

struct EditResult {
    bool didChange = false;
    bool Changed() const { return didChange; }
};

struct DockLayout;
struct PanelSource;
struct DockAreaState;
struct PanelStateNode;

// Turns a persisted leaf into a live panel id. Rust's PanelBuilder.
struct PaneBuilder {
    void* data = nullptr;
    PanelId (*build)(void* data, const PanelStateNode* state) = nullptr;
};

struct PaneTree {
    PaneNode* root = nullptr;
    RootKind rootKind = RootKind::Any;

    explicit PaneTree(RootKind kind = RootKind::Any);
    PaneTree(const PaneTree&) = delete;
    PaneTree& operator=(const PaneTree&) = delete;
    ~PaneTree();

    PaneNode* Root() { return root; }
    const PaneNode* Root() const { return root; }
    RootKind GetRootKind() const { return rootKind; }
    NodeId AllocateNodeId();
    PaneNode* FindNode(NodeId id);
    const PaneNode* FindNode(NodeId id) const;
    bool FindPanelNode(PanelId panel, NodeId* out) const;
    bool ContainsPanel(PanelId panel) const;
    void NodeIds(Vec<NodeId>* out) const;
    void Panels(Vec<PanelId>* out) const;

    // Construction helpers also make the pure algebra convenient in tests
    // and for hosts that already have their own builder.
    NodeId SetRootSplit(Axis axis);
    NodeId SetRootTabs(const PanelId* panels, int count, int activeIx = 0);
    NodeId AddSplit(NodeId parent, Axis axis, const float* size = nullptr);
    NodeId AddTabs(NodeId parent, const PanelId* panels, int count,
                   const float* size = nullptr);

    EditResult InsertPanel(PanelId panel, InsertTarget target);
    EditResult RemovePanel(PanelId panel);
    EditResult MovePanel(PanelId panel, InsertTarget target);
    EditResult Split(NodeId at, PanelId panel, Placement placement,
                     const float* size = nullptr);
    EditResult SetActive(NodeId node, int ix);
    EditResult SetSizes(NodeId node, const float* sizes, const uint8_t* known,
                        int count);
    void Normalize();
    bool IsNormalized() const;
    // Appends a persisted subtree and returns its node index.
    int ToState(const PanelSource& source, DockAreaState* out) const;
    // Read a persisted subtree into this tree, replacing the root.
    void FromState(const DockAreaState* st, int nodeIx,
                   const PaneBuilder& builder);

    static PaneTree* FromLayout(DockLayout* layout, RootKind kind,
                                Vec<DockPanelDef>* panels = nullptr);

  private:
    bool ApplyInsert(PanelId panel, InsertTarget target);
    bool DetachPanel(PanelId panel);
    bool InsertBeside(NodeId at, PanelId panel, Placement placement,
                      const float* size);
};

// Describes a layout without building an entity. It owns child descriptions;
// FromLayout consumes none of them, so a host may rebuild more than one tree
// from the same description.
struct DockLayout {
    PaneKind kind = PaneKind::Split;
    Axis axis = Axis::Horizontal;
    Vec<DockLayout*> children;
    Vec<float> sizes;
    Vec<uint8_t> sizeKnown;
    Vec<PanelId> panelIds;
    Vec<DockPanelDef> panelViews;
    int activeIx = 0;

    static DockLayout* HSplit();
    static DockLayout* VSplit();
    static DockLayout* Tabs();
    DockLayout* Child(DockLayout* child, const float* size = nullptr);
    DockLayout* Panel(PanelId id, DockPanelDef view = {});
    DockLayout* ActiveIndex(int ix);
    ~DockLayout();
};

} // namespace gpui
#endif // GPUI_BASE_DOCK_LAYOUT_H_
