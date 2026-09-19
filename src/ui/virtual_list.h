#ifndef GPUI_SRC_UI_VIRTUAL_LIST_H_
#define GPUI_SRC_UI_VIRTUAL_LIST_H_
/* Themed virtual list — crates/ui/src/virtual_list.rs */

#include "ui/sizing.h"
#include "ui/scroll.h"

namespace gpui {

namespace component {

struct VirtualList {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = StrL("vlist");
    int count = 0;
    float rowH = 32;
    float viewH = 192;
    float scrollY = 0;
    // The sideways offset, for a list whose rows are wider than the viewport.
    float scrollX = 0;
    // The item sizes, for a list whose rows are not all one height. Null is
    // the uniform list, which is `rowH` per row.
    const float* sizes = nullptr;
    uint8_t* needsMeasure = nullptr;
    // The handle the caller holds: where the list has scrolled to, and where
    // it has been asked to scroll. A list with one reads its offset from the
    // handle rather than from `scrollY`.
    VirtualListScrollHandle* handle = nullptr;
    Listener onRenderRow; // not used; rows built here
    El* (*row)(Ctx* cx, int ix) = nullptr;
    // C++'s equivalent of Rust's row-builder closure. `user` carries only the
    // closure environment for the duration of IntoEl; state itself stays in
    // an Entity and is resolved by the callback.
    VirtualRowFn rowWithUser = nullptr;
    void* rowUser = nullptr;
    // The scroll id and the listener a scrolled list needs to hear the wheel.
    int scrollId = 0;
    Listener onScroll = {};
    // ScrollbarAxis: which bars the list shows. Rust hangs the bars off the
    // list with `.scrollbar(&handle, axis)`, which adds a bar layer per axis
    // named — it does not decide what scrolls, so a list showing one bar
    // still takes the wheel both ways.
    ScrollAxis axis = ScrollAxis::Both;
    // The list's own inset. Rust's command palette puts `p_1` on the virtual
    // list rather than on the box around it, where it behaves as CSS
    // scroll-padding does: the two ends of the scroll keep their inset, and a
    // row scrolled under the edge clips flush against it rather than against
    // a padded box. The clip is the element's border box, so the padding is
    // outside it and the height given is the rows' — the element is that much
    // taller.
    float pad = 0;

    static VirtualList* New(Ctx* cx, int count);
    VirtualList* Id(Str v);
    VirtualList* RowH(float v);
    VirtualList* ViewH(float v);
    VirtualList* ScrollY(float v);
    VirtualList* ScrollX(float v);
    VirtualList* Sizes(const float* v);
    VirtualList* MeasureRows(uint8_t* flags);
    VirtualList* Handle(VirtualListScrollHandle* h);
    VirtualList* Scroll(int id, Listener onScroll);
    VirtualList* Axis(ScrollAxis v);
    VirtualList* Pad(float v);
    // The row builder. Rust's is a closure that captured `cx`, so this takes
    // one: a row that reads the theme has no other way to.
    VirtualList* Row(El* (*fn)(Ctx*, int));
    VirtualList* Row(VirtualRowFn fn, void* user);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_VIRTUAL_LIST_H_
