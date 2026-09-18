#ifndef GPUI_BASE_DOCK_STATE_H_
#define GPUI_BASE_DOCK_STATE_H_
/* The serialised dock layout — crates/ui/src/dock/state.rs

   A DockArea can be written out and read back: the centre item, the three
   docks around it, and for every node what kind it is — a split with its
   sizes and axis, a tab group with its active index, or a leaf panel. Rust
   does it with serde; this does it over the small JSON reader in base. An
   older file whose centre is a tiles node is read as a tab group. */

#include "base/json.h"
#include "base/dock.h"

namespace gpui {

// PanelInfo: which of the three a node is. Upstream dropped Tiles; a saved
// tiles node is read as a tab group.
enum class PanelInfoKind : uint8_t {
    Panel,
    Stack,
    Tabs
};

using PanelInfo = PanelInfoKind;

// PanelState: one node of the tree. Rust nests them by ownership; the nodes
// here live in one array and name their children by index, the way the dock's
// own tree does.
struct PanelStateNode {
    Str panelName = {};
    // As many children and sizes as the tree has. The live tree they are
    // written from is unbounded, so a saved layout that truncated it would
    // be a layout that could not be read back.
    Vec<int> children;
    PanelInfoKind kind = PanelInfoKind::Panel;
    // Stack: the size of each child, and which way they are laid out.
    Vec<float> sizes;
    Axis axis = Axis::Horizontal;
    // Tabs.
    int activeIndex = 0;
    // Panel: whatever the panel itself wrote, kept as it was so a round trip
    // does not lose it. Rust holds a serde_json::Value here.
    Str info = {};
    bool infoIsJson = false;
};

using PanelState = PanelStateNode;

// state_convert.rs object-safe seams. Serialization stays dependency-free:
// callers provide function tables instead of Rust trait objects.
struct PanelSource {
    void* data = nullptr;
    Str (*panelName)(void* data, PanelId id) = nullptr;
    bool (*isVisible)(void* data, PanelId id) = nullptr;
    void (*dump)(void* data, PanelId id, PanelState* out) = nullptr;
};

struct PanelBuilder {
    void* data = nullptr;
    DockPanelDef (*build)(void* data, const PanelState* state,
                          const PanelInfo* info, Window* win,
                          App* app) = nullptr;
};

// DockState: one of the three docks around the centre.
struct DockSideState {
    bool present = false;
    int node = -1;
    DockPlacement placement = DockPlacement::Left;
    float size = 0;
    bool open = true;
};

struct DockAreaState {
    // The version a layout was written with, so a reader can refuse one it
    // does not understand. Rust leaves it None when there is none.
    bool hasVersion = false;
    int version = 0;
    Vec<PanelStateNode> nodes;
    int center = -1;
    DockSideState left = {};
    DockSideState right = {};
    DockSideState bottom = {};

    int NewNode(Str panelName);
    // Back to an empty layout, with the nodes' own arrays dropped. Assigning
    // a fresh one over it would leak them; this is what the two callers that
    // start a layout from nothing use.
    void Clear();

    ~DockAreaState() {
        for (int i = 0; i < len(nodes); i++) {
            VecReset(nodes[i].children);
            VecReset(nodes[i].sizes);
        }
        VecReset(nodes);
    }
};

// The layout as JSON, and back. The parse answers false for text that is not
// a layout at all; a member that is not there leaves its default behind, the
// way serde's #[serde(default)] does.
bool DockAreaStateParse(Arena* a, Str json, DockAreaState* out);
// `out` takes the text; it is not pretty-printed, which is what serde_json's
// to_string writes too.
void DockAreaStateWrite(const DockAreaState* s, StrBuilder* out);

// ─── the live tree and the saved one ──────────────────────────────────────

// DockArea::dump: the tree as it stands, written as PanelStates. A tab group
// is a "TabPanel" whose children are its panels, a split is a "StackPanel"
// with its sizes and axis, and a panel is a leaf under the name it was
// registered with.
void DockDump(const DockState* s, DockAreaState* out);

// What an InvalidPanel is handed: the name the layout asked for and nothing
// answered to. Rust builds a view that says so and keeps the state it came
// from, so a round trip does not lose the panel it could not build.
struct DockInvalidPanel {
    Str name = {};
};

// DockArea::load. Panels are matched by name against the ones already
// registered — PanelRegistry::build_panel — and a name nothing answers to is
// registered as an InvalidPanel rendered by `invalidRender`, under the name
// it was asked for so dumping it again writes the same layout back.
//
// `a` holds the strings the new panels keep, so it has to outlive the dock;
// it is the arena the state was parsed into. False when the state has no
// centre to build from, which leaves the dock as it was.
bool DockLoad(DockState* s, const DockAreaState* st, Arena* a,
              El* (*invalidRender)(Ctx* cx, void* data) = nullptr,
              App* app = nullptr, Window* win = nullptr,
              Entity<DockState> dockArea = {});

} // namespace gpui
#endif // GPUI_BASE_DOCK_STATE_H_
