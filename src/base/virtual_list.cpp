#include "base/virtual_list.h"

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

El* VirtualList::New(Ctx* cx, Str id, const VirtualListOpts& o) {
    Arena* a = cx->a;
    VirtualListFrameState frame;
    float viewport = o.layoutAxis == Axis::Horizontal ? o.viewW : o.viewH;
    float crossSize = o.layoutAxis == Axis::Horizontal ? o.viewH : o.viewW;
    ItemSizeLayoutBuild(&frame.sizeLayout, o.layoutAxis, o.sizes, o.count,
                        o.rowH, o.gap, crossSize);
    // The layout is where the handle is answered: it learns how many items
    // there are and how much of them is showing, a pending scroll_to_item is
    // applied against that, and the offset is clamped to the list.
    float offset = o.layoutAxis == Axis::Horizontal ? o.scrollX : o.scrollY;
    if (o.handle) {
        o.handle->axis = o.layoutAxis;
        const float* sizes =
            frame.sizeLayout.sizes.len ? frame.sizeLayout.sizes.els : nullptr;
        VirtualListHandleLayout(o.handle, sizes, o.count, 0, viewport);
        offset = o.handle->offset;
    }
    // The rows the viewport can show, and a spacer at each end standing in
    // for the ones that were not built — without the second one the list
    // would scroll only as far as the last row it made.
    const float* layoutSizes =
        frame.sizeLayout.sizes.len ? frame.sizeLayout.sizes.els : nullptr;
    const float* layoutOrigins =
        frame.sizeLayout.origins.len ? frame.sizeLayout.origins.els : nullptr;
    frame.visible = VirtualListVisibleRangeFromLayout(
        layoutOrigins, layoutSizes, o.count, offset, viewport);
    El* list = Div(a);
    if (o.layoutAxis == Axis::Horizontal) {
        list->FlexRow();
    } else {
        list->FlexCol();
    }
    if (frame.visible.first > 0) {
        frame.before = frame.sizeLayout.origins[frame.visible.first];
        El* spacer = Div(a);
        if (o.layoutAxis == Axis::Horizontal) {
            spacer->W(frame.before);
        } else {
            spacer->H(frame.before);
        }
        list->Child(spacer);
    }
    int visibleCount = frame.visible.end - frame.visible.first;
    El** rangeRows = nullptr;
    if (o.range && visibleCount > 0) {
        rangeRows = (El**)Alloc(a, (int)(sizeof(El*) * (size_t)visibleCount));
        if (rangeRows) {
            memset(static_cast<void*>(rangeRows), 0,
                   sizeof(El*) * (size_t)visibleCount);
            o.range(o.user, cx, frame.visible.first, frame.visible.end,
                    rangeRows);
        }
    }
    for (int ix = frame.visible.first; ix < frame.visible.end; ix++) {
        El* made = rangeRows ? rangeRows[ix - frame.visible.first]
                             : (o.row ? o.row(o.user, cx, ix) : nullptr);
        if (El* built = made) {
            if (o.gap != 0 && ix + 1 < o.count) {
                El* allocation = Div(a);
                if (o.layoutAxis == Axis::Horizontal) {
                    allocation->FlexRow()->W(frame.sizeLayout.sizes[ix]);
                } else {
                    allocation->FlexCol()->H(frame.sizeLayout.sizes[ix]);
                }
                allocation->Child(built);
                built = allocation;
            }
            list->Child(built);
        }
    }
    if (frame.visible.end < o.count) {
        float content = o.layoutAxis == Axis::Horizontal
                            ? frame.sizeLayout.contentSize.w
                            : frame.sizeLayout.contentSize.h;
        frame.after = content - frame.sizeLayout.origins[frame.visible.end];
        El* spacer = Div(a);
        if (o.layoutAxis == Axis::Horizontal) {
            spacer->W(frame.after);
        } else {
            spacer->H(frame.after);
        }
        list->Child(spacer);
    }

    frame.scrollOffset = offset;
    El* e = New(cx, id)->ClipY()->ClipX();
    if (o.layoutAxis == Axis::Horizontal) {
        e->W(o.viewW + o.pad * 2)->ScrollX(offset)->ScrollY(o.scrollY);
    } else {
        e->H(o.viewH + o.pad * 2)->ScrollY(offset)->ScrollX(o.scrollX);
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
    return e->Child(list);
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
