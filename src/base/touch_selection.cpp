#include "base/touch_selection.h"

namespace gpui {

TouchSelectionSnapshot TouchSelectionSnapshot::New(Bounds startValue,
                                                   Bounds endValue) {
    TouchSelectionSnapshot out;
    out.start = startValue;
    out.end = endValue;
    return out;
}

TouchSelectionSnapshot TouchSelectionSnapshot::WithEdgeVisible(
    SelectionEdge edge, bool visible) const {
    TouchSelectionSnapshot out = *this;
    (edge == SelectionEdge::Start ? out.startVisible : out.endVisible) =
        visible;
    return out;
}

TouchSelectionSnapshot TouchSelectionSnapshot::WithMenuOpen(bool open) const {
    TouchSelectionSnapshot out = *this;
    out.menuOpen = open;
    return out;
}

TouchSelectionSnapshot TouchSelectionSnapshot::WithDragging(
    SelectionEdge edge) const {
    TouchSelectionSnapshot out = *this;
    out.dragging = edge;
    out.hasDragging = true;
    return out;
}

TouchSelectionSnapshot TouchSelectionSnapshot::WithoutDragging() const {
    TouchSelectionSnapshot out = *this;
    out.hasDragging = false;
    return out;
}

Bounds TouchSelectionSnapshot::Edge(SelectionEdge edge) const {
    return edge == SelectionEdge::Start ? start : end;
}

bool TouchSelectionSnapshot::IsEdgeVisible(SelectionEdge edge) const {
    return edge == SelectionEdge::Start ? startVisible : endVisible;
}

bool TouchSelectionSnapshot::IsEmpty() const {
    return start.x == end.x && start.y == end.y && start.w == end.w &&
           start.h == end.h;
}

bool TouchSelectionSnapshot::BoundsOfVisible(Bounds* out) const {
    if (!startVisible && !endVisible) {
        return false;
    }
    Bounds b = startVisible ? start : end;
    if (startVisible && endVisible) {
        float left = std::min(start.x, end.x);
        float top = std::min(start.y, end.y);
        float right = std::max(start.x + start.w, end.x + end.w);
        float bottom = std::max(start.y + start.h, end.y + end.h);
        b = {left, top, right - left, bottom - top};
    }
    if (out) {
        *out = b;
    }
    return true;
}

Bounds TouchHandle::HitBounds(SelectionEdge edge, Bounds caret) {
    float top = edge == SelectionEdge::Start ? caret.y - kExtent : caret.y;
    return {caret.x - kHitSize * 0.5f, top, kHitSize, caret.h + kExtent};
}

Bounds TouchHandle::BarBounds(Bounds caret) {
    return {caret.x - kBarWidth * 0.5f, caret.y, kBarWidth, caret.h};
}

Bounds TouchHandle::KnobBounds(SelectionEdge edge, Bounds caret) {
    float top =
        edge == SelectionEdge::Start ? caret.y - kKnobSize : caret.y + caret.h;
    return {caret.x - kKnobSize * 0.5f, top, kKnobSize, kKnobSize};
}

Bounds TouchCaretLineBox(Point position, float lineHeight) {
    return {position.x, position.y, 0, lineHeight};
}

bool TouchCaretInView(Bounds caret, Bounds viewport) {
    return caret.Bottom() > viewport.y && caret.y < viewport.Bottom() &&
           caret.x >= viewport.x && caret.x <= viewport.Right();
}

TouchEdgeDrag TouchEdgeDrag::Begin(SelectionEdge edgeValue, Bounds caret,
                                   Point finger) {
    TouchEdgeDrag out;
    out.edge = edgeValue;
    out.offset = {caret.CenterX() - finger.x, caret.CenterY() - finger.y};
    return out;
}

TouchEdgeDrag TouchEdgeDrag::WithEdge(SelectionEdge value) const {
    TouchEdgeDrag out = *this;
    out.edge = value;
    return out;
}

Point TouchEdgeDrag::TextPosition(Point finger) const {
    return {finger.x + offset.x, finger.y + offset.y};
}

} // namespace gpui
