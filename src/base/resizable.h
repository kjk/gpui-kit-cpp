#ifndef GPUI_BASE_RESIZABLE_H_
#define GPUI_BASE_RESIZABLE_H_
/* Unstyled resizable — crates/base/src/resizable */

#include "gpui/gpui.h"
#include "base/geometry.h"

namespace gpui {

// PANEL_MIN_SIZE. A panel never shrinks below this unless its own range says
// otherwise.
const float PANEL_MIN_SIZE = 100.f;
const float kResizablePanelMinSize = PANEL_MIN_SIZE;

// resize_panel_at_handle: set panel `ix` to `size` and settle the rest.
//
// `ix` names the handle between panel ix and ix + 1, so the last panel has
// none and answers false. Growing takes the space from the panels after it,
// one at a time, each down to its own minimum; shrinking gives it back to the
// panel immediately after and pulls from the ones before if that is not
// enough. A total that still overruns the container comes off the panel that
// was dragged, which is Rust's last correction.
//
// `sizes` is read and written in place. `mins` and `maxs` are the per-panel
// range — pass null for either to use kResizablePanelMinSize and no ceiling.
// Answers false when there was nothing to do, which is Rust's early return.
bool ResizablePanelResize(float* sizes, const float* mins, const float* maxs,
                          int n, int ix, float size, float containerSize);

// adjust_to_container_size: every panel keeps the share it had when the
// container changes size.
void ResizableAdjustToContainer(float* sizes, int n, float containerSize);

// HANDLE_SIZE and HANDLE_PADDING: a hairline with four DIPs of grab either
// side of it, sitting over the boundary rather than taking room from it.
const float kResizeHandleSize = 1.f;
const float kResizeHandlePadding = 4.f;

namespace base_theme {
struct Theme;
}
Rgba ResizableHandleColor(const base_theme::Theme& theme, bool active);

// ResizeHandleState: how far the pointer has gone with a resize handle. A
// drag takes the pointer out of the handle's own band within a pixel or two,
// so hover cannot stand in for Dragging; the handle tracks the progression.
enum class ResizeHandleState : uint8_t {
    // The pointer is somewhere else.
    Idle,
    // The pointer is over the handle's band.
    Hovered,
    // The pointer went down on the handle and has not moved since.
    Pressed,
    // The handle is being dragged.
    Dragging,
};

// ResizeHandleState::is_active: pressed on it, or dragging it.
inline bool ResizeHandleStateIsActive(ResizeHandleState s) {
    return s == ResizeHandleState::Pressed || s == ResizeHandleState::Dragging;
}

// HandleEdge: which edge of its own container a handle hugs. A handle named
// no edge straddles the boundary it resizes, half its band on either side;
// one named an edge sits wholly inside. Its hairline stays on the boundary
// itself -- the container's outermost pixel, the one the neighbour's content
// butts up against -- and what a renderer centres on it overhangs the
// boundary, so the renderer's element is painted deferred, out of the
// container's clip.
enum class HandleEdge : uint8_t {
    // Where the axis starts: the left edge for a horizontal handle, the top
    // for a vertical one.
    Leading,
    // Where the axis ends.
    Trailing,
};

// SharedHandleState: one handle's ResizeHandleState, kept where Rust keeps it
// (`window.with_element_state`, here an element-state entity) and shared by
// every listener the handle registers. It is the handle's own and not the
// group's, which is why a handle does not have to be told which of the
// group's boundaries it is in order to know whether it is the one being
// dragged.
struct SharedHandleState {
    ResizeHandleState state = ResizeHandleState::Idle;
    // GPUI's `window.on_mouse_event` registers a listener; the port's element
    // carries one per event, and the handle's own answer is not the only one
    // the event has to reach. So the group's goes through here.
    Listener nextDown;
    Listener nextUp;
    Listener nextDrag;

    ResizeHandleState Get() const { return state; }
    // Answers whether the state actually changed, so a listener repaints only
    // when there is something new to paint.
    bool Set(ResizeHandleState next) {
        bool changed = state != next;
        state = next;
        return changed;
    }

