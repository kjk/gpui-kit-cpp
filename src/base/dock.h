#ifndef GPUI_BASE_DOCK_H_
#define GPUI_BASE_DOCK_H_
/* Unstyled dock — crates/ui/src/dock

   The dock is a tree: a DockItem is either a group of tabs or a split of
   other items, and a DockArea holds one of those in its centre plus a fixed
   Dock on the left, the right and the bottom. Rust builds the tree out of
   entities holding `Arc<dyn PanelView>`; there is no dyn here, so a panel is
   a title and a function that renders it, and the tree is an array of nodes
   naming each other by index. */

#include "base/geometry.h"

namespace gpui {

// PANEL_MIN_SIZE, from crates/base/src/resizable.
const float kDockPanelMinSize = 100.f;
// resize_handle: the grab between two panels, and along a Dock's inner edge.
const float kDockHandleW = 4;
// CLOSED_BOTTOM_STRIP: a closed bottom dock keeps this much, so its tab bar
// stays clickable. A closed side dock keeps nothing: there is no tab bar left
// to click at zero width, and reopening it is the application's to offer.
const float kClosedBottomStrip = 29.f;
// DragPanel's own box — `w_24` and a line of text between two paddings —
// which is both what follows the pointer and where the drop placeholder flies
// in from.
const float kDockDragPreviewW = 96.f;
const float kDockDragPreviewH = 30.f;

// What a press on a tab picks up, and what a press on a resize handle does.
// `ix` is the panel for the first and the handle for the second.
extern const Str kDockPanelDrag;
extern const Str kDockResizeDrag;

// layout::NodeId / PanelId. These are stable values rather than pool
// positions: the source keeps container identity across normalization, and a
// panel id is the packed generational entity identity when it came from one.
struct NodeId {
    uint64_t value = 0;

    static NodeId FromU64(uint64_t raw) { return NodeId{raw}; }
    uint64_t AsU64() const { return value; }
};

inline bool operator==(NodeId a, NodeId b) {
    return a.value == b.value;
}
inline bool operator!=(NodeId a, NodeId b) {
    return !(a == b);
}

struct PanelId {
    uint64_t value = 0;

    static PanelId FromU64(uint64_t raw) { return PanelId{raw}; }
    static PanelId FromEntity(EntityId id) {
        return PanelId{((uint64_t)id.gen << 32) | (uint32_t)id.index};
    }
    uint64_t AsU64() const { return value; }
};

inline bool operator==(PanelId a, PanelId b) {
    return a.value == b.value;
}
inline bool operator!=(PanelId a, PanelId b) {
    return !(a == b);
}

// The four places a Dock can be. Center is the DockArea's own item, which is
// not a Dock and never closes.
enum class DockPlacement : uint8_t {
    Center,
    Left,
    Bottom,
    Right
};

// split_placement_at: an edge means split the target that way, the middle
// means merge into its tab group.
enum class DockDrop : uint8_t {
    Center,
    Left,
    Right,
    Top,
    Bottom
};

// Which of the five zones of `b` the position is in. Rust's thresholds are
// 35% and 65% of each side.
DockDrop DockDropAt(Bounds b, float x, float y);
// DropPlaceholderBounds::for_placement: the half of `b` a drop would land in,
// or all of it for a merge.
Bounds DockDropPlaceholder(Bounds b, DockDrop d);

// drag.rs: source-shaped drag geometry over the same hit-testing functions.
// `split_placement_at` answers false for the center/merge zone.
bool split_placement_at(Bounds bounds, Point position, Placement* out);

struct DropPlaceholderBounds {
    Point origin = {};
    Size size = {};

    static DropPlaceholderBounds ForPlacement(Bounds bounds,
                                              const Placement* placement);
    Bounds In(Bounds parent) const {
        return {parent.x + origin.x, parent.y + origin.y, size.w, size.h};
    }
};

struct DragPanel {
    PanelId panel = {};
    NodeId source = {};
    Point dragOffset = {};
    Size previewSize = {};
    uint64_t dragSessionId = 0;

