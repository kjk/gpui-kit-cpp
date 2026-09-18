#include "ui/virtual_list.h"

namespace gpui {

namespace component {

VirtualList* VirtualList::New(Ctx* cx, int count) {
    Arena* a = cx->a;
    VirtualList* v = ArenaNew<VirtualList>(a);
    v->a = a;
    v->cx = cx;
    v->count = count;
    return v;
}
VirtualList* VirtualList::Id(Str v) {
    id = v;
    return this;
}
VirtualList* VirtualList::RowH(float v) {
    rowH = v;
    return this;
}
VirtualList* VirtualList::ViewH(float v) {
    viewH = v;
    return this;
}
VirtualList* VirtualList::ScrollY(float v) {
    scrollY = v;
    return this;
}
VirtualList* VirtualList::ScrollX(float v) {
    scrollX = v;
    return this;
}
VirtualList* VirtualList::Sizes(const float* v) {
    sizes = v;
    return this;
}
VirtualList* VirtualList::Handle(VirtualListScrollHandle* h) {
    handle = h;
    return this;
}
VirtualList* VirtualList::Scroll(int sid, Listener l) {
    scrollId = sid;
    onScroll = l;
    return this;
}
VirtualList* VirtualList::Axis(ScrollAxis v) {
    axis = v;
    return this;
}
VirtualList* VirtualList::Pad(float v) {
    pad = v;
    return this;
}
VirtualList* VirtualList::Row(El* (*fn)(Ctx*, int)) {
    row = fn;
    rowWithUser = nullptr;
    rowUser = nullptr;
    return this;
}
VirtualList* VirtualList::Row(VirtualRowFn fn, void* user) {
    row = nullptr;
    rowWithUser = fn;
    rowUser = user;
    return this;
}

// The default row, for a list that named none: `Item N` in the theme's own
// text colour, which is the half of this that belongs up here.
struct DefaultRow {
    const float* sizes;
    float rowH;
};

static El* ThemedDefaultRow(void* user, Ctx* cx, int ix) {
    Arena* a = cx->a;
    const DefaultRow* d = (const DefaultRow*)user;
    float h = d->sizes ? d->sizes[ix] : d->rowH;
    return Div(a)->H(h)->PadX(8)->ItemsCenter()->Child(
        TextEl(a, StrDup(a, fmt("Item %d", ix)))
            ->Font(12)
            ->Fg(ThemeNow(cx->app).foreground));
}

// The caller's row builder takes no user pointer, so it is carried through
// one — `Tree::item(..)`'s closure has the same shape and the same problem.
static El* CallerRow(void* user, Ctx* cx, int ix) {
    auto fn = (El * (*)(Ctx*, int)) user;
    return fn(cx, ix);
}

El* VirtualList::IntoEl() {
    // The list is the base one. What is left up here is the theme: the row a
    // caller did not supply, and nothing else.
    VirtualListOpts o;
    o.count = count;
    o.rowH = rowH;
    o.viewH = viewH;
    o.sizes = sizes;
    o.scrollY = scrollY;
    o.scrollX = scrollX;
    o.handle = handle;
    o.scrollId = scrollId;
    o.onScroll = onScroll;
    o.axis = axis;
    o.pad = pad;
    if (rowWithUser) {
        o.row = rowWithUser;
        o.user = rowUser;
    } else if (row) {
        o.row = &CallerRow;
        o.user = (void*)row;
    } else {
        DefaultRow* d = ArenaNew<DefaultRow>(a);
        d->sizes = sizes;
        d->rowH = rowH;
        o.row = &ThemedDefaultRow;
        o.user = d;
    }
    return gpui::VirtualList::New(cx, id, o);
}

} // namespace component
} // namespace gpui
