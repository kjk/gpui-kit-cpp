#ifndef GPUI_UI_LABEL_H_
#define GPUI_UI_LABEL_H_
/* Themed label — crates/ui/src/label.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

enum class HighlightsMatchKind : uint8_t {
    Prefix,
    Full
};

// Rust represents this as a payload enum. The POD C++ projection keeps the
// same tag and string together and provides the two variant constructors.
struct HighlightsMatch {
    HighlightsMatchKind kind = HighlightsMatchKind::Full;
    Str text = {};

    static HighlightsMatch Prefix(Str text);
    static HighlightsMatch Full(Str text);
    static HighlightsMatch From(Str text);
    Str AsStr() const;
    bool IsPrefix() const;
};

struct Label {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str text = {};
    Str secondary = {};
    bool hasSecondary = false;
    bool masked = false;
    bool semibold = false;
    float font = 0; // inherited, as Label's StyleRefinement is by default
    HighlightsMatch highlight = {};
    bool hasHighlight = false;
    // text_center / text_right, and line_height(rems(..)).
    int align = 0; // 0 start, 1 center, 2 end
    float lineHeight = 1.25f;

    static Label* New(Ctx* cx, Str text, const char* file = __builtin_FILE(),
                      int line = __builtin_LINE(),
                      int column = GPUI_CALLER_COLUMN());
    // Labels built at one call site need distinct ids when they are siblings.
    Label* Id(Str value);
    Label* Secondary(Str s);
    Label* Masked(bool v);
    Label* Semibold();
    Label* Font(float px);
    Label* Highlights(HighlightsMatch matched);
    // Compatibility convenience over HighlightsMatch::Full/Prefix.
    Label* Highlights(Str matched, bool prefix = false);
    Label* TextCenter();
    Label* TextRight();
    Label* LineHeight(float mult);
    Str FullText() const;
    int HighlightRanges(int totalLength, Selection* out, int capacity) const;
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_LABEL_H_
