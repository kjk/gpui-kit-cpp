#ifndef GPUI_BASE_TEXT_SELECTION_H_
#define GPUI_BASE_TEXT_SELECTION_H_
/* Unstyled selectable text host — crates/base/src/text_selection.rs */

#include "gpui/gpui.h"
#include "base/touch_selection.h"

namespace gpui {

struct TouchHandleHitbox {
    SelectionEdge edge = SelectionEdge::Start;
    Bounds bounds = {};
};

struct TouchHandleLayout {
    Vec<TouchHandleHitbox> hitboxes;
    ~TouchHandleLayout() { VecReset(hitboxes); }
};

struct TextSelectionScopeId {
    uint64_t raw = 0;

    static TextSelectionScopeId New();
    static TextSelectionScopeId FromRaw(uint64_t value) { return {value}; }
    uint64_t Value() const { return raw; }
    int RuntimeScope() const { return (int)(raw & 0x7fffffffU); }
};

inline bool operator==(TextSelectionScopeId a, TextSelectionScopeId b) {
    return a.raw == b.raw;
}
inline bool operator!=(TextSelectionScopeId a, TextSelectionScopeId b) {
    return !(a == b);
}

struct TextSelectionContentKey {
    uint64_t raw = 0;

    static TextSelectionContentKey New(uint64_t value) { return {value}; }
    uint64_t Value() const { return raw; }
};

inline bool operator==(TextSelectionContentKey a, TextSelectionContentKey b) {
    return a.raw == b.raw;
}

enum class TextSelectionCoverage : uint8_t {
    Bounded,
    FromStart,
    ToEnd,
    Full
};

struct TextSelectionEndpoint {
    EntityId entity = {};
    Point point = {};
    TextSelectionContentKey contentKey = {};
    bool hasEntity = false;
    bool hasContentKey = false;

    static TextSelectionEndpoint New(EntityId entity, Point point);
    static TextSelectionEndpoint At(Point point);
    TextSelectionEndpoint WithContentKey(TextSelectionContentKey value) const;
    EntityId Entity() const { return entity; }
    Point ContentPoint() const { return point; }
};

struct TextSelectionWindowPoints {
    Point anchor = {};
    Point cursor = {};

    static TextSelectionWindowPoints New(Point anchor, Point cursor) {
        return {anchor, cursor};
    }
    Point Anchor() const { return anchor; }
    Point Cursor() const { return cursor; }
};

struct TextSelectionSnapshot {
    TextSelectionEndpoint anchor = {};
    TextSelectionEndpoint cursor = {};
    TextSelectionWindowPoints windowPoints = {};
    TextSelectionCoverage coverage = TextSelectionCoverage::Bounded;
    bool selecting = false;
    bool hasWindowPoints = false;

    static TextSelectionSnapshot New(TextSelectionEndpoint anchor,
                                     TextSelectionEndpoint cursor);
    TextSelectionSnapshot WithSelecting(bool value) const;
    TextSelectionSnapshot WithWindowPoints(
        TextSelectionWindowPoints value) const;
    TextSelectionSnapshot WithCoverage(TextSelectionCoverage value) const;
    TextSelectionEndpoint Anchor() const { return anchor; }
    TextSelectionEndpoint Cursor() const { return cursor; }
    bool IsSelecting() const { return selecting; }
    TextSelectionCoverage Coverage() const { return coverage; }
};

// GPUI's Hitbox is retained by the source registration. This runtime rebuilds
// hit testing each frame, so its bounds are the durable part of that value.
struct TextSelectionRegistration {
    Bounds hitbox = {};
    Bounds bounds = {};
    Point scrollOffset = {};
    TextSelectionScopeId scope = {};
    uint64_t documentOrder = 0;
    const Bounds* textBounds = nullptr;
    int textBoundsCount = 0;

    static TextSelectionRegistration New(Bounds hitbox, Bounds bounds);
    TextSelectionRegistration WithScrollOffset(Point value) const;
    TextSelectionRegistration WithScope(TextSelectionScopeId value) const;
    TextSelectionRegistration WithDocumentOrder(uint64_t value) const;
    TextSelectionRegistration WithTextBounds(const Bounds* values,
                                             int count) const;
};

// TextLayout is reference-counted by the paint backend. A run borrows it for
// the call to UpdateRuns; the handle takes its own reference when retaining
// the source run list.
struct TextSelectionRun {
    uint64_t documentOrder = 0;
    Str text = {};
    TextLayout* layout = nullptr;
    Bounds bounds = {};

