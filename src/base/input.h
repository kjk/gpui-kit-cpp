#ifndef GPUI_BASE_INPUT_H_
#define GPUI_BASE_INPUT_H_
/* Unstyled input / textarea / editor — crates/base/src/input.

   The state engine is InputState in Gpui.h; this is element.rs, the half that
   draws it. */

#include "base/input_core.h"
#include "base/input_editor.h"
#include "base/input_lsp.h"
#include "base/input_rope.h"
#include "base/input_tokens.h"
#include "base/touch_selection.h"

namespace gpui {

struct SemanticThemeTokens;

// `InputBase::new(id)`: the frame is a stateful element, so its click and
// focus ids are the fold of the name down from the root rather than a hash of
// the one name. `interactive` is Rust's `.when(!disabled, ..)` around the
// listeners: a disabled field is not a hit target and takes no focus.
struct InputBase {
    static El* New(Ctx* cx, Str id, bool interactive = false,
                   AccessibilityRole role = AccessibilityRole::TextInput);
    static El* New(Ctx* cx, Str id, const InputPresentation& presentation,
                   const InputStyles& styles = {});
};

// gpui_base::input::InputEditorStyle. The base draws the text, the selection
// and the caret; what they look like is pushed in by the themed layer above
// it, the way Rust calls state.set_editor_style(...) before rendering.
struct InputEditorStyle {
    Rgba foreground = {0, 0, 0, 0};
    Rgba mutedForeground = {0, 0, 0, 0};
    Rgba caret = {0, 0, 0, 0};
    Rgba selection = {0, 0, 0, 0};
    float fontSize = 12;
    // Editor::font_family(cx.theme().mono_font_family): a code editor draws
    // its rows, and its gutter, in the theme's monospace family.
    bool mono = false;
    // The family itself, as FontFamilyIntern's id: 0 leaves the platform's
    // default face for `mono`. Rust's Editor names theme.mono_font_family
    // unless a caller's `.font_family(..)` refines over it.
    uint8_t fontFamily = 0;
    // A masked field draws one bullet per character, and text_center /
    // text_right move the run inside the field. Both also live on the state;
    // either one turning it on is enough.
    bool mask = false;
    int align = 0;
    // highlight_styles: how the installed InputHighlighter's capture names
    // resolve to colours. The themed layer projects it, the element passes
    // it into styles(), and base itself interprets nothing — the shape
    // highlighting.rs gives the resolver.
    HighlightStyleResolver highlightStyles = {};
    // The decoration runs over the whole document, in order, as UTF-8
    // offsets into it: semantic tokens and an editor's TextDecorations both
    // arrive this way, and the element lays them over what the highlighter
    // answers for the visible range. The rows slice what falls inside them
    // out of the composed list, so a run may span more than one.
    const TextSpan* spans = nullptr;
    int nSpans = 0;
    // The search matches over the whole document, in order, as UTF-8 offsets
    // — `search_session.matcher.matched_ranges()`. The rows slice what falls
    // inside them out of it, the way they do the highlighted runs. Empty
    // while the find bar is closed, which is when Rust builds no paths.
    const Selection* matches = nullptr;
    int nMatches = 0;
    // Which of them is the one the panel is on, painted in its own colour so
    // it stands out from the rest. -1 for none.
    int currentMatch = -1;
    // Spelled out rather than left at `{}`: an Rgba defaults to opaque, so a
    // style that names none of these would paint an opaque black wash over
    // the caret's row -- which is what an editor built without `.active_line`
    // did.
    Rgba matchBg = {0, 0, 0, 0};
    Rgba currentMatchBg = {0, 0, 0, 0};
    // The editor's own surface, which an inline suggestion's second line and
    // beyond is drawn on: it covers the row under it rather than pushing it
    // down. Transparent means the rows show through, which is what a field
    // that never suggests anything gets.
    Rgba background = {0, 0, 0, 0};
    Rgba border = {0, 0, 0, 0};
    // The colour a symbol takes while the shortcut modifier is held over it
    // and the definition provider has somewhere to go. Rust reads `link_text`
    // out of the *highlight* theme; this tree's scanner palette has no such
    // entry, so the themed layer passes the UI theme's link colour.
    Rgba linkText = {0, 0, 0, 0};
    // EditorStyle::diagnostics: what each severity underlines in. All four
    // transparent is an editor that draws none, which is every field that is
    // not a code editor.
    DiagnosticColors diagnostics = {};
    // Editor's active-line wash and its indent guides. Alpha 0 and 0 are off,
    // which is what a plain textarea wants.
    Rgba activeLine = {0, 0, 0, 0};
    // How far left of the rows the active-line wash reaches: the editor's
    // left padding, which upstream's wash covers as well as the gutter.
    float activeLineBleedL = 0;
    Rgba indentGuide = {0, 0, 0, 0};
    // How many columns an indent guide stands every, which is the language's
    // tab size.
    int indentWidth = 4;
};

// Fill only colours the caller left unset. Keeping the projected style as
// the input to every call lets a palette change take effect immediately.
InputEditorStyle InputEditorStyleResolve(const InputEditorStyle& projected,
                                         const SemanticThemeTokens& tokens);

struct Input {
    static El* New(Ctx* cx, InputState* state);
    static El* New(Ctx* cx, InputState* state, const InputEditorStyle& style);
};

struct Textarea {
    static El* New(Ctx* cx, InputState* state);
    static El* New(Ctx* cx, InputState* state, const InputEditorStyle& style,
                   bool lineNumbers = false);
};

struct Editor {
    static El* New(Ctx* cx, InputState* state);
    static El* New(Ctx* cx, InputState* state, const InputEditorStyle& style);
};

// element.rs's prepaint geometry, read back after the frame that laid it out
// — what Rust's tests ask of `last_layout`. Each answers from the rows the
// multi-line element painted in `win`'s last finished frame and answers
// nothing (0, false) when it painted none there.
//
// The visible, unfolded rows in order, as their buffer ranges without the
// newline: layout.visible_line_byte_offsets with each line's len().
int InputLastPaintedRows(const InputState* s, const Window* win, Selection* out,
                         int cap);
// layout_range_corners: the corners `range` takes through those rows, one
// per visual row it touches, a line's newline as a space-wide cell. 0 when
// none of it is visible — Rust's None.
int InputLastRangeCorners(const InputState* s, Window* win, Selection range,
                          Vec<RangeCorners>* out);
// layout_range_decorations: how many fill and frame paths the last frame
// built for the visible decorations.
bool InputLastRangeDecorationPaths(const InputState* s, const Window* win,
                                   int* fills, int* frames);
// line_and_position_for_offset, in window coordinates: the top-left of the
// caret before `offset` as the last finished frame laid it out. False when
// the offset's row was not laid out (scrolled out of the built band) or
// nothing has been painted yet.
bool InputLastCaretPoint(const InputState* s, Window* win, int offset,
                         Point* out);
// range_to_bounds: the box `range` takes from the caret before its start to
// the caret before its end, one line tall, as the last finished frame laid
// it out. An inline token's range is its chip. False when either end was not
// laid out.
bool InputRangeToBounds(const InputState* s, Window* win, Selection range,
                        Bounds* out);
// last_layout.lines[..].wrapped_lines: every visual row the last frame
// painted, in order. `text` is where the row's run landed and `width` its
// shaped width; a continuation row's run starts its line's wrap_indent to
// the right of the first row's.
struct InputPaintedVisualRow {
    int start = 0; // buffer offset of the row's first byte
    int len = 0;
    int lineStart = 0; // the logical line's
    int lineLen = 0;
    bool lastOfLine = false;
    Bounds text = {};
    float width = 0;
};
int InputLastVisualRows(const InputState* s, Window* win,
                        InputPaintedVisualRow* out, int cap);
// The active-line quad the last frame painted under the caret's line, gutter
// and the editor's left padding included. False when it painted none.
bool InputLastActiveLine(const InputState* s, const Window* win, Bounds* out);

// ─── soft wrap (display_map/text_wrapper.rs) ─────────────────────────────

// TextWrapper::on_layout_changed and set_font: the rows every line wraps to
// at `width` (0 wraps nothing) in `fontSize` and `fontWord`, rebuilt only
// when one of those, the document or the wrapping indent moved. `ctx`
// measures the characters; without one a character not measured before is
// estimated. The editor element calls it every frame with the text column it
// last laid out; a test calls it to wrap without drawing.
void InputUpdateWrapMap(InputState* s, PaintCtx* ctx, float width,
                        float fontSize, uint16_t fontWord);
// The rows of logical line `line`, as byte offsets into it (row 0's is 0),
// and the continuation rows' indent. One row when nothing wraps.
int InputWrapRows(const InputState* s, int line, const int** starts,
                  float* indent);
// InputState::set_wrapping_indent.
void InputSetWrappingIndent(InputState* s, App* app, Window* win,
                            WrappingIndent indent);

// ─── touch selection (input/base/touch.rs) ───────────────────────────────
//
// A long press selects the word under the finger and keeps following the
// finger while it stays down, like the double-click-and-drag it stands in
// for. Releasing opens the edit menu; from then on the selection carries a
// handle at each end, which drags through InputBeginEdgeDrag. The touch
// selection remembers the range it made: as soon as the selection is
// something else — the caret moved, text was typed, a cursor was added — the
// handles and the menu are gone without anyone hiding them.

// touch_selection(): the live touch selection, laid out for the handles and
// the edit menu from what `win` last painted. False when it has since become
// something else or nothing is laid out.
bool InputTouchSelection(const InputState* s, Window* win,
                         TouchSelectionSnapshot* out);
// keep_touch_selection: the current selection becomes a touch selection with
// its menu open — a double tap's word.
void InputKeepTouchSelection(InputState* s, App* app, Window* win);
// dismiss_touch_selection: the handles and the menu go.
void InputDismissTouchSelection(InputState* s, App* app, Window* win);
// close_edit_menu: the menu closes; the selection keeps its handles.
void InputCloseEditMenu(InputState* s, App* app, Window* win);
// reopen_edit_menu_at: a tap inside the touch selection brings its menu
// back. True when it did.
bool InputReopenEditMenuAt(InputState* s, App* app, Window* win, Point at);
// edit_menu_on_scroll: a scrolling finger steps the menu aside, and lifting
// it brings the menu back over the handles.
void InputEditMenuOnScroll(InputState* s, App* app, Window* win,
                           TouchPhase phase);
// select_all_from_edit_menu: Select All, keeping a live touch selection's
// handles and menu over the new range.
void InputSelectAllFromEditMenu(InputState* s, App* app, Window* win);
// on_long_press: one phase of a long press inside the field. True when the
// field claimed it; the window then routes the gesture's moves and release
// here even after the finger leaves the field.
bool InputOnLongPress(InputState* s, App* app, Window* win,
                      const LongPressEvent& event);
// begin/update/end_edge_drag: one end of the touch selection follows the
// finger while the other stays put. Dragging an end past the other swaps
// them, and the finger keeps the handle that became the other one.
void InputBeginEdgeDrag(InputState* s, App* app, Window* win,
                        SelectionEdge edge, Point finger);
void InputUpdateEdgeDrag(InputState* s, App* app, Window* win, Point finger);
// extend_edge_drag_to: the dragged end to the text at `position` (already
// moved off the finger by the drag's offset). What the frame's auto-scroll
// tick re-runs while the content scrolls under a held handle.
void InputExtendEdgeDragTo(InputState* s, App* app, Window* win,
                           Point position);
void InputEndEdgeDrag(InputState* s, App* app, Window* win);
// Rust's #[cfg(test)] is_edit_menu_open / touch_selection_range.
bool InputIsEditMenuOpen(const InputState* s);
bool InputTouchSelectionRange(const InputState* s, Selection* out);
} // namespace gpui
#endif // GPUI_BASE_INPUT_H_
