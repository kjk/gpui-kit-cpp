#ifndef GPUI_BASE_VIRTUAL_LIST_H_
#define GPUI_BASE_VIRTUAL_LIST_H_
/* Unstyled virtual list host — crates/base/src/virtual_list.rs */

#include "gpui/gpui.h"
#include "base/scrollbar.h"

namespace gpui {

// Which items a scrolled list has to build. Everything outside this range is
// not rendered at all, which is the whole point of a virtual list: a
// half-million-row list costs a screenful.
//
// `sizes` is the length of each item along the scrolled axis — Rust takes a
// Size per item and reads only the one the axis names. `offset` is how far the
// list has scrolled, positive-down as El::ScrollY takes it; Rust's is negative
// because it offsets the content rather than the view.
//
// The end is exclusive and runs past what is strictly visible: the search
// stops at the first item whose end is below the bottom edge, takes that one
// in, and Rust then adds one more — so the row being scrolled into is already
// built when it arrives. When nothing crossed the bottom edge the rest of the
// list fits and all of it is taken.
struct VirtualRange {
    int first = 0;
    int end = 0;
};

VirtualRange VirtualListVisibleRange(const float* sizes, int count,
                                     float offset, float viewport);

// The hot-path form used once request_layout has already retained every
// item's origin. Both viewport edges are binary searches, matching the Rust
// visible_range helper; unlike the sizes-only compatibility seam above this
// does not scan all preceding rows every frame.
VirtualRange VirtualListVisibleRangeFromLayout(const float* origins,
                                               const float* sizes, int count,
                                               float offset, float viewport);

// The same range for a list whose items are all one size — uniform_list,
// which is what a tree renders through. Worked out by division rather than by
// scanning, so a hundred thousand rows cost nothing to skip.
VirtualRange VirtualListVisibleRows(int count, float rowSize, float offset,
                                    float viewport);

// The distance from the start of the list to item `ix` — Rust's running scan
// over the sizes, which is where each item is placed.
float VirtualListItemOrigin(const float* sizes, int count, int ix);

// gpui::ScrollStrategy, which says where scroll_to_item leaves the item it
// brought into view. The virtual list treats Top and Bottom alike — Rust
// matches only Center and lets the rest fall into one branch that scrolls as
// little as it can: an item above the view aligns with the top, one below
// aligns with the bottom, and one already in view does not move at all.
enum class ScrollStrategy : uint8_t {
    Top,
    Center,
    Bottom
};

// scroll_to_item: the offset that brings the item at `origin` into a viewport
// of `viewport`, from where it is now. Offsets run positive-down, as
// El::ScrollY takes them, and the answer is clamped to the list — there is
// nothing to see past either end.
float VirtualListScrollTo(float origin, float size, float offset,
                          float viewport, float contentSize,
                          ScrollStrategy strategy);
// The same, for the item at `ix` of a list whose items are `sizes`.
float VirtualListScrollToItem(const float* sizes, int count, int ix,
                              float offset, float viewport,
                              ScrollStrategy strategy);
// The same again, for a list whose items are all one size — which is what
// uniform_list is, and what the tree renders through.
float VirtualListScrollToRow(int count, float rowSize, int ix, float offset,
                             float viewport, ScrollStrategy strategy);

// Every item's length added up: the scrolled size of the whole list.
float VirtualListContentSize(const float* sizes, int count);

// VirtualListScrollHandle. Rust's handle is what a caller outside the list
// holds: it knows the axis and how many items there are, it carries the
// viewport and the offset of the list it is attached to, and a request to
// scroll to an item waits on it until the list is next laid out — at the
// moment the request is made nothing knows where the item is.
struct VirtualListScrollHandle {
    Axis axis = Axis::Vertical;
    int itemsCount = 0;
    // The base handle: where the list has scrolled to, how much of it is
    // showing, and how long the whole of it is. Offsets run positive-down,
    // as El::ScrollY takes them.
    float offset = 0;
    float viewport = 0;
    float contentSize = 0;
    // deferred_scroll_to_item: the request, until a layout can answer it.
    bool pending = false;
    int pendingIx = 0;
    // scroll_to_item_with_offset: how many items past the one named to bring
    // into view instead.
    int pendingOffset = 0;
    ScrollStrategy pendingStrategy = ScrollStrategy::Top;
};

// scroll_to_item / scroll_to_item_with_offset: the request is recorded and
// answered at the next layout.
void VirtualListScrollToItemDeferred(VirtualListScrollHandle* h, int ix,
                                     ScrollStrategy strategy);
void VirtualListScrollToItemDeferredWithOffset(VirtualListScrollHandle* h,
                                               int ix, ScrollStrategy strategy,
                                               int offset);
// scroll_to_bottom: the last item, at the top of the view — which is as far
// as the list goes.
void VirtualListScrollToBottomDeferred(VirtualListScrollHandle* h);

// What the list does with the handle when it lays out: the item sizes and the
// viewport it measured, then the pending request if there is one, and last
// the clamp that keeps the offset inside the list. `sizes` may be null for a
// list whose items are all `itemSize` long. Answers whether the offset moved.
bool VirtualListHandleLayout(VirtualListScrollHandle* h, const float* sizes,
                             int count, float itemSize, float viewport);

// The range the handle's offset makes visible, for the same pair of lists.
VirtualRange VirtualListHandleRange(const VirtualListScrollHandle* h,
                                    const float* sizes, int count,
                                    float itemSize);

// request_layout's retained calculation: each along-axis extent (including
// the gap after every item but the last), its cumulative origin, total content
// size and the bounds from the last layout. Rust retains this by element id;
// the C++ frame state computes the same POD-visible result while building.
struct ItemSizeLayout {
    Vec<float> sizes;
    Vec<float> origins;
    Size contentSize = {};
    Bounds lastLayoutBounds = {};