    static DragPanel New(PanelId panel, NodeId source);
};

struct AnyDrag {
    void* value = nullptr;
    // The host's stable tag. No RTTI is required to discriminate values.
    uint64_t type = 0;
};

enum class DropTarget : uint8_t {
    Canvas,
    Group
};

struct DropTargetValue {
    DropTarget kind = DropTarget::Canvas;
    NodeId node = {};
    bool hasPlacement = false;
    Placement placement = Placement::Top;
};

struct DropIndicator {
    Bounds bounds = {};
    bool hasPlacement = false;
    Placement placement = Placement::Top;
    DropPlaceholderBounds from = {};
    DropPlaceholderBounds to = {};
    uint64_t dragSessionId = 0;
    uint64_t epoch = 0;
};

// A node index past the pool, which is what a listener bound to one of the
// three docks rather than to a node carries. The pool grows, so this is a
// number no node index can reach rather than the pool's own size.
const int kDockSideBase = 1 << 20;

// Panel::zoomable, which is an `Option<PanelControl>` in Rust and defaults to
// `Some(Menu)`: where the zoom action shows up. A panel that cannot be zoomed
// at all is `No`, and the maximise icon is on the bar only for Toolbar and
// Both — every other panel offers Zoom In in the ⋯ menu and nowhere else.
enum class DockPanelControl : uint8_t {
    No,
    Menu,
    Toolbar,
    Both
};

inline bool DockPanelControlToolbar(DockPanelControl c) {
    return c == DockPanelControl::Toolbar || c == DockPanelControl::Both;
}
inline bool DockPanelControlMenu(DockPanelControl c) {
    return c == DockPanelControl::Menu || c == DockPanelControl::Both;
}

// DockArea::panel_style. Auto is Rust's default: a group showing one panel is
// a plain title row rather than a tab bar, and only a second tab turns the bar
// on. TabBar is the opt-out that always shows tabs.
enum class DockPanelStyle : uint8_t {
    Auto,
    TabBar
};

struct PanelStateNode;

// One panel the host handed the dock. Rust splits this across Base's behavior
// trait and UI's presentation trait, then carries both through a concrete
// PanelHandle. C++ has no trait objects, so the same object-safe seam is this
// POD function table. Base stores and moves it without interpreting the UI
// callbacks; a renderer that understands them may call them.
struct DockPanelDef {
    // Panel::panel_name(): what a saved layout stores and what the registry
    // is asked for on the way back. Not the title — that is what the tab
    // shows, and it can change while the name cannot.
    Str name = {};
    Str title = {};
    PanelId id = {};
    El* (*render)(Ctx* cx, void* data) = nullptr;
    // ui::Panel::title: an element rather than a string, so a title may carry
    // an icon, badge or styled fragments. Null falls back to `title`.
    El* (*titleEl)(Ctx* cx, void* data) = nullptr;
    // ui::Panel::title_style. False means inherit the skin. Two Rgba outputs
    // keep Base independent of UI's TitleStyle type.
    bool (*titleStyle)(Ctx* cx, void* data, Rgba* background,
                       Rgba* foreground) = nullptr;
    // Panel::title_suffix: what the panel puts in its own tab bar, between
    // the tabs and the group's zoom and menu buttons. A panel that wants a
    // search box or a status of its own has nowhere else to put it.
    El* (*titleSuffix)(Ctx* cx, void* data) = nullptr;
    // ui::Panel::toolbar_buttons. Rust returns Vec<Button>; one row element
    // is the natural C++ equivalent and still permits any number of buttons.
    El* (*toolbarButtons)(Ctx* cx, void* data) = nullptr;
    // ui::Panel::dropdown_menu. The menu is deliberately opaque here: Base
    // must not depend on the themed PopupMenu type.
    void (*dropdownMenu)(Ctx* cx, void* data, void* menu) = nullptr;
    // Panel::dump: lets a dynamic panel attach its own serialized JSON value
    // to the persisted leaf. `PanelState::infoIsJson` distinguishes raw JSON
    // from the legacy string payload used by native panels.
    void (*dump)(void* data, PanelStateNode* out) = nullptr;
    // Panel::tab_name(): the short label a tab shows when the title is too
    // much for one. Empty falls back to the title, and the single-panel title
    // row always shows the title.
    Str tabName = {};
    void* data = nullptr;
    bool closable = true;
    // Panel::visible(). A hidden panel has no tab, is not the active panel,
    // and does not count towards the one that may not be closed or dragged.
    bool visible = true;
    // Base Panel behavior callbacks. The source dispatches these through its
    // object-safe PanelView; function pointers retain the same seam here.
    void (*setActive)(Ctx* cx, void* data, bool active) = nullptr;
    void (*setZoomed)(Ctx* cx, void* data, bool zoomed) = nullptr;
    void (*onAddedTo)(Ctx* cx, void* data, int node) = nullptr;
    void (*onRemoved)(Ctx* cx, void* data) = nullptr;
    bool canZoom = true;
    DockPanelControl zoomable = DockPanelControl::Menu;
    // ui::Panel::title_bar: a lone panel that carries its own chrome may use
    // the whole group. A group with multiple panels still draws its tabs.
    bool titleBar = true;
    // ui::Panel::inner_padding: only relevant when a full tab bar surrounds
    // the active panel. Rust's default is true.
    bool innerPadding = true;
};

using Panel = DockPanelDef;
using PanelView = DockPanelDef;

enum class PanelEvent : uint8_t {
    ZoomIn,
    ZoomOut,
    LayoutChanged
};

// DockItem. A node is either Tabs — a list of panels with one active — or
// Split — a list of child nodes along an axis, each with a size.
struct DockNode {
    DockNode() = default;

