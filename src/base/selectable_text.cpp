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

SelectableText* SelectableText::RevealRange(Selection range) {
    revealRange = range;
    return this;
}

SelectableText* SelectableText::OnReveal(Listener listener) {
    onReveal = listener;
    return this;
}

struct SelectableRevealState {
    Selection range = {-1, -1};
};

struct SelectableRevealRequest {
    SelectableRevealState* state = nullptr;
    Selection range = {};
    Listener listener = {};
};

static bool SelectableUtf8Boundary(Str text, int at) {
    return at >= 0 && at <= len(text) &&
           (at == len(text) || ((uint8_t)text.s[at] & 0xc0) != 0x80);
}

static void SelectableRevealLine(PaintCtx* ctx, El* el, void* user) {
    auto* request = (SelectableRevealRequest*)user;
    const Selection& range = request->range;
    auto& last = request->state->range;
    if (!ctx->window || (range.start == last.start && range.end == last.end)) {
        return;
    }
    float x = 0, y = 0, h = 0;
    float maxW = el->laidMaxW > 0 ? el->laidMaxW : el->w;
    if (!TextPointAt(ctx, el->text, el->laidFont, maxW, el->style.wrap,
                     range.start, &x, &y, &h, ElTextWeight(el),
                     el->style.lineHeight, false)) {
        return;
    }
    last = range;
    Bounds line = {el->x, el->y + y, el->w, h};
    if (request->listener.IsValid()) {
        ListenerCall(ctx->app, ctx->window, request->listener, &line);
    } else {
        WindowRequestAutoscroll(ctx->window, line);
    }
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
    if (cx->win && id.s && revealRange.end >= revealRange.start &&
        SelectableUtf8Boundary(text, revealRange.start) &&
        SelectableUtf8Boundary(text, revealRange.end)) {
        uint32_t key = KeyedKey(KeyedName(cx, id),
                                HashClickId(StrL("SelectableRevealState")));
        auto* request = ArenaNew<SelectableRevealRequest>(a);
        request->state = FrameKeyedState<SelectableRevealState>(cx, key);
        request->range = revealRange;
        request->listener = onReveal;
        e->customUser = request;
        e->prePaint = &SelectableRevealLine;
    }
    return e;
}

} // namespace gpui
