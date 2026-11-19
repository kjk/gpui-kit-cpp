#ifndef GPUI_BASE_SELECTABLE_TEXT_H_
#define GPUI_BASE_SELECTABLE_TEXT_H_
/* Plain text that joins the window's selection —
   crates/base/src/selectable_text.rs

   For an application that wants copyable text without a rich-text document.
   `New` gives a run its own selection; `WithHandle` joins the document a
   TextSelectionHandle belongs to, so several runs read as one — which is what
   the showcase's text-selection page does with its four paragraphs.

   Rust implements Element by hand: it lays a StyledText out, inserts a
   hitbox, registers it with the handle and paints the projected ranges under
   the glyphs itself. The runtime here already does all of that for any
   element that says `Selectable()` — the frame collects its text runs, the
   window's gesture projects onto them in document order and the paint pass
   draws the wash — so this is the same contract expressed as a builder over
   one text element. `SelectionQuadBounds` is the one piece of geometry Rust
   writes out, kept because it is the part with a rule in it. */

#include "base/text_selection.h"

namespace gpui {

struct SelectableText {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str text = {};
    TextSelectionHandle handle = {};
    bool hasHandle = false;
    uint64_t documentOrder = 0;
    // TextStyleRefinement: what the run is laid out and painted with. Unset
    // is the size and colour the container above it pushed.
    float font = 0;
    Rgba color = {};
    bool hasColor = false;
    int weight = 0;
    // `selection_color`, which defaults to the theme's `colors.selection`.
    // The runtime paints the window's selection in one pass, from the theme,
    // so a per-run override has no seam to go through: this is carried and
    // reported, not painted differently. Said again in the log.
    Rgba selectionColor = {};
    bool hasSelectionColor = false;
    const TextHighlight* highlights = nullptr;
    int highlightCount = 0;
    Selection revealRange = {-1, -1};
    Listener onReveal = {};

    static SelectableText* New(Ctx* cx, Str id, Str text);
    static SelectableText* WithHandle(Ctx* cx, Str id,
                                      TextSelectionHandle handle, Str text);
    // Where this run sits in reading order among the others sharing its
    // handle.
    SelectableText* DocumentOrder(uint64_t order);
    SelectableText* TextStyle(float fontSize, Rgba textColor);
    SelectableText* Font(float fontSize);
    SelectableText* Semibold();
    SelectableText* Highlights(const TextHighlight* runs, int count);
    SelectableText* SelectionColor(Rgba value);
    // Reveal the visual line containing the range's starting UTF-8 byte.
    // An unchanged request runs once per element lifetime.
    SelectableText* RevealRange(Selection range);
    // Receives the line bounds in window coordinates during prepaint.
    // Without a listener the element asks the enclosing scroller to reveal it.
    SelectableText* OnReveal(Listener listener);
    El* IntoEl();
};

// selection_quad_bounds: the one, two or three rectangles a selection from
// `start` to `end` covers inside `bounds`. A selection on one line is a
// single quad; one that spans lines is the tail of the first line, the whole
// of the lines between, and the head of the last. Writes at most three into
// `out` and answers how many.
int SelectionQuadBounds(Point start, Point end, Bounds bounds, float lineHeight,
                        Bounds* out);

} // namespace gpui
#endif // GPUI_BASE_SELECTABLE_TEXT_H_