    static void OnDown(SharedHandleState* self, Ctx* cx,
                       const MouseDownEvent* ev);
    // The pointer entering or leaving the band.
    static void OnHover(SharedHandleState* self, Ctx* cx, const HoverEvent* ev);
    // A press that moves is a drag, and stays one until the button is up.
    static void OnMove(SharedHandleState* self, Ctx* cx,
                       const MouseMoveEvent* ev);
    static void OnDragMove(SharedHandleState* self, Ctx* cx,
                           const DragMoveEvent* ev);
    // Released over the handle: it stays hovered.
    static void OnUp(SharedHandleState* self, Ctx* cx, const MouseUpEvent* ev);
    // Released anywhere else: idle.
    static void OnUpOut(SharedHandleState* self, Ctx* cx,
                        const MouseUpEvent* ev);
};

// `with_element_state` for one handle, named among the parts of whatever it
// is being built inside.
Entity<SharedHandleState> ResizeHandleStateFor(Ctx* cx, Str name);

// Wire an element as a handle band: press, hover, move, drag and release all
// go through the handle's shared state.
void ResizeHandleBindState(El* handle, Entity<SharedHandleState> state);

// ResizeHandleContext / ResizeHandleRenderer. Rust retains an Rc closure;
// Base uses a caller-owned payload and a function pointer. Returning null
// keeps the built-in one-pixel line.
struct ResizeHandleContext {
    Axis axis = Axis::Horizontal;
    ResizeHandleState state = ResizeHandleState::Idle;

    Axis AxisValue() const { return axis; }
    // Whether the pointer currently owns this handle.
    bool IsActive() const { return ResizeHandleStateIsActive(state); }
    // How far the pointer has gone with this handle.
    ResizeHandleState State() const { return state; }
};

using ResizeHandleRenderer = El* (*)(void* user,
                                     const ResizeHandleContext* context,
                                     Ctx* cx);

// The source resize_handle element, usable outside a panel group as Dock does.
struct ResizeHandle {
    Ctx* cx = nullptr;
    Str id = {};
    Axis axis = Axis::Horizontal;
    HandleEdge edge = HandleEdge::Leading;
    bool hasEdge = false;
    Listener onDrag = {};
    void* appearanceUser = nullptr;
    ResizeHandleRenderer appearance = nullptr;
    Rgba color = {};
    Rgba activeColor = {};

    static ResizeHandle* New(Ctx* cx, Str id, Axis axis);
    // Keep the whole handle inside its container, hugging `edge`, instead of
    // straddling the boundary it resizes.
    ResizeHandle* Inside(HandleEdge value);
    ResizeHandle* OnDrag(Listener listener);
    ResizeHandle* WithAppearance(void* user, ResizeHandleRenderer renderer);
    ResizeHandle* Colors(Rgba rest, Rgba active);
    El* IntoEl();
};

ResizeHandle* resize_handle(Ctx* cx, Str id, Axis axis);

// ResizableState. Rust keeps the sizes, the per-panel range and the axis on
// the state and re-derives a panel's size from it every frame; so does this.
// The panels are declared by the caller each frame, which is what fills the
// three arrays in — a page that changes how many panels it has gets the sizes
// it declared, and one that does not keeps what the drags left.
struct ResizableState {
    Axis axis = Axis::Horizontal;
    // One entry per panel, in the order they are declared.
    Vec<float> sizes;
    Vec<float> mins;
    Vec<float> maxs;
    // Which panels take a share of what is left over — Rust's panels carry
    // `flex_grow: 1` unless a caller cancels it with `flex_none()`. One entry
    // per panel, like the three above.
    Vec<bool> grows;
    // resizable_panel().visible(false). The slot keeps its place and its size
    // while nothing is drawn for it, so showing the panel again brings back
    // the width a drag left it at.
    Vec<bool> shown;
    // Where the layout put each panel on the last frame. A panel with no size
    // of its own is declared as a flex item and measured, and what comes back
    // is its size from then on — `update_panel_size`, one frame later, since
    // a box reports where it landed after it has been laid out.
    Vec<Bounds> laid;
    // The box the panels lie in, written at paint. `adjust_to_container_size`
    // keeps every panel's share of it when it changes.
    Bounds bounds = {};
    float lastContainer = 0;
    // The handle being dragged, or -1. `ix` is the boundary between panel ix
    // and ix + 1.
    int dragging = -1;
    // ResizablePanelEvent::Resized, once the drag ends.
    Listener onResized = {};

    ~ResizableState() {
        VecReset(sizes);
        VecReset(mins);
        VecReset(maxs);
        VecReset(grows);
        VecReset(shown);
        VecReset(laid);
    }

    static void OnHandleDown(ResizableState* self, Ctx* cx,
                             const MouseDownEvent* ev, intptr_t ix);
    static void OnHandleDrag(ResizableState* self, Ctx* cx,
                             const DragMoveEvent* ev);
    static void OnHandleUp(ResizableState* self, Ctx* cx,
                           const MouseUpEvent* ev);
    static void OnSettled(ResizableState* self, Ctx* cx, const void*);