    static TextSelectionRun New(Str text, TextLayout* layout, Bounds bounds);
    TextSelectionRun WithDocumentOrder(uint64_t value) const;
};

struct TextSelectionRange {
    int start = 0;
    int end = 0;
    bool selected = false;
};

struct TextSelectionProjection {
    Vec<TextSelectionRange> ranges;
    bool active = false;

    int Len() const { return ranges.len; }
    const TextSelectionRange* Ranges() const { return ranges.els; }
    bool IsActive() const { return active; }
    void Reset() { VecReset(ranges); }
};

enum class TextSelectionEventKind : uint8_t {
    SelectionChanged,
    AutoScroll,
    Cleared
};

// Rust's TextSelectionEvent is a payload enum. The POD projection keeps the
// discriminator and both optional payloads adjacent.
struct TextSelectionEvent {
    TextSelectionEventKind kind = TextSelectionEventKind::SelectionChanged;
    TextSelectionSnapshot snapshot = {};
    float autoScroll = 0;
    bool hasSnapshot = false;
    bool hasAutoScroll = false;
};

using TextSelectionFocusFn = void (*)(void* user, Window* window, App* app);
using TextSelectionClearFn = void (*)(void* user, App* app);
using TextSelectionCopyFn = int (*)(void* user, App* app, char* out, int cap);
using TextSelectionContentKeyFn = bool (*)(void* user, Point point,
                                           const App* app,
                                           TextSelectionContentKey* out);

struct TextSelectionParticipantState;

template <>
struct EventEmitter<TextSelectionParticipantState, TextSelectionEvent> {};

struct TextSelectionHandle {
    gpui::Entity<TextSelectionParticipantState> state = {};

    static TextSelectionHandle New(Str fallbackCopyText, App* app);
    EntityId Entity() const { return state.id; }
    bool Snapshot(const App* app, TextSelectionSnapshot* out) const;
    void SetFallbackCopyText(Str text, App* app) const;
    void SetLocalSelection(bool active, App* app) const;
    bool HasLocalSelection(const App* app) const;
    void Register(TextSelectionRegistration registration, Window* window,
                  App* app) const;
    TextSelectionProjection UpdateRuns(const TextSelectionRun* runs, int count,
                                       App* app) const;
    Subscription RefreshWindowOnChange(App* app) const;
    void FocusWith(TextSelectionFocusFn fn, void* user, App* app) const;
    void ClearWith(TextSelectionClearFn fn, void* user, App* app) const;
    void CopyWith(TextSelectionCopyFn fn, void* user, App* app) const;
    void ResolveContentKeyWith(TextSelectionContentKeyFn fn, void* user,
                               App* app) const;