    bool used = false;
    bool split = false;
    Axis axis = Axis::Horizontal;
    int parent = -1;
    // A split's children and their sizes, and a tab group's panels: as many
    // of either as the caller builds. Rust holds a Vec for each.
    Vec<int> child;
    Vec<float> size;
    Vec<int> panel;
    int activeIx = 0;
    // Where the node was last painted. The drop zone under the pointer can
    // only be worked out against boxes, and the boxes are last frame's.
    Bounds bounds = {};
    // The tab bar's own scroll, for a row of tabs wider than the bar, and the
    // tab it has been asked to bring into view — Rust's `tab_bar_scroll_handle`
    // and `pending_scroll_to_ix`. The two boxes are last frame's, which is
    // what the offset is worked out from.
    float tabScrollX = 0;
    int pendingScrollIx = -1;
    Bounds tabStripBounds = {};
    Bounds activeTabBounds = {};
    // Which tab `activeTabBounds` was measured for. A tab just made active
    // has no box yet — the frame that shows it is the one that measures it —
    // so the scroll waits for the frame after that.
    int activeTabBoundsIx = -1;
};

// A Dock: the fixed container on one side of the area.
struct DockSide {
    int node = -1;
    bool open = true;
    bool collapsible = true;
    float size = 200;

    static DockSide New(float value) {
        DockSide dock;
        dock.size = std::max(value, kDockPanelMinSize);
        return dock;
    }
    bool IsOpen() const { return open; }
    void SetOpen(bool value) { open = value; }
    bool IsCollapsible() const { return collapsible; }
    void SetCollapsible(bool value) { collapsible = value; }
    float GetSize() const { return size; }
    void SetSize(float value) { size = std::max(value, kDockPanelMinSize); }
    bool IsResizing() const { return resizing; }
    void SetResizing(bool value) { resizing = value; }

    // Source Dock retains this on each side. DockState also keeps the active
    // placement because its compatibility renderer has one shared listener.
    bool resizing = false;
};

using Dock = DockSide;

// dock_placement.rs: pure pointer-to-size arithmetic, independent of an
// entity or renderer.
struct DockSizing {
    DockPlacement placement = DockPlacement::Center;
    Bounds area = {};
    float oppositeDockSize = 0;

