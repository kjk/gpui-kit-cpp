#include "base/selectable_text.h"

namespace gpui {

// Bounds::from_corners, which is what Rust builds every quad with.
static Bounds FromCorners(Point a, Point b) {
    float x = a.x < b.x ? a.x : b.x;
    float y = a.y < b.y ? a.y : b.y;
    float right = a.x > b.x ? a.x : b.x;
    float bottom = a.y > b.y ? a.y : b.y;
    return {x, y, right - x, bottom - y};
}

int SelectionQuadBounds(Point start, Point end, Bounds bounds, float lineHeight,
                        Bounds* out) {
    if (!out) {
        return 0;
    }
    if (start.y == end.y) {
        out[0] = FromCorners(start, {end.x, end.y + lineHeight});
        return 1;
    }
    int n = 0;
    out[n++] = FromCorners(start, {bounds.Right(), start.y + lineHeight});
    if (end.y > start.y + lineHeight) {
        out[n++] = FromCorners({bounds.x, start.y + lineHeight},
                               {bounds.Right(), end.y});
    }
    out[n++] = FromCorners({bounds.x, end.y}, {end.x, end.y + lineHeight});
    return n;
}

SelectableText* SelectableText::New(Ctx* cx, Str id, Str text) {
    SelectableText* t = ArenaNew<SelectableText>(cx->a);
    t->a = cx->a;
    t->cx = cx;
    t->id = id;
    t->text = text;
    return t;
}

SelectableText* SelectableText::WithHandle(Ctx* cx, Str id,
                                           TextSelectionHandle handle,
                                           Str text) {
    SelectableText* t = New(cx, id, text);
    t->handle = handle;
    t->hasHandle = true;
    return t;
}

SelectableText* SelectableText::DocumentOrder(uint64_t order) {
    documentOrder = order;
    return this;
}

SelectableText* SelectableText::TextStyle(float fontSize, Rgba textColor) {
    font = fontSize;
    color = textColor;
    hasColor = true;
    return this;
}

SelectableText* SelectableText::Font(float fontSize) {
    font = fontSize;
    return this;
}

SelectableText* SelectableText::Semibold() {
    weight = 2;
    return this;
}

SelectableText* SelectableText::Highlights(const TextHighlight* runs,
                                           int count) {
    highlights = nullptr;
    highlightCount = 0;
    if (!runs || count <= 0 || count > INT32_MAX / (int)sizeof(TextHighlight)) {
        return this;
    }
    int bytes = count * (int)sizeof(TextHighlight);
    if (auto* saved = (TextHighlight*)Alloc(a, bytes)) {
        memcpy(saved, runs, (size_t)bytes);
        highlights = saved;
        highlightCount = count;
    }
    return this;
}

SelectableText* SelectableText::SelectionColor(Rgba value) {
    selectionColor = value;
    hasSelectionColor = true;
    return this;
}

El* SelectableText::IntoEl() {
    El* e = TextEl(a, text)->Wrap()->W(kFill);
    if (font > 0) {
        e->Font(font);
    }
    if (hasColor) {
        e->Fg(color);
    }
    if (weight >= 2) {
        e->Semibold();
    }
    if (highlightCount > 0) e->Highlights(highlights, highlightCount);
    if (id.s) {
        e->Click(HashClickId(id));
    }
    // The registration Rust makes in prepaint: the run joins the window's
    // selection, and — when it was given one — the document its handle owns,
    // so a drag runs from this paragraph into the next.
    e->Selectable();
    if (hasHandle) {
        e->SelectionOwner(handle.Entity());
    }
    return e;
}

} // namespace gpui