    template <typename S>
    Subscription Subscribe(Ctx* cx,
                           void (*fn)(S*, Ctx*,
                                      const TextSelectionEvent*)) const {
        return gpui::Subscribe(cx, state, fn);
    }
};

// Rust's did_hit_text, which is the rule the whole module's mouse handling
// turns on: a gesture that never touched a glyph publishes nothing and copies
// nothing, however far it dragged. `blank_only_drag_never_publishes_or_copies_
// selection` is the case that pins it.
//
// It is sticky in both directions, and that is the point. A press in the
// margin still begins a gesture, so dragging from beside a paragraph into it
// selects — Rust sets the flag from `anchor.inside_text || endpoint.inside_
// text` and then ORs every move into it. And once any point has landed on
// text the selection stands even as the pointer wanders back off, because the
// flag is never cleared mid-gesture.
struct TextSelectionGesture {
    bool selecting = false;
    bool didHitText = false;
};

// The press. `insideText` is whether it landed on a glyph rather than in the
// space around one.
void TextSelectionBegin(TextSelectionGesture* g, bool insideText);
// A move during the drag.
void TextSelectionExtend(TextSelectionGesture* g, bool insideText);
// The release. The flag outlives the gesture, so what was selected can still
// be copied afterwards.
void TextSelectionEnd(TextSelectionGesture* g);
// Whether there is a selection to show or copy at all.
bool TextSelectionPublishes(const TextSelectionGesture* g);
// A press that starts something else — a click on a control, or one that
// dismissed an overlay — drops the gesture and what it had.
void TextSelectionClear(TextSelectionGesture* g);

struct TextSelection {
    // Compatibility constructor retained for the older root code.
    static El* New(Ctx* cx, Str id, int clickId = 0);
    static int SelectedText(Window* window, App* app, char* out, int cap);
    static bool HasSelection(Window* window, const App* app);
    static void Clear(Window* window, App* app);
    static void ClearForWindow(Window* window, App* app);
    static void End(Window* window, App* app);
    static void ActivateScope(TextSelectionScopeId scope, Window* window,
                              App* app);
};

struct TextSelectionLayerPrepaintState {
    WindowSelection* selection = nullptr;
};

struct TextSelectionLayer {
    static El* New(Ctx* cx);
};

// pub(crate) text_selection_scope. Rust carries the scope through every
// element phase; the flat runtime carries the same value in the inherited
// trap id used when selectable TextHits are collected.
El* TextSelectionScope(El* element, TextSelectionScopeId scope);

// ─── the window's selection ───────────────────────────────────────────────
//
// WindowSelectionState. Rust keeps one per window in a global keyed by
// WindowId: every selectable run registers with it as it paints, and the
// window's own mouse handlers — not the application's — drive the gesture,
// so a drag runs from a paragraph into the one below it without either of
// them knowing about the other. Here the registrations are the `TextHit`s
// the frame already collects, and the endpoints are offsets into that same
// document order, so this is the state and the handlers around it.
//
// The runtime calls the three gestures and TextSelectionApply; an
// application only has to say `Selectable()` on the text.
struct WindowSelection {
    TextSelectionGesture gesture;
    // SelectionEndpoint::anchor / cursor, as document offsets. -1 is none.
    int anchor = -1;
    int cursor = -1;
    // TextSelectionScopeId: the trap the gesture began in. Extending stays
    // inside it, and so does what gets painted and copied.
    int scope = 0;
    TextSelectionScopeId activeScope = {};
    Point anchorPoint = {};
    Point cursorPoint = {};
    bool hasWindowPoints = false;
    bool publishing = false;
    bool clearing = false;
    uint64_t frameGeneration = 0;
    // Stable handles registered in this window. The participant state owns
    // its frame geometry; this list only lets window gestures publish events.
    Vec<EntityId> participants;
    // What a copy says. Rust hangs `selection_format` off the TextView's own
    // state and the document does the copying; the copy here is the window's,
    // so the format is too, and `TextView::SelFormat` sets it as it renders.
    SelectionFormat format = SelectionFormat::Plain;
};

// The window's selection, made on first use.
WindowSelection* WindowSelectionOf(Window* win);
void WindowSelectionFree(Window* win);

// The press. `clickCount` is GPUI's — 2 takes the word, 3 the line — and
// `extend` is a shift-click, which moves the cursor and keeps the anchor.
void WindowSelectionPress(Window* win, float x, float y, int clickCount,
                          bool extend);
// A long press takes the word like a double click but keeps the gesture live
// so subsequent touch movement can extend it.
bool WindowSelectionLongPressStart(Window* win, float x, float y);
// A move with the button down.
void WindowSelectionDrag(Window* win, float x, float y);
// The release. What was selected stands until the next press.
void WindowSelectionRelease(Window* win);

// TextSelection::has_selection.
bool WindowSelectionHas(const Window* win);
// TextSelection::clear.
void WindowSelectionClear(Window* win);
// TextSelection::selected_text, written into `out`. Answers its length. The
// format is the window's unless one is named, which is what the tests do.
int WindowSelectionText(Window* win, char* out, int cap);
int WindowSelectionTextAs(Window* win, char* out, int cap, SelectionFormat fmt);
// TextViewState's view-local projection of the window selection.
int WindowSelectionTextForEntity(Window* win, EntityId owner, char* out,
                                 int cap, SelectionFormat fmt);
bool WindowSelectionHasEntity(const Window* win, EntityId owner);
void WindowSelectionSelectAll(Window* win, EntityId owner);
// TextView::selection_format, on the window that does the copying.
void WindowSelectionSetFormat(Window* win, SelectionFormat fmt);
SelectionFormat WindowSelectionFormat(Window* win);
// The window's copy: the selection to the clipboard. False when there is
// nothing selected, which is what lets a Ctrl+C fall through to whatever
// else wants it.
bool WindowSelectionCopy(Window* win);

// Hand the range to the frame being built, which is what makes it paint.
// The runtime calls this before the view renders.
void WindowSelectionApply(Window* win);
// Sweeps participant registrations not renewed while this frame rendered.
void WindowSelectionFinishFrame(Window* win);
} // namespace gpui
#endif // GPUI_BASE_TEXT_SELECTION_H_