    static DockSizing New(DockPlacement placement);
    DockSizing WithAreaBounds(Bounds value) const;
    DockSizing WithAreaWidth(float value) const;
    DockSizing WithAreaHeight(float value) const;
    DockSizing WithOppositeDockSize(float value) const;
    float SizeFromPointer(Point pointer) const;
    float Clamp(float value) const;
};

enum class DockEventKind : uint8_t {
    // DockEvent::LayoutChanged.
    LayoutChanged
};

struct DockEvent {
    DockEventKind kind = DockEventKind::LayoutChanged;
};

struct DockState {
    Vec<DockPanelDef> panels;
    // The node pool. A free slot is one whose `used` is false, and the pool
    // grows when there is none — `DockNode` holds arrays of its own, so a
    // slot is emptied rather than dropped.
    Vec<DockNode> nodes;
    int center = -1;
    DockSide left = {};
    DockSide bottom = {};
    DockSide right = {};
    // ToggleZoom: the one panel filling the whole area, or -1.
    int zoomPanel = -1;
    // DockArea::locked. A locked dock still resizes; it just does not move
    // panels around.
    bool locked = false;
    // DockArea::panel_style, which every group in the area reads.
    DockPanelStyle panelStyle = DockPanelStyle::Auto;
    // DockArea::toggle_button_visible: whether the three dock toggles are
    // drawn at all.
    bool toggleButtonVisible = true;
    // UI DockSkin::tiles_scrollbar_mode. Rust retains this on the shared
    // skin; the C++ entity is the shared retained object, so copies of the
    // lightweight DockSkin handle continue to observe the same setting.
    bool hasTilesScrollbarMode = false;
    ScrollbarMode tilesScrollbarMode = ScrollbarMode::Always;
    // DockArea::version, kept so a layout that was loaded writes back the
    // version it came with.
    bool hasVersion = false;
    int version = 0;
    // DockArea::bounds, written at paint.
    Bounds bounds = {};
    // Which node a drag is over and where it would land, so the placeholder
    // can be drawn before the button comes up. -1 when no drag is in flight.
    int dropNode = -1;
    DockDrop dropAt = DockDrop::Center;
    // Where the placeholder flies in from when a drag first reaches a group:
    // the dragged tab's own preview, which is what Rust's `source` is for a
    // panel drag. Pending until the frame that draws it has seeded the
    // transition with it.
    Bounds dropFrom = {};
    bool dropFromPending = false;
    // Which group's ⋯ menu is open, so the row it reports lands on the right
    // panel. Rust's menu is built by the TabPanel itself and closes over it;
    // a menu here reports only which row was taken.
    int menuNode = -1;
    // The side being resized, and the split handle being dragged.
    DockPlacement resizingSide = DockPlacement::Center;
    bool resizing = false;

    Listener onEvent;

    static void OnTabClick(DockState* self, Ctx* cx, const ClickEvent* ev,
                           intptr_t nodeAndIx);
    static void OnCloseClick(DockState* self, Ctx* cx, const ClickEvent* ev,
                             intptr_t nodeAndIx);
    static void OnZoomClick(DockState* self, Ctx* cx, const ClickEvent* ev,
                            intptr_t panelIx);
    static void OnToggleSide(DockState* self, Ctx* cx, const ClickEvent* ev,
                             intptr_t placement);
    static void OnTabDragMove(DockState* self, Ctx* cx,
                              const DragMoveEvent* ev);
    static void OnTabDragEnd(DockState* self, Ctx* cx, const MouseUpEvent* ev);
    static void OnDropPanel(DockState* self, Ctx* cx, const DropEvent* ev,
                            intptr_t node);
    // A drop on a tab, which names the place in the row the panel takes, and
    // one on the empty space past the last tab, which appends. Rust's two
    // `on_drop` closures on the tab bar.
    static void OnDropTab(DockState* self, Ctx* cx, const DropEvent* ev,
                          intptr_t nodeAndIx);
    static void OnDropTabBar(DockState* self, Ctx* cx, const DropEvent* ev,
                             intptr_t node);
    // The ⋯ menu: Zoom In / Zoom Out and Close, over the active panel.
    static void OnMenuItem(DockState* self, Ctx* cx, const ClickEvent* ev,
                           intptr_t nodeAndIx);
    // The tab bar scrolled sideways, which a row of tabs too wide for it does.
    static void OnTabBarScroll(DockState* self, Ctx* cx, const ScrollEvent* ev,
                               intptr_t node);
    static void OnResizeDrag(DockState* self, Ctx* cx, const DragMoveEvent* ev);
    static void OnResizeEnd(DockState* self, Ctx* cx, const MouseUpEvent* ev);

