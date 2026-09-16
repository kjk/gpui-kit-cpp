#ifndef GPUI_BASE_TOUCH_SELECTION_H_
#define GPUI_BASE_TOUCH_SELECTION_H_
/* Touch selection geometry — crates/base/src/touch_selection.rs. */

#include "gpui/gpui.h"

namespace gpui {

enum class SelectionEdge : uint8_t {
    Start,
    End,
};

inline SelectionEdge SelectionEdgeOpposite(SelectionEdge edge) {
    return edge == SelectionEdge::Start ? SelectionEdge::End
                                        : SelectionEdge::Start;
}

struct TouchSelectionSnapshot {
    Bounds start = {};
    Bounds end = {};
    SelectionEdge dragging = SelectionEdge::Start;
    bool startVisible = true;
    bool endVisible = true;
    bool menuOpen = false;
    bool hasDragging = false;

    static TouchSelectionSnapshot New(Bounds start, Bounds end);
    TouchSelectionSnapshot WithEdgeVisible(SelectionEdge edge,
                                           bool visible) const;
    TouchSelectionSnapshot WithMenuOpen(bool open) const;
    TouchSelectionSnapshot WithDragging(SelectionEdge edge) const;
    TouchSelectionSnapshot WithoutDragging() const;
    Bounds Edge(SelectionEdge edge) const;
    bool IsEdgeVisible(SelectionEdge edge) const;
    bool IsEmpty() const;
    bool BoundsOfVisible(Bounds* out) const;
};

struct TouchHandle {
    static constexpr float kHitSize = 44.f;
    static constexpr float kKnobSize = 10.f;
    static constexpr float kBarWidth = 2.f;
    static constexpr float kExtent = 12.f;

    static Bounds HitBounds(SelectionEdge edge, Bounds caret);
    static Bounds BarBounds(Bounds caret);
    static Bounds KnobBounds(SelectionEdge edge, Bounds caret);
};

Bounds TouchCaretLineBox(Point position, float lineHeight);
bool TouchCaretInView(Bounds caret, Bounds viewport);

struct TouchEdgeDrag {
    SelectionEdge edge = SelectionEdge::Start;
    Point offset = {};

    static TouchEdgeDrag Begin(SelectionEdge edge, Bounds caret, Point finger);
    TouchEdgeDrag WithEdge(SelectionEdge value) const;
    Point TextPosition(Point finger) const;
};

} // namespace gpui
#endif // GPUI_BASE_TOUCH_SELECTION_H_
