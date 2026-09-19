#include "base/virtual_list.h"

#include <string.h>

namespace gpui {

static int VirtualListEndsBefore(const float* origins, const float* sizes,
                                 int count, float edge) {
    int low = 0;
    int high = count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (origins[mid] + sizes[mid] <= edge) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

VirtualRange VirtualListVisibleRangeFromLayout(const float* origins,
                                               const float* sizes, int count,
                                               float offset, float viewport) {
    VirtualRange r;
    if (!origins || !sizes || count <= 0) {
        return r;
    }
    r.first = VirtualListEndsBefore(origins, sizes, count, offset);
    int pastEnd =
        VirtualListEndsBefore(origins, sizes, count, offset + viewport);
    r.end = pastEnd == count ? count : pastEnd + 2;
    if (r.end > count) {
        r.end = count;
    }
    if (r.end < r.first) {
        r.end = r.first;
    }
    return r;
}

VirtualRange VirtualListVisibleRange(const float* sizes, int count,
                                     float offset, float viewport) {
    VirtualRange r;
    if (count <= 0) {
        return r;
    }
    // The first item whose end has passed the top edge.
    float cumulative = 0;
    for (int i = 0; i < count; i++) {
        cumulative += sizes[i];
        if (cumulative > offset) {
            r.first = i;
            break;
        }
    }
    // The first whose end has passed the bottom edge. Rust counts this one in
    // and then adds one more, so the row being scrolled into is already built
    // by the time it arrives.
    cumulative = 0;
    int last = 0;
    for (int i = 0; i < count; i++) {
        cumulative += sizes[i];
        if (cumulative > offset + viewport) {
            last = i + 1;
            break;
        }
    }
    if (last == 0) {
        // Nothing crossed the bottom edge, so the rest of the list fits.
        last = count;
    } else {
        last += 1;
    }
    r.end = last < count ? last : count;
    return r;
}

VirtualRange VirtualListVisibleRows(int count, float rowSize, float offset,
                                    float viewport) {
    VirtualRange r;
    if (count <= 0 || rowSize <= 0) {
        return r;
    }
    // The first row whose end has passed the top edge. Scrolled past the end
    // of the list there is no such row, and Rust's scan leaves the first at
    // zero rather than clamping.
    int first = (int)(offset / rowSize);
    if (first < 0 || first >= count) {
        first = 0;
    }
    r.first = first;
    // The first whose end has passed the bottom edge, counted in, plus the
    // spare Rust adds after it.
    int last = (int)((offset + viewport) / rowSize);
    if (last < 0) {
        last = 0;
    }
    int end = last >= count ? count : last + 2;
    r.end = end < count ? end : count;
    if (r.end < r.first) {
        r.end = r.first;
    }
    return r;
}

float VirtualListItemOrigin(const float* sizes, int count, int ix) {
    float at = 0;
    int n = ix < count ? ix : count;
    for (int i = 0; i < n; i++) {
        at += sizes[i];
    }
    return at;
}

float VirtualListContentSize(const float* sizes, int count) {
    return VirtualListItemOrigin(sizes, count, count);
}

float VirtualListPixelFromLogical(const float* sizes, int count, int item,
                                  float into) {
    if (item < 0) item = 0;
    if (item > count) item = count;
    float pixel = 0;
    if (sizes) {
        for (int i = 0; i < item && i < count; i++) {
            if (sizes[i] <= 0) return pixel;
            pixel += sizes[i];
        }
    }
    return pixel + (into > 0 ? into : 0);
}

void VirtualListLogicalFromPixel(const float* sizes, int count, float rowH,
                                 float pixel, int* item, float* into) {
    if (pixel < 0) pixel = 0;
    int ix = 0;
    float rest = pixel;
    if (sizes) {
        while (ix < count) {
            float h = sizes[ix];
            if (h <= 0) break;
            if (rest < h) break;
            rest -= h;
            ix++;
        }
    } else if (rowH > 0 && count > 0) {
        ix = (int)(pixel / rowH);
        if (ix >= count) ix = count - 1;
        if (ix < 0) ix = 0;
        rest = pixel - (float)ix * rowH;
    }
    if (item) *item = ix;
    if (into) *into = rest;
}

float VirtualListScrollTo(float origin, float size, float offset,
                          float viewport, float contentSize,
                          ScrollStrategy strategy) {
    float want = offset;
    if (strategy == ScrollStrategy::Center) {
        want = origin + size * 0.5f - viewport * 0.5f;
    } else if (origin < offset) {
        want = origin;
    } else if (origin + size > offset + viewport) {
        want = origin + size - viewport;
    }
    float most = contentSize - viewport;
    if (want > most) {
        want = most;
    }
    return want < 0 ? 0 : want;
}

float VirtualListScrollToItem(const float* sizes, int count, int ix,
                              float offset, float viewport,
                              ScrollStrategy strategy) {
    if (ix < 0 || ix >= count) {
        return offset;
    }
    return VirtualListScrollTo(VirtualListItemOrigin(sizes, count, ix),
                               sizes[ix], offset, viewport,
                               VirtualListContentSize(sizes, count), strategy);
}

float VirtualListScrollToRow(int count, float rowSize, int ix, float offset,
                             float viewport, ScrollStrategy strategy) {
    if (ix < 0 || ix >= count) {
        return offset;
    }
    return VirtualListScrollTo((float)ix * rowSize, rowSize, offset, viewport,
                               (float)count * rowSize, strategy);
}

void VirtualListScrollToItemDeferred(VirtualListScrollHandle* h, int ix,
                                     ScrollStrategy strategy) {
    VirtualListScrollToItemDeferredWithOffset(h, ix, strategy, 0);
}

void VirtualListScrollToItemDeferredWithOffset(VirtualListScrollHandle* h,
                                               int ix, ScrollStrategy strategy,
                                               int offset) {
    if (!h) {
        return;
    }
    h->pending = true;
    h->pendingIx = ix;
    h->pendingStrategy = strategy;
    h->pendingOffset = offset;
}

void VirtualListScrollToBottomDeferred(VirtualListScrollHandle* h) {
    if (!h) {
        return;
    }
    // saturating_sub: an empty list scrolls to item 0, which is nowhere.
    int last = h->itemsCount > 0 ? h->itemsCount - 1 : 0;
    VirtualListScrollToItemDeferred(h, last, ScrollStrategy::Top);
}

bool VirtualListHandleLayout(VirtualListScrollHandle* h, const float* sizes,
                             int count, float itemSize, float viewport) {
    if (!h) {
        return false;
    }
    float before = h->offset;
    h->itemsCount = count;
    h->viewport = viewport;
    h->contentSize =
        sizes ? VirtualListContentSize(sizes, count) : (float)count * itemSize;
    if (h->pending) {
        // The request is taken, whether or not there is an item to answer it
        // with: Rust's `take()` clears it either way.
        h->pending = false;
        int ix = h->pendingIx + h->pendingOffset;
        if (ix >= 0 && ix < count) {
            h->offset =
                sizes ? VirtualListScrollToItem(sizes, count, ix, h->offset,
                                                viewport, h->pendingStrategy)
                      : VirtualListScrollToRow(count, itemSize, ix, h->offset,
                                               viewport, h->pendingStrategy);
        }
    }
    // The clamp: there is nothing to see past either end, however the offset
    // got there — a list that shrank under a scrolled view comes back.
    float most = h->contentSize - viewport;
    if (h->offset > most) {
        h->offset = most;
    }
    if (h->offset < 0) {
        h->offset = 0;
    }
    return h->offset != before;
}

VirtualRange VirtualListHandleRange(const VirtualListScrollHandle* h,
                                    const float* sizes, int count,
                                    float itemSize) {
    if (!h) {
        return {};
    }
    if (sizes) {
        return VirtualListVisibleRange(sizes, count, h->offset, h->viewport);
    }
    return VirtualListVisibleRows(count, itemSize, h->offset, h->viewport);
}

void ItemSizeLayoutBuild(ItemSizeLayout* layout, Axis axis,
                         const float* itemSizes, int count,
                         float uniformItemSize, float gap, float crossSize) {
    if (!layout) {
        return;
    }
    layout->sizes.len = 0;
    layout->origins.len = 0;
    layout->contentSize = {};
    float origin = 0;
    for (int i = 0; i < count; i++) {
        float item = itemSizes ? itemSizes[i] : uniformItemSize;
        float extent = item + (i + 1 < count ? gap : 0.f);
        VecAppend(layout->origins, origin);
        VecAppend(layout->sizes, extent);
        origin += extent;
    }
    if (axis == Axis::Horizontal) {
        layout->contentSize.w = origin;
        layout->contentSize.h = crossSize;
    } else {
        layout->contentSize.w = crossSize;
        layout->contentSize.h = origin;
    }
}

struct VirtualListPaint {
    VirtualListOpts opts = {};
    Arena* a = nullptr;
    App* app = nullptr;
    Window* win = nullptr;
};

static El* VirtualListTakeRow(const VirtualListOpts& o, El** rangeRows,
                              int first, int ix, Ctx* cx) {
    if (rangeRows) return rangeRows[ix - first];
    if (o.row) return o.row(o.user, cx, ix);
    return nullptr;
}

static void VirtualListPlace(PaintCtx* ctx, El* e, El* made, Axis axis,
                             float pad, float innerW, float innerH,
                             float origin, float offset, float extent) {
    float x = e->x + pad;
    float y = e->y + pad;
    float w = innerW;
    float h = innerH;
    if (axis == Axis::Horizontal) {
        x += origin - offset;
        w = extent;
    } else {
        y += origin - offset;
        h = extent;
    }
    LayoutEl(ctx, made, x, y, w, h, 0, {});
    e->Child(made);
}

static float VirtualListMeasureItem(PaintCtx* ctx, El* made, Axis axis,
                                    float fallback) {
    Size got = MeasureEl(ctx, made);
    float extent = axis == Axis::Horizontal ? got.w : got.h;
    if (extent <= 0) extent = fallback > 0 ? fallback : 1.f;
    return extent;
}

static void VirtualListBindRows(PaintCtx* ctx, El* e, VirtualListPaint* paint,
                                ItemSizeLayout* layout, float offset,
                                float viewport, float pad, float innerW,
                                float innerH) {
    const VirtualListOpts& o = paint->opts;
    Axis axis = o.layoutAxis;
    Ctx cx = {};
    cx.app = paint->app;
    cx.win = paint->win;
    cx.a = paint->a;
    e->first = nullptr;
    e->last = nullptr;
    if (o.logicalScroll) {
        int ix = o.topItem;
        if (ix < 0) ix = 0;
        float along = -o.topInto;
        float origin = offset + along;
        while (ix < o.count && along < viewport + o.overdraw) {
            El* made = nullptr;
            if (o.row) {
                made = o.row(o.user, &cx, ix);
            } else if (o.range) {
                El* one = nullptr;
                o.range(o.user, &cx, ix, ix + 1, &one);
                made = one;
            }
            if (!made) {
                if (!cx.a) break;
                made = Div(cx.a);
            }
            float extent = ix < layout->sizes.len ? layout->sizes[ix] : o.rowH;
            if (extent <= 0) {
                extent = VirtualListMeasureItem(ctx, made, axis, o.rowH);
                if (o.sizes && ix < o.count) {
                    const_cast<float*>(o.sizes)[ix] = extent;
                }
                if (ix < layout->sizes.len) layout->sizes[ix] = extent;
            }
            VirtualListPlace(ctx, e, made, axis, pad, innerW, innerH, origin,
                             offset, extent);
            along += extent;
            origin += extent;
            ix++;
        }
        return;
    }
    VirtualRange vis = VirtualListVisibleRangeFromLayout(
        layout->origins.len ? layout->origins.els : nullptr,
        layout->sizes.len ? layout->sizes.els : nullptr, o.count, offset,
        viewport);
    int visibleCount = vis.end - vis.first;
    El** rangeRows = nullptr;
    if (o.range && visibleCount > 0 && cx.a) {
        rangeRows =
            (El**)Alloc(cx.a, (int)(sizeof(El*) * (size_t)visibleCount));
        if (rangeRows) {
            memset(static_cast<void*>(rangeRows), 0,
                   sizeof(El*) * (size_t)visibleCount);
            o.range(o.user, &cx, vis.first, vis.end, rangeRows);
        }
    }
    for (int ix = vis.first; ix < vis.end; ix++) {
        El* made = VirtualListTakeRow(o, rangeRows, vis.first, ix, &cx);
        if (!made) {
            if (!cx.a) continue;
            made = Div(cx.a);
        }
        float extent = ix < layout->sizes.len ? layout->sizes[ix] : o.rowH;
        float origin =
            ix < layout->origins.len ? layout->origins[ix] : (float)ix * o.rowH;
        VirtualListPlace(ctx, e, made, axis, pad, innerW, innerH, origin,
                         offset, extent);
    }
}

static void VirtualListPrePaint(PaintCtx* ctx, El* e, void* user) {
    auto* paint = (VirtualListPaint*)user;
    if (!paint || !e || !paint->a) return;
    VirtualListOpts& o = paint->opts;
    float pad = o.pad;
    float innerW = e->w - pad * 2;
    float innerH = e->h - pad * 2;
    if (innerW < 0) innerW = 0;
    if (innerH < 0) innerH = 0;
    Axis axis = o.layoutAxis;
    float viewport = axis == Axis::Horizontal ? innerW : innerH;
    float cross = axis == Axis::Horizontal ? innerH : innerW;

    ItemSizeLayout layout;
    ItemSizeLayoutBuild(&layout, axis, o.sizes, o.count, o.rowH, o.gap, cross);
    float offset = axis == Axis::Horizontal ? e->scrollX : e->scrollY;
    if (o.logicalScroll) {
        offset =
            VirtualListPixelFromLogical(o.sizes, o.count, o.topItem, o.topInto);
        if (axis == Axis::Horizontal)
            e->scrollX = offset;
        else
            e->scrollY = offset;
    }
    const float* sizes = layout.sizes.len ? layout.sizes.els : nullptr;
    bool hadPending = o.handle && o.handle->pending;
    int pendingIx = hadPending ? o.handle->pendingIx : 0;
    int pendingOffset = hadPending ? o.handle->pendingOffset : 0;
    ScrollStrategy pendingStrategy = hadPending
                                         ? o.handle->pendingStrategy
                                         : ScrollStrategy::Top;
    if (o.handle) {
        o.handle->axis = axis;
        VirtualListHandleLayout(o.handle, sizes, o.count, 0, viewport);
        offset = o.handle->offset;
        if (axis == Axis::Horizontal)
            e->scrollX = offset;
        else
            e->scrollY = offset;
    }
    if (o.needsMeasure && o.sizes && o.row && o.count > 0) {
        // GPUI's ListState measures a row as it enters the overdraw range.
        // Start with the provisional extents to find that range, then rebuild
        // origins and the pending scroll request from the actual row boxes.
        // The handle request was consumed above; a following-tail request
        // needs the newly measured content size before it resolves.
        Ctx rowCx = {};
        rowCx.app = paint->app;
        rowCx.win = paint->win;
        rowCx.a = paint->a;
        // A newly measured group can shrink enough to expose more rows.
        // Continue until the entire visible range has measured extents.
        for (int pass = 0; pass < o.count; pass++) {
            VirtualRange range = VirtualListVisibleRangeFromLayout(
                layout.origins.els, layout.sizes.els, o.count,
                offset > o.overdraw ? offset - o.overdraw : 0,
                viewport + o.overdraw * 2);
            int anchor = VirtualListVisibleRangeFromLayout(
                layout.origins.els, layout.sizes.els, o.count, offset, 0).first;
            float anchorOrigin = anchor < layout.origins.len
                                     ? layout.origins[anchor] : 0;
            bool changed = false;
            for (int ix = range.first; ix < range.end; ix++) {
                if (!o.needsMeasure[ix]) continue;
                El* row = o.row(o.user, &rowCx, ix);
                if (!row) continue;
                Size measured = MeasureElAtWidth(ctx, row, cross);
                if (measured.h <= 0) continue;
                const_cast<float*>(o.sizes)[ix] = measured.h;
                o.needsMeasure[ix] = 0;
                changed = true;
            }
            if (!changed) break;
            ItemSizeLayoutBuild(&layout, axis, o.sizes, o.count, o.rowH,
                                o.gap, cross);
            if (o.handle) {
                if (!hadPending && anchor < layout.origins.len)
                    o.handle->offset += layout.origins[anchor] - anchorOrigin;
                if (hadPending) {
                    o.handle->pending = true;
                    o.handle->pendingIx = pendingIx;
                    o.handle->pendingOffset = pendingOffset;
                    o.handle->pendingStrategy = pendingStrategy;
                }
                VirtualListHandleLayout(o.handle, layout.sizes.els, o.count,
                                        0, viewport);
                offset = o.handle->offset;
                if (axis == Axis::Horizontal) e->scrollX = offset;
                else e->scrollY = offset;
            }
        }
    }
    float content =
        axis == Axis::Horizontal ? layout.contentSize.w : layout.contentSize.h;
    if (axis == Axis::Horizontal) {
        e->contentW = content > innerW ? content : innerW;
        e->contentH = innerH;
    } else {
        e->contentH = content > innerH ? content : innerH;
        e->contentW = innerW;
    }
    VirtualListBindRows(ctx, e, paint, &layout, offset, viewport, pad, innerW,
                        innerH);
    if (o.logicalScroll && o.sizes) {
        float measured = VirtualListContentSize(o.sizes, o.count);
        if (axis == Axis::Horizontal) {
            e->contentW = measured > innerW ? measured : innerW;
        } else {
            e->contentH = measured > innerH ? measured : innerH;
        }
    }
}

El* VirtualList::New(Ctx* cx, Str id, const VirtualListOpts& o) {
    Arena* a = cx->a;
    float viewport = o.layoutAxis == Axis::Horizontal ? o.viewW : o.viewH;
    float offset = o.layoutAxis == Axis::Horizontal ? o.scrollX : o.scrollY;
    if (o.logicalScroll) {
        offset =
            VirtualListPixelFromLogical(o.sizes, o.count, o.topItem, o.topInto);
    }
    if (o.handle && viewport > 0 && !o.needsMeasure) {
        o.handle->axis = o.layoutAxis;
        ItemSizeLayout layout;
        float cross = o.layoutAxis == Axis::Horizontal ? o.viewH : o.viewW;
        ItemSizeLayoutBuild(&layout, o.layoutAxis, o.sizes, o.count, o.rowH,
                            o.gap, cross);
        const float* sizes = layout.sizes.len ? layout.sizes.els : nullptr;
        VirtualListHandleLayout(o.handle, sizes, o.count, 0, viewport);
        offset = o.handle->offset;
    }
    auto* paint = ArenaNew<VirtualListPaint>(a);
    paint->opts = o;
    paint->a = a;
    paint->app = cx->app;
    paint->win = cx->win;
    El* e = New(cx, id)->ClipY()->ClipX();
    if (o.layoutAxis == Axis::Horizontal) {
        e->ScrollX(offset)->ScrollY(o.scrollY);
        if (o.viewW > 0)
            e->W(o.viewW + o.pad * 2);
        else
            e->W(kFill);
        if (o.viewH > 0)
            e->H(o.viewH);
        else
            e->H(kFill);
    } else {
        e->ScrollY(offset)->ScrollX(o.scrollX);
        if (o.viewH > 0)
            e->H(o.viewH + o.pad * 2);
        else
            e->H(kFill);
        if (o.viewW > 0)
            e->W(o.viewW);
        else
            e->W(kFill);
    }
    if (o.pad > 0) {
        e->Pad(o.pad);
    }
    if (o.axis == ScrollAxis::Vertical) {
        e->HideScrollbarX();
    } else if (o.axis == ScrollAxis::Horizontal) {
        e->HideScrollbarY();
    }
    if (o.scrollId) {
        e->ScrollId(o.scrollId)->OnScroll(o.onScroll);
    }
    e->prePaint = &VirtualListPrePaint;
    e->customUser = paint;
    return e;
}

El* VirtualList::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    return Div(a)->Id(id);
}

El* virtual_list(Ctx* cx, Str id, Axis axis, const VirtualListOpts& opts) {
    VirtualListOpts copy = opts;
    copy.layoutAxis = axis;
    return VirtualList::New(cx, id, copy);
}

El* v_virtual_list(Ctx* cx, Str id, const VirtualListOpts& opts) {
    return virtual_list(cx, id, Axis::Vertical, opts);
}

El* h_virtual_list(Ctx* cx, Str id, const VirtualListOpts& opts) {
    return virtual_list(cx, id, Axis::Horizontal, opts);
}
} // namespace gpui