    ~DockState() {
        for (int i = 0; i < nodes.len; i++) {
            VecReset(nodes[i].child);
            VecReset(nodes[i].size);
            VecReset(nodes[i].panel);
        }
        VecReset(nodes);
        VecReset(panels);
    }
};

// A node index and a slot inside it, packed into the one intptr_t a listener
// carries. Rust's closure captures both outright.
inline intptr_t DockPack(int node, int ix) {
    return (intptr_t)(node * 64 + ix);
}
inline int DockUnpackNode(intptr_t v) {
    return (int)(v / 64);
}
inline int DockUnpackIx(intptr_t v) {
    return (int)(v % 64);
}

// Register a panel. The index it answers with is what the tree names.
// normalize (crates/base/src/dock/layout/normalize.rs): the tree collapsed
// to its canonical shape, bottom up and repeated until nothing changes. An
// empty group or split is dropped, a split with one child is replaced by that
// child, a split nested in a split of the same axis is spliced into it, and a
// tab group's active index is clamped to what it holds. A root — the centre
// item and the three docks — is kept whatever shape it is in, which is what
// Rust's RootKind::Split does.
//
// The edits here collapse as they go (see the prune in dock.cpp), so this is
// for the shapes an edit cannot make: a layout read back from a file, which
// may hold any tree at all.
void DockNormalize(DockState* s);

int DockAddPanelDef(DockState* s, DockPanelDef def);
// DockItem::tabs / DockItem::split, as node indices. -1 when the tree is full.
int DockNewTabs(DockState* s);
int DockNewSplit(DockState* s, Axis axis);
// Add a panel to a tab group, or a child to a split. A child's size is its
// extent along the split's axis.
void DockTabsAdd(DockState* s, int node, int panelIx);
// insert_panel_at: the same, at a place in the row rather than at the end,
// and the inserted panel becomes the active one.
void DockTabsInsert(DockState* s, int node, int panelIx, int at);
void DockSplitAdd(DockState* s, int node, int childNode, float size);
// The active tab of a group.
void DockSetActive(DockState* s, Ctx* cx, int node, int ix);
// Panel::closable: take the panel out of its group. An empty group leaves the
// split it was in, and a split with one child left is replaced by that child,
// which is what Rust's remove_self_if_empty does.
void DockClosePanel(DockState* s, Ctx* cx, int node, int ix);
// The drag landed: merge into `to`'s tabs, or split `to` that way. Moving a
// panel onto its own group is a no-op the way Rust's is.
//
// `atIx` is Rust's `ix: Option<usize>` — the tab the drop landed on, so the
// panel takes that place in the row instead of the end. -1 is `None`: the
// drop was on the body, or on the empty space past the last tab. A drop onto
// a tab of the group the panel is already in is a reorder, which is the one
// case a same-group drop is not a no-op.
void DockMovePanel(DockState* s, Ctx* cx, int panelIx, int to, DockDrop drop,
                   int atIx = -1);
// The tree half of those two, with no window to notify: the listeners call
// these and then emit, and a test drives them on their own.
bool DockClosePanelAt(DockState* s, int node, int ix);
bool DockMovePanelTo(DockState* s, int panelIx, int to, DockDrop drop,
                     int atIx = -1);
// ScrollHandle::scroll_to_item: the sideways offset that brings `tab` inside
// `strip`, from where the strip is scrolled now. A tab already inside asks for
// nothing; one off either end is brought just inside that edge.
float DockTabScrollTo(float scrollX, Bounds strip, Bounds tab);

// Dock::toggle_open, and the size a drag on its edge asks for.
void DockToggleSide(DockState* s, Ctx* cx, DockPlacement p);
void DockResizeSide(DockState* s, Ctx* cx, DockPlacement p, float x, float y);
// DockArea::set_dock_size: the programmatic size, clamped the way the Dock
// clamps it. Only an effective change is persisted — a size that lands where
// it already was neither redraws nor emits LayoutChanged.
void DockSetDockSize(DockState* s, Ctx* cx, DockPlacement p, float size);
// ToggleZoom.
void DockToggleZoom(DockState* s, Ctx* cx, int panelIx);
// The Dock on one side, or null for Center.
DockSide* DockSideOf(DockState* s, DockPlacement p);
// Which tab group holds this panel, or -1.
int DockNodeOfPanel(const DockState* s, int panelIx);
// PanelRegistry::build_panel's lookup half: the panel registered under this
// name, or -1.
int DockPanelByName(const DockState* s, Str name);
// Which Dock a node is in — the side whose tree it hangs under, or Center for
// the area's own item. What a click on a collapsed dock's tab needs to know.
DockPlacement DockPlacementOfNode(const DockState* s, int node);

// Panel::visible: how many of a group's panels are shown, and the index of
// the one that is active — a hidden panel sitting at `activeIx` falls back to
// the first visible one, which is Rust's `active_panel`.
int DockVisibleCount(const DockState* s, int node);
// is_node_visible: whether this node has anything to show — a tab group with
// a visible panel, or a split with a visible child. A node with nothing takes
// no room in the split holding it.
bool DockNodeVisible(const DockState* s, int node);
int DockActiveIx(const DockState* s, int node);
// TabPanel::is_locked: the area is locked, this group is the zoomed one, or
// it is a root with no split above it.
bool DockNodeLocked(const DockState* s, int node);
// TabPanel::is_last_panel: this group shows one panel at most and no split
// above it holds more than one child. The last panel of a dock is neither
// draggable nor closable, so a dock can never be emptied by hand.
bool DockIsLastPanel(const DockState* s, int node);
// draggable() / droppable(): what the two guards above come to.
bool DockNodeDraggable(const DockState* s, int node);
bool DockNodeDroppable(const DockState* s, int node);
// StackPanel::left_top_tab_panel / right_top_tab_panel: which group carries
// each of the three dock toggle buttons. The right one follows the split's
// axis — the last child across, the first one down — and the bottom toggle
// belongs to the bottom Dock's own first group.
int DockLeftTopTabs(const DockState* s, int node);
int DockRightTopTabs(const DockState* s, int node);
// Dock::set_collapsible: a dock that may not be collapsed is opened, so it
// can never be left shut and unreachable.
void DockSetCollapsible(DockState* s, DockPlacement p, bool collapsible);

// ---------------------------------------------------------------------------
// The skin
//
// Upstream splits the dock the way it splits everything else: `crates/base`
// owns the tree, the drag, the drop and the resize, and every pixel is the
// caller's, handed over through `DockAreaRenderer`, `TabGroupRenderer` and
// `TilesRenderer`. `crates/ui` is one implementation of those traits and the
// base showcase is another — which is why upstream's showcase can put a dock
// on a page without reaching for the themed one.
//
// An element here holds no closures, so a trait object is a struct of
// function pointers and a `data`, the way `CalendarItemFn` and
// `VirtualListOpts` already are. Where Rust hands the skin a context object
// with methods that reach back into the area, this hands it a `DockCtx` /
// `DockTabGroup` and a set of `DockBind*` calls that wire base's behavior
// onto whatever element the skin built.

// DockContext: one Dock as the skin sees it.
struct DockCtx {
    Ctx* cx = nullptr;
    Entity<DockState> state = {};
    Str id = {};
    DockPlacement placement = DockPlacement::Left;
    float size = 0;
    bool open = true;
    bool collapsible = true;
};

using DockContext = DockCtx;

// dock_extent: how much room a dock asks for along its own axis — its size
// while open, CLOSED_BOTTOM_STRIP for a shut bottom dock, nothing for a shut
// side dock.
float DockExtent(const DockCtx* dock);
// dock_frame: the box a dock occupies — its extent along its own axis, full
// across, and held at that size rather than stretched by the row it sits in.
// Structural, not decorative, which is why base builds it around whatever
// the renderer's `dock` hook returns rather than leaving it to the skin.
El* DockFrame(Ctx* cx, const DockCtx* dock, float size);

// ResizeHandleContext: one boundary between two panels, and how it is being
// touched. `is_active()` is the drag, and it is the whole of what Rust hands
// the appearance callback: the pointer being over the strip is answered by
// `group_hover` on what the callback returns, not by asking the window.
struct DockHandleCtx {
    Axis axis = Axis::Horizontal;
    bool active = false;
};

// TabGroupContext: one tab group as the skin sees it.
struct DockTabGroup {
    Ctx* cx = nullptr;
    Entity<DockState> state = {};
    Str id = {};
    int node = -1;
    // TabPanel::collapsed — the group is in a Dock that is shut, so it keeps
    // its bar and nothing else.
    bool collapsed = false;
};

using TabGroupContext = DockTabGroup;

enum class TabGroupEvent : uint8_t {
    Drop,
    DragDrop,
    ClosePanel,
    ActiveChanged,
    ZoomIn,
    ZoomOut
};

struct TabGroupConstraints {
    bool alone = true;
    bool dockLocked = true;
    bool collapsed = false;
    bool closable = false;