    ~ItemSizeLayout() {
        VecReset(sizes);
        VecReset(origins);
    }
};

void ItemSizeLayoutBuild(ItemSizeLayout* layout, Axis axis,
                         const float* itemSizes, int count,
                         float uniformItemSize, float gap, float crossSize);

// The per-frame state the source Element passes from request_layout through
// prepaint to paint. Elements are arena children here, so their visible range
// and spacer extents are the retained part needed by the builder.
struct VirtualListFrameState {
    VirtualRange visible = {};
    ItemSizeLayout sizeLayout;
    float scrollOffset = 0;
    float before = 0;
    float after = 0;
};

// virtual_list.rs's list itself. Rust's `v_virtual_list(entity, id, sizes,
// |_, range, _, _| ...)` hands the closure the whole visible range and takes
// back the rows; this asks for one row at a time and carries the user pointer
// an element that holds no closures needs.
using VirtualRowFn = El* (*)(void* user, Ctx* cx, int ix);
// The source constructor renders a visible range in one callback. Supplying
// this keeps script-backed lists to one VM crossing per viewport; `out` has
// exactly `end - first` slots and null entries are skipped.
using VirtualRangeFn = void (*)(void* user, Ctx* cx, int first, int end,
                                El** out);

// What the list is: how many items, how long each is, how much of it shows,
// and where it has scrolled to. `sizes` null is the uniform list, which is
// `rowH` per item.
struct VirtualListOpts {
    int count = 0;
    float rowH = 32;
    float viewH = 192;
    // The horizontal counterpart. `rowH` remains the historical name for a
    // uniform along-axis extent, so it is a width when layoutAxis is
    // horizontal.
    float viewW = 192;
    const float* sizes = nullptr;
    // The offsets, for a list without a handle. A list with one reads its
    // offset from the handle instead — `track_scroll(&handle)`.
    float scrollY = 0;
    float scrollX = 0;
    VirtualListScrollHandle* handle = nullptr;
    // The id the scroll is tracked under and where the wheel reports. Zero
    // leaves the list unscrollable by the wheel, for a caller that scrolls it
    // some other way; anything else must set both, since a region with no
    // listener is skipped and the wheel falls through to the window.
    int scrollId = 0;
    Listener onScroll = {};
    // Which bars are drawn. The axis names the bars, not the scrolling: a
    // list set to Vertical still slides sideways under the wheel, it just
    // does not draw the bar along the bottom — Rust's `.scrollbar(&handle,
    // axis)` hangs the bar layer beside the list rather than inside it.
    ScrollAxis axis = ScrollAxis::Both;
    // Source Axis: which coordinate holds item origins. This is independent
    // of `axis` above, which selects the visible scrollbar skin.
    Axis layoutAxis = Axis::Vertical;
    // Included after every item except the last, as request_layout does.
    float gap = 0;
    // The list's own inset, which behaves as CSS scroll-padding does: the two
    // ends keep their inset and a row scrolled under the edge clips flush
    // against it. `viewH` is the rows' height, so the element is this much
    // taller.
    float pad = 0;
    VirtualRowFn row = nullptr;
    VirtualRangeFn range = nullptr;
    void* user = nullptr;
};

struct VirtualList {
    // The bare box, for a caller that builds the rows itself.
    static El* New(Ctx* cx, Str id);
    // The list: only the rows the viewport can show are built, and a spacer
    // at each end stands in for the rest — without the second one the list
    // would scroll only as far as the last row it made.
    static El* New(Ctx* cx, Str id, const VirtualListOpts& o);
};

// Source-named constructors. The callback and entity closure are represented
// by VirtualListOpts::row + user under the repository's no-closure rule.
El* virtual_list(Ctx* cx, Str id, Axis axis, const VirtualListOpts& opts);
El* v_virtual_list(Ctx* cx, Str id, const VirtualListOpts& opts);
El* h_virtual_list(Ctx* cx, Str id, const VirtualListOpts& opts);
} // namespace gpui
#endif // GPUI_BASE_VIRTUAL_LIST_H_
