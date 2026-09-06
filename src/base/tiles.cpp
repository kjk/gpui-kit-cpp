#include "base/tiles.h"

#include <math.h>

namespace gpui {

const Str kTileMoveDrag = StrL("tile-move");
const Str kTileResizeDrag = StrL("tile-resize");

void TilesPaintOrder(const TilesState* s, int* out) {
    for (int i = 0; i < s->items.len; i++) {
        out[i] = i;
    }
    // sorted_panels: by z-index, and by the order they were added where two
    // share one. An insertion sort keeps that tie unbroken.
    for (int i = 1; i < s->items.len; i++) {
        int v = out[i];
        int j = i - 1;
        while (j >= 0 && s->items[out[j]].zIndex > s->items[v].zIndex) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = v;
    }
}

Size TilesContentSize(const TilesState* s) {
    // Rust folds from an empty box at the origin, so the origin is never
    // positive and the size never smaller than the view's own corner.
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
    for (int i = 0; i < s->items.len; i++) {
        Bounds b = s->items[i].bounds;
        if (b.x < left) {
            left = b.x;
        }
        if (b.y < top) {
            top = b.y;
        }
        if (b.Right() > right) {
            right = b.Right();
        }
        if (b.Bottom() > bottom) {
            bottom = b.Bottom();
        }
    }
    return {right - left, bottom - top};
}

int TilesAdd(TilesState* s, int panel, Bounds bounds) {
    TileItem it;
    it.panel = panel;
    it.bounds = bounds;
    VecAppend(s->items, it);
    return s->items.len - 1;
}

void TilesRemove(TilesState* s, int ix) {
    if (ix < 0 || ix >= s->items.len) {
        return;
    }
    for (int i = ix; i + 1 < s->items.len; i++) {
        s->items[i] = s->items[i + 1];
    }
    s->items.len--;
    s->dragging = -1;
    s->resizing = -1;
}

int TilesIndexOfPanel(const TilesState* s, int panel) {
    for (int i = 0; i < s->items.len; i++) {
        if (s->items[i].panel == panel) {
            return i;
        }
    }
    return -1;
}

bool TileSnapEdge(float edge, const float* candidates, int n, float threshold,
                  float* out) {
    bool found = false;
    float best = threshold;
    for (int i = 0; i < n; i++) {
        float dist = edge - candidates[i];
        if (dist < 0) {
            dist = -dist;
        }
        if (dist < best) {
            best = dist;
            *out = candidates[i];
            found = true;
        }
    }
    return found;
}

float TileRoundToGrid(float v, float grid) {
    if (grid <= 0) {
        return v;
    }
    float t = v / grid;
    // Rust's f32::round is half away from zero.
    float r = (float)lroundf(t);
    return r * grid;
}

Bounds TileComputeResizedBounds(Bounds prev, const float* newX,
                                const float* newY, const float* newW,
                                const float* newH, const Bounds* others,
                                int nOthers, float grid) {
    // The edges of the neighbours, which is what a moving edge snaps to.
    Arena* ta = GetTempArena();
    int cap = nOthers * 2 + 1;
    float* xEdges = (float*)Alloc(ta, (int)sizeof(float) * cap);
    float* yEdges = (float*)Alloc(ta, (int)sizeof(float) * cap);
    int nx = 0;
    int ny = 0;
    for (int i = 0; i < nOthers; i++) {
        xEdges[nx++] = others[i].x;
        xEdges[nx++] = others[i].Right();
        yEdges[ny++] = others[i].y;
        yEdges[ny++] = others[i].Bottom();
    }

    float prevRight = prev.x + prev.w;
    float prevBottom = prev.y + prev.h;
    float finalX = prev.x;
    float finalW = prev.w;
    float finalY = prev.y;
    float finalH = prev.h;

    if (newX) {
        // The left edge moves and the right one is pinned; the left of the
        // area is a target too.
        float rawLeft = *newX > 0 ? *newX : 0;
        xEdges[nx] = 0;
        float snapped = 0;
        if (!TileSnapEdge(rawLeft, xEdges, nx + 1, grid, &snapped)) {
            snapped = TileRoundToGrid(rawLeft, grid);
        }
        float w = prevRight - snapped;
        finalX = snapped;
        finalW = w > kTileMinW ? w : kTileMinW;
    } else if (newW) {
        // The right edge moves and the left one is pinned.
        float rawRight = prev.x + *newW;
        float snapped = 0;
        if (!TileSnapEdge(rawRight, xEdges, nx, grid, &snapped)) {
            snapped = TileRoundToGrid(rawRight, grid);
        }
        float w = snapped - prev.x;
        finalW = w > kTileMinW ? w : kTileMinW;
    }

    if (newY) {
        float rawTop = *newY > 0 ? *newY : 0;
        yEdges[ny] = 0;
        float snapped = 0;
        if (!TileSnapEdge(rawTop, yEdges, ny + 1, grid, &snapped)) {
            snapped = TileRoundToGrid(rawTop, grid);
        }
        float h = prevBottom - snapped;
        finalY = snapped;
        finalH = h > kTileMinH ? h : kTileMinH;
    } else if (newH) {
        float rawBottom = prev.y + *newH;
        float snapped = 0;
        if (!TileSnapEdge(rawBottom, yEdges, ny, grid, &snapped)) {
            snapped = TileRoundToGrid(rawBottom, grid);
        }
        float h = snapped - prev.y;
        finalH = h > kTileMinH ? h : kTileMinH;
    }

    return {finalX, finalY, finalW, finalH};
}

void TilesMagneticSnap(const TilesState* s, Bounds dragging, int itemIx,
                       float threshold, bool* hasX, float* snapX, bool* hasY,
                       float* snapY) {
    *hasX = false;
    *hasY = false;
    // Only the neighbours within a threshold of the tile are looked at.
    Bounds search = {dragging.x - threshold, dragging.y - threshold,
                     dragging.w + threshold * 2, dragging.h + threshold * 2};
    float minX = threshold;
    float minY = threshold;

    float dragLeft = dragging.x;
    float dragRight = dragging.Right();
    float dragTop = dragging.y;
    float dragBottom = dragging.Bottom();

    // The top and left of the area come first: a tile near either snaps flush
    // to it whatever its neighbours say.
    float topDist = dragTop < 0 ? -dragTop : dragTop;
    if (topDist < threshold) {
        *hasY = true;
        *snapY = 0;
        minY = topDist;
    }
    float leftDist = dragLeft < 0 ? -dragLeft : dragLeft;
    if (leftDist < threshold) {
        *hasX = true;
        *snapX = 0;
        minX = leftDist;
    }
    if (*hasX && *hasY) {
        return;
    }

    for (int i = 0; i < s->items.len; i++) {
        if (i == itemIx) {
            continue;
        }
        Bounds o = s->items[i].bounds;
        if (o.Right() < search.x || o.x > search.Right() ||
            o.Bottom() < search.y || o.y > search.Bottom()) {
            continue;
        }
        if (!*hasX) {
            // Either edge of the tile against either edge of the neighbour.
            float dists[4] = {dragLeft - o.x, dragLeft - o.Right(),
                              dragRight - o.x, dragRight - o.Right()};
            float posns[4] = {o.x, o.Right(), o.x - dragging.w,
                              o.Right() - dragging.w};
            for (int k = 0; k < 4; k++) {
                float d = dists[k] < 0 ? -dists[k] : dists[k];
                if (d < minX) {
                    minX = d;
                    *hasX = true;
                    *snapX = posns[k];
                }
            }
        }
        if (!*hasY) {
            float dists[4] = {dragTop - o.y, dragTop - o.Bottom(),
                              dragBottom - o.y, dragBottom - o.Bottom()};
            float posns[4] = {o.y, o.Bottom(), o.y - dragging.h,
                              o.Bottom() - dragging.h};
            for (int k = 0; k < 4; k++) {
                float d = dists[k] < 0 ? -dists[k] : dists[k];
                if (d < minY) {
                    minY = d;
                    *hasY = true;
                    *snapY = posns[k];
                }
            }
        }
        if (*hasX && *hasY) {
            break;
        }
    }
}

Point TilesConstrainOrigin(const TilesState* s, Point origin) {
    if (origin.y < 0) {
        origin.y = 0;
    }
    // A tile can hang off the left, but not so far that there is nothing left
    // to grab.
    float minLeft = -s->dragInitialBounds.w + kTileKeepVisible;
    if (origin.x < minLeft) {
        origin.x = minLeft;
    }
    return origin;
}

static void PushChange(TilesState* s, const TileChange& c) {
    s->history.Push(c);
}

void TilesBeginMove(TilesState* s, int ix, float x, float y) {
    if (ix < 0 || ix >= s->items.len) {
        return;
    }
    s->dragging = ix;
    s->dragInitialMouse = {x - s->bounds.x + s->scrollX,
                           y - s->bounds.y + s->scrollY};
    s->dragInitialBounds = s->items[ix].bounds;
}

void TilesBeginResize(TilesState* s, int ix, TileSide side, float x, float y) {
    if (ix < 0 || ix >= s->items.len || side == TileSide::None) {
        return;
    }
    s->resizing = ix;
    s->side = side;
    s->resizeInitialMouse = {x - s->bounds.x + s->scrollX,
                             y - s->bounds.y + s->scrollY};
    s->resizeInitialBounds = s->items[ix].bounds;
}

void TilesUpdatePosition(TilesState* s, float x, float y) {
    int ix = s->dragging;
    if (ix < 0 || ix >= s->items.len) {
        return;
    }
    Bounds previous = s->items[ix].bounds;
    Point adjusted = {x - s->bounds.x + s->scrollX,
                      y - s->bounds.y + s->scrollY};
    Point origin = {
        s->dragInitialBounds.x + adjusted.x - s->dragInitialMouse.x,
        s->dragInitialBounds.y + adjusted.y - s->dragInitialMouse.y};

    // The snap comes before the boundary, and neither rounds to the grid —
    // the drag itself is smooth, and only the release lands on it.
    Bounds dragging = {origin.x, origin.y, s->dragInitialBounds.w,
                       s->dragInitialBounds.h};
    bool hasX = false;
    bool hasY = false;
    float snapX = 0;
    float snapY = 0;
    TilesMagneticSnap(s, dragging, ix, kTileGridSize, &hasX, &snapX, &hasY,
                      &snapY);
    if (hasX) {
        origin.x = snapX;
    }
    if (hasY) {
        origin.y = snapY;
    }
    origin = TilesConstrainOrigin(s, origin);

    if (origin.x == previous.x && origin.y == previous.y) {
        return;
    }
    s->items[ix].bounds.x = origin.x;
    s->items[ix].bounds.y = origin.y;
    TileChange c;
    c.tile = ix;
    c.hasBounds = true;
    c.oldBounds = previous;
    c.newBounds = s->items[ix].bounds;
    PushChange(s, c);
}

void TilesUpdateResize(TilesState* s, float x, float y) {
    int ix = s->resizing;
    if (ix < 0 || ix >= s->items.len) {
        return;
    }
    // The neighbours, which are what the moving edge snaps to.
    Bounds* others = (Bounds*)Alloc(GetTempArena(),
                                    (int)sizeof(Bounds) * (s->items.len + 1));
    int nOthers = 0;
    for (int i = 0; i < s->items.len; i++) {
        if (i != ix) {
            others[nOthers++] = s->items[i].bounds;
        }
    }

    Point at = {x - s->bounds.x + s->scrollX, y - s->bounds.y + s->scrollY};
    Bounds init = s->resizeInitialBounds;
    float dx = at.x - s->resizeInitialMouse.x;
    float dy = at.y - s->resizeInitialMouse.y;
    float newX = init.x + dx;
    float newY = init.y + dy;
    float newW = init.w + dx;
    float newH = init.h + dy;
    // Which of the four the side moves, which is what tells
    // compute_resized_bounds what is pinned.
    const float* px = nullptr;
    const float* py = nullptr;
    const float* pw = nullptr;
    const float* ph = nullptr;
    switch (s->side) {
        case TileSide::Left:
            px = &newX;
            break;
        case TileSide::Right:
            pw = &newW;
            break;
        case TileSide::Top:
            py = &newY;
            break;
        case TileSide::Bottom:
            ph = &newH;
            break;
        case TileSide::BottomRight:
            pw = &newW;
            ph = &newH;
            break;
        case TileSide::None:
            return;
    }

    Bounds previous = s->items[ix].bounds;
    Bounds next = TileComputeResizedBounds(previous, px, py, pw, ph, others,
                                           nOthers, kTileGridSize);
    if (next.x == previous.x && next.y == previous.y && next.w == previous.w &&
        next.h == previous.h) {
        return;
    }
    s->items[ix].bounds = next;
    TileChange c;
    c.tile = ix;
    c.hasBounds = true;
    c.oldBounds = previous;
    c.newBounds = next;
    PushChange(s, c);
}

void TilesMouseUp(TilesState* s) {
    if (s->dragging < 0 && s->resizing < 0) {
        return;
    }
    if (s->dragging >= 0 && s->dragging < s->items.len) {
        int ix = s->dragging;
        Bounds initial = s->dragInitialBounds;
        Bounds current = s->items[ix].bounds;
        // The release is what lands the tile on the grid; the drag itself is
        // free of it.
        Point aligned = {TileRoundToGrid(current.x, kTileGridSize),
                         TileRoundToGrid(current.y, kTileGridSize)};
        if (initial.x != aligned.x || initial.y != aligned.y ||
            initial.w != current.w || initial.h != current.h) {
            s->items[ix].bounds.x = aligned.x;
            s->items[ix].bounds.y = aligned.y;
            TileChange c;
            c.tile = ix;
            c.hasBounds = true;
            c.oldBounds = initial;
            c.newBounds = s->items[ix].bounds;
            PushChange(s, c);
        }
    }
    if (s->resizing >= 0 && s->resizing < s->items.len) {
        Bounds initial = s->resizeInitialBounds;
        Bounds current = s->items[s->resizing].bounds;
        if (initial.w != current.w || initial.h != current.h) {
            TileChange c;
            c.tile = s->resizing;
            c.hasBounds = true;
            c.oldBounds = initial;
            c.newBounds = current;
            PushChange(s, c);
        }
    }
    s->dragging = -1;
    s->resizing = -1;
    s->side = TileSide::None;
}

int TilesBringToFront(TilesState* s, int ix) {
    if (ix < 0 || ix >= s->items.len) {
        return -1;
    }
    TileItem item = s->items[ix];
    for (int i = ix; i + 1 < s->items.len; i++) {
        s->items[i] = s->items[i + 1];
    }
    s->items[s->items.len - 1] = item;
    int newIx = s->items.len - 1;
    TileChange c;
    c.tile = newIx;
    c.hasOrder = true;
    c.oldOrder = ix;
    c.newOrder = newIx;
    PushChange(s, c);
    return newIx;
}

bool TilesCanUndo(const TilesState* s) {
    return s->history.CanUndo();
}
bool TilesCanRedo(const TilesState* s) {
    return s->history.CanRedo();
}

// Move the tile at `from` to `to`, which is what putting an order change back
// comes down to.
static void MoveItem(TilesState* s, int from, int to) {
    if (from < 0 || from >= s->items.len || to < 0 || to >= s->items.len ||
        from == to) {
        return;
    }
    TileItem item = s->items[from];
    if (from < to) {
        for (int i = from; i < to; i++) {
            s->items[i] = s->items[i + 1];
        }
    } else {
        for (int i = from; i > to; i--) {
            s->items[i] = s->items[i - 1];
        }
    }
    s->items[to] = item;
}

void TilesUndo(TilesState* s) {
    if (!TilesCanUndo(s)) {
        return;
    }
    s->history.SetIgnoring(true);
    Vec<TileChange> changes = s->history.Undo();
    for (const TileChange& c : changes) {
        if (c.hasBounds && c.tile >= 0 && c.tile < s->items.len) {
            s->items[c.tile].bounds = c.oldBounds;
        }
        if (c.hasOrder) {
            MoveItem(s, c.newOrder, c.oldOrder);
        }
    }
    s->history.SetIgnoring(false);
}

void TilesRedo(TilesState* s) {
    if (!TilesCanRedo(s)) {
        return;
    }
    s->history.SetIgnoring(true);
    Vec<TileChange> changes = s->history.Redo();
    for (const TileChange& c : changes) {
        if (c.hasBounds && c.tile >= 0 && c.tile < s->items.len) {
            s->items[c.tile].bounds = c.newBounds;
        }
        if (c.hasOrder) {
            MoveItem(s, c.oldOrder, c.newOrder);
        }
    }
    s->history.SetIgnoring(false);
}

void TilesState::OnMoveDown(TilesState* self, Ctx* cx, const MouseDownEvent* ev,
                            intptr_t ix) {
    TilesBeginMove(self, (int)ix, ev->x, ev->y);
    Notify(cx);
}

void TilesState::OnResizeDown(TilesState* self, Ctx* cx,
                              const MouseDownEvent* ev, intptr_t packed) {
    TilesBeginResize(self, TileResizeTile((int)packed),
                     TileResizeSide((int)packed), ev->x, ev->y);
    Notify(cx);
}

void TilesState::OnMoveDrag(TilesState* self, Ctx* cx,
                            const DragMoveEvent* ev) {
    TilesUpdatePosition(self, ev->event.x, ev->event.y);
    Notify(cx);
}

void TilesState::OnResizeDrag(TilesState* self, Ctx* cx,
                              const DragMoveEvent* ev) {
    TilesUpdateResize(self, ev->event.x, ev->event.y);
    Notify(cx);
}

void TilesState::OnDragEnd(TilesState* self, Ctx* cx, const MouseUpEvent* ev) {
    (void)ev;
    // The tile that was moved comes to the front, which is what a click on a
    // window does everywhere.
    int moved = self->dragging;
    TilesMouseUp(self);
    if (moved >= 0) {
        TilesBringToFront(self, moved);
    }
    Notify(cx);
}

void TilesState::OnTileDown(TilesState* self, Ctx* cx, const MouseDownEvent*,
                            intptr_t ix) {
    (void)cx;
    self->pressed = (int)ix;
}

void TilesState::OnTileUp(TilesState* self, Ctx* cx, const MouseUpEvent*,
                          intptr_t ix) {
    if (self->pressed != (int)ix) {
        return;
    }
    self->pressed = -1;
    TilesBringToFront(self, (int)ix);
    Notify(cx);
}

void TilesState::OnScroll(TilesState* self, Ctx* cx, const ScrollEvent* ev) {
    self->scrollX = ev->offsetX;
    self->scrollY = ev->offsetY;
    Notify(cx);
}

void TileContext::BeginMove(Point pointer) const {
    if (state) {
        TilesBeginMove(state, ix, pointer.x, pointer.y);
    }
}

void TileContext::MoveTo(Point pointer) const {
    if (state && state->dragging == ix) {
        TilesUpdatePosition(state, pointer.x, pointer.y);
    }
}

void TileContext::EndMove() const {
    if (state && state->dragging == ix) {
        TilesMouseUp(state);
    }
}

void TileContext::BeginResize(ResizeSide side, Point pointer) const {
    if (state) {
        TilesBeginResize(state, ix, side, pointer.x, pointer.y);
    }
}

void TileContext::ResizeTo(Point pointer) const {
    if (state && state->resizing == ix) {
        TilesUpdateResize(state, pointer.x, pointer.y);
    }
}

void TileContext::EndResize() const {
    if (state && state->resizing == ix) {
        TilesMouseUp(state);
    }
}

void TileContext::BringToFront() const {
    if (state) {
        TilesBringToFront(state, ix);
    }
}

void TileContext::ToggleZoom() const {
    const TileItem* item = Item();
    if (state && item) {
        state->zoomedPanel =
            state->zoomedPanel == item->panel ? -1 : item->panel;
    }
}

void TileContext::Close() const {
    const TileItem* item = Item();
    if (!state || !item) {
        return;
    }
    if (state->zoomedPanel == item->panel) {
        state->zoomedPanel = -1;
    }
    TilesRemove(state, ix);
}

bool snap_edge(float edge, const float* candidates, int count, float threshold,
               float* out) {
    return TileSnapEdge(edge, candidates, count, threshold, out);
}

Bounds compute_resized_bounds(Bounds previous, const float* newX,
                              const float* newY, const float* newW,
                              const float* newH, const Bounds* others,
                              int count, float gridSize) {
    return TileComputeResizedBounds(previous, newX, newY, newW, newH, others,
                                    count, gridSize);
}

float round_to_grid(float value, float gridSize) {
    return TileRoundToGrid(value, gridSize);
}

Point magnetic_snap(Bounds moving, const Bounds* others, int count,
                    float threshold) {
    TilesState state;
    TilesAdd(&state, 0, moving);
    for (int i = 0; others && i < count; i++) {
        TilesAdd(&state, i + 1, others[i]);
    }
    bool hasX = false;
    bool hasY = false;
    float x = moving.x;
    float y = moving.y;
    TilesMagneticSnap(&state, moving, 0, threshold, &hasX, &x, &hasY, &y);
    return {hasX ? x : moving.x, hasY ? y : moving.y};
}

Point apply_boundary_constraints(Point origin, float draggingWidth) {
    if (origin.y < 0) {
        origin.y = 0;
    }
    float minLeft = -draggingWidth + kTileKeepVisible;
    if (origin.x < minLeft) {
        origin.x = minLeft;
    }
    return origin;
}

Size content_size(const Bounds* tiles, int count) {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
    for (int i = 0; tiles && i < count; i++) {
        left = std::min(left, tiles[i].x);
        top = std::min(top, tiles[i].y);
        right = std::max(right, tiles[i].Right());
        bottom = std::max(bottom, tiles[i].Bottom());
    }
    return {right - left, bottom - top};
}

} // namespace gpui