    const Vec<float>& Sizes() const { return sizes; }
    float ContainerSize() const {
        return AxisIsHorizontal(axis) ? bounds.w : bounds.h;
    }
    bool ResizePanel(Ctx* cx, int ix, float size);
    bool InsertPanel(Ctx* cx, float size = PANEL_MIN_SIZE, int ix = -1);
    bool RemovePanel(Ctx* cx, int ix);
    bool ResetPanel(Ctx* cx, int ix);
    void Clear();
};

struct ResizablePanelEvent {
    const float* sizes = nullptr;
    int count = 0;
};

// The size panel `ix` is drawn at: what the state holds, or what the caller
// declared until a drag has moved it.
float ResizablePanelSize(const ResizableState* s, int ix, float declared);

// ResizablePanelGroup — `h_resizable(id)` / `v_resizable(id)` with
// `resizable_panel()` children. The group owns the panels' sizes, the handle
// between each pair and the drag that moves it. Rust keeps the whole of this
// in `crates/base` and has no themed counterpart at all, because the only
// thing a theme has to say about it is what colour the hairline is. Explicit
// `HandleColors` win; otherwise Base resolves border/ring semantic tokens.
struct ResizablePanelGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<ResizableState> state = {};
    gpui::Axis groupAxis = gpui::Axis::Horizontal;
    float width = kFill;
    float height = kFill;
    // The hairline over each boundary, at rest and while it is being dragged.
    Rgba handleColor = {};
    Rgba handleDragColor = {};
    // One per panel, in declaration order.
    ArenaVec<El*> panels;
    ArenaVec<float> sizes;
    ArenaVec<float> mins;
    ArenaVec<float> maxs;
    ArenaVec<bool> grows;
    ArenaVec<bool> shown;
    void* handleAppearanceUser = nullptr;
    ResizeHandleRenderer handleAppearance = nullptr;
    Listener onResize = {};

    // `h_resizable(id)` / `v_resizable(id)`. The state is optional, as
    // `.state(..)` is upstream: a group left to itself keys its own off the
    // id, and only a caller that means to resize the panels itself -- from a
    // button, rather than from the handle -- has to hold one.
    static ResizablePanelGroup* New(Ctx* cx, Str id,
                                    Entity<ResizableState> state = {},
                                    Axis axis = Axis::Horizontal);
    ResizablePanelGroup* W(float v);
    ResizablePanelGroup* H(float v);
    ResizablePanelGroup* WithState(Entity<ResizableState> value);
    ResizablePanelGroup* Axis(gpui::Axis value);
    ResizablePanelGroup* Size(float v);
    ResizablePanelGroup* HandleColors(Rgba rest, Rgba dragging);
    ResizablePanelGroup* WithHandleAppearance(void* user,
                                              ResizeHandleRenderer renderer);
    ResizablePanelGroup* OnResize(Listener listener);
    // A panel of a fixed starting size, with the range a drag keeps it in.
    // `max` of 0 is Rust's `Pixels::MAX` — no ceiling.
    ResizablePanelGroup* Panel(El* content, float size,
                               float min = kResizablePanelMinSize,
                               float max = 0);
    // The panel that takes what the others leave: `panel_box(..)` handed to
    // the group as a plain child, which becomes a panel with no size of its
    // own.
    ResizablePanelGroup* Grow(El* content, float min = kResizablePanelMinSize);
    // The panel last declared keeps its size *and* takes a share of the
    // slack — a `resizable_panel()` that never called `flex_none()`, whose
    // internal `flex_grow: 1` stands.
    ResizablePanelGroup* Flex();
    // resizable_panel().visible(v), on the panel last declared. A hidden
    // panel draws nothing, has no handle on the boundary before it, and is
    // left out of the arithmetic — but keeps its slot and its size.
    ResizablePanelGroup* Visible(bool v);
    ResizablePanelGroup* Child(struct ResizablePanel* panel);
    ResizablePanelGroup* Children(struct ResizablePanel** values, int count);
    El* IntoEl();
};

using Resizable = ResizablePanelGroup;

struct ResizablePanel {
    Ctx* cx = nullptr;
    El* content = nullptr;
    float size = 0;
    float min = PANEL_MIN_SIZE;
    float max = 0;
    bool grow = true;
    bool visible = true;

    static ResizablePanel* New(Ctx* cx);
    ResizablePanel* Child(El* value);
    ResizablePanel* Size(float value);
    ResizablePanel* SizeRange(float minValue, float maxValue = 0);
    ResizablePanel* FlexNone();
    ResizablePanel* Visible(bool value);
};

ResizablePanelGroup* h_resizable(Ctx* cx, Str id,
                                 Entity<ResizableState> state = {});
ResizablePanelGroup* v_resizable(Ctx* cx, Str id,
                                 Entity<ResizableState> state = {});
ResizablePanel* resizable_panel(Ctx* cx);
} // namespace gpui
#endif // GPUI_BASE_RESIZABLE_H_