    static TabGroupConstraints Sealed() { return {}; }
    static TabGroupConstraints InSplit(bool isAlone) {
        TabGroupConstraints value;
        value.alone = isAlone;
        value.dockLocked = false;
        value.closable = true;
        return value;
    }
    TabGroupConstraints DockLocked(bool value) const {
        TabGroupConstraints copy = *this;
        copy.dockLocked = value;
        return copy;
    }
    TabGroupConstraints Collapsed(bool value) const {
        TabGroupConstraints copy = *this;
        copy.collapsed = value;
        return copy;
    }
    TabGroupConstraints Closable(bool value) const {
        TabGroupConstraints copy = *this;
        copy.closable = value;
        return copy;
    }
};

// The compatibility engine retains groups as nodes of DockState. This handle
// is the source TabGroup entity's stable identity over that same state.
struct TabGroup {
    Entity<DockState> state = {};
    int node = -1;
    TabGroupConstraints constraints = {};
};

// group.panels() and group.active_ix().
int DockGroupCount(const DockTabGroup* g);
const DockPanelDef* DockGroupPanel(const DockTabGroup* g, int ix);
int DockGroupActiveIx(const DockTabGroup* g);
DockPlacement DockGroupPlacement(const DockTabGroup* g);
// Which Dock, if any, carries its toggle button on this group —
// `update_toggle_button_tab_panels`, asked rather than cached.
bool DockGroupHasToggle(const DockTabGroup* g, DockPlacement p);

// group.select_tab(ix), group.drag_panel(ix, cx) and the drop that lands on a
// tab, wired onto the element the skin built. The element comes back so the
// call chains. A collapsed group's tab clicks to open the Dock and does
// neither of the other two, which is Rust's `when(!self.collapsed, ..)`.
El* DockBindTab(const DockTabGroup* g, int ix, El* tab);
// last_empty_space: the run of bar past the last tab, which takes a drop as
// "put it at the end".
El* DockBindTabRest(const DockTabGroup* g, El* rest);
// TabBar::track_scroll: a row of tabs wider than the bar scrolls sideways,
// and the tab a select asked for is brought into view from where the last
// frame put it.
El* DockBindTabStrip(const DockTabGroup* g, El* strip);
// droppable(): whether this group takes a panel at all. What a theme needs in
// order to decide whether to hang a `DragOver` refinement on its tabs —
// upstream's `.when(droppable, |this| this.drag_over::<DragPanel>(..))`. The
// marker itself is no longer a question anyone asks while building: it is a
// refinement the element carries, resolved against the frame's drag the way
// GPUI resolves one in `compute_style`.
bool DockGroupDroppable(const DockTabGroup* g);
// The single-panel title row's drag (the title is what a press picks up), the
// dock toggle buttons, the zoom button and a tab's close button.
El* DockBindTitleDrag(const DockTabGroup* g, int ix, El* e);
El* DockBindToggle(const DockTabGroup* g, DockPlacement p, El* e);
El* DockBindZoom(const DockTabGroup* g, int panelIx, El* e);
El* DockBindClose(const DockTabGroup* g, int ix, El* e);
// The strip on a Dock's inner edge that resizes it. Rust's showcase skin
// stashes the DockContext on mouse down and follows the pointer from the area
// frame; the drag is base's here, so the strip only has to say it is one.
El* DockBindResizeStrip(const DockCtx* d, El* e);
// A menu the skin opens over a group: which node it belongs to, so the row it
// reports lands on the right panel.
void DockGroupOpenMenu(const DockTabGroup* g, bool open);

// DockAreaRenderer + TabGroupRenderer, as one table. A null hook falls back
// to a bare `Div`, so a skin only writes what it has an opinion about.
struct DockRenderer {
    void* data = nullptr;
    // DockAreaRenderer::frame — the area's own box, which base then fills.
    El* (*frame)(Ctx* cx, void* data) = nullptr;
    // center_frame and split_frame.
    El* (*centerFrame)(Ctx* cx, void* data) = nullptr;
    El* (*splitFrame)(Ctx* cx, void* data, int node, Axis axis) = nullptr;
    // render_split_handle: the paint inside the grab, which base sizes,
    // gives a cursor and drags.
    El* (*splitHandle)(Ctx* cx, void* data, const DockHandleCtx* h) = nullptr;
    // render_dock: one Dock's chrome around its content — the title strip,
    // the collapse affordance, the resize handle. Chrome only: the dock's own
    // box, its extent along its axis and the `flex_none` that holds it there,
    // is DockFrame's, applied by base around whatever this returns, so a skin
    // cannot misplace a dock by not knowing to size it and a skin with no
    // hook at all still gets a dock the right shape. Base never asks for a
    // dock with no extent, so a shut side dock is out of the layout before
    // the skin hears of it.
    El* (*dock)(Ctx* cx, void* data, const DockCtx* d, El* content) = nullptr;
    // TabGroupRenderer::frame, content_frame and render_tab_bar. The bar is
    // the whole of the chrome — tabs, toggles, tools, the lot.
    El* (*tabGroupFrame)(Ctx* cx, void* data, const DockTabGroup* g) = nullptr;
    El* (*tabContentFrame)(Ctx* cx, void* data,
                           const DockTabGroup* g) = nullptr;
    El* (*tabBar)(Ctx* cx, void* data, const DockTabGroup* g) = nullptr;
    // TabGroupRenderer::render_empty: what a group with nothing to show says.
    // Null leaves the content region empty, which is base's own default.
    El* (*emptyGroup)(Ctx* cx, void* data, const DockTabGroup* g) = nullptr;
    // render_drop_indicator: the paint over the half a drop would take.
    // `to` is in the group's own coordinates, already sprung.
    El* (*dropIndicator)(Ctx* cx, void* data, Bounds to) = nullptr;
    // TabPanel::render_drag_panel. Base's own DragPanel renders nothing,
    // because a preview is appearance; base places it under the pointer and
    // defers it, and this draws it.
    El* (*dragPreview)(Ctx* cx, void* data, const DockPanelDef* def) = nullptr;
};

// Rust uses three object-safe renderer traits; one C++ table carries their
// disjoint hooks without virtual dispatch or reference counting.
using DockAreaRenderer = DockRenderer;
using TabGroupRenderer = DockRenderer;
using TilesRenderer = DockRenderer;

// DockArea, as an element. The tree, the three Docks around the centre, the
// splits and their handles, each group's body and the drop placeholder are
// base's; everything drawn comes back through `r`.
//
// `r` must outlive the frame — a skin is a static table, or something the
// caller keeps, not a frame-arena temporary.
struct DockArea {
    static El* New(Ctx* cx, Str id, Entity<DockState> state,
                   const DockRenderer* r);
};

} // namespace gpui
#endif // GPUI_BASE_DOCK_H_
