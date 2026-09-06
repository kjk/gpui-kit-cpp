#ifndef GPUI_GPUI_GPUI_H_
#define GPUI_GPUI_GPUI_H_
/* C++ GPUI subset used by system_monitor. Frame-rebuilt element tree. */

#include "base.h"
#include "taffy/taffy_tree.h"

// The base lives in `namespace base` so that `src/taffy` and `src/markdown` —
// ports of crates that have never heard of gpui — can be written against it
// and nothing else. gpui is the one module that treats it as its own
// vocabulary, so it takes the whole namespace in: `Str`, `Vec`, `Arena`,
// `fmt` and the rest are spelled unqualified below, and qualified lookup
// still finds them, so `gpui::Str` outside names what it always did.
namespace gpui {
using namespace base;
}

namespace base {
int StrToIntUnchecked(Str s);
}

// ─── color ────────────────────────────────────────────────────────────────

namespace gpui {

struct App;

struct Rgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

inline Rgba Rgb(uint8_t r, uint8_t g, uint8_t b) {
    return Rgba{r, g, b, 255};
}
inline Rgba Rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return Rgba{r, g, b, a};
}
inline bool RgbaEq(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
inline Rgba RgbaHex(uint32_t hex) {
    // 0xRRGGBB or 0xAARRGGBB if top byte set
    if (hex > 0xFFFFFFu) {
        return Rgba{(uint8_t)((hex >> 16) & 0xff), (uint8_t)((hex >> 8) & 0xff),
                    (uint8_t)(hex & 0xff), (uint8_t)((hex >> 24) & 0xff)};
    }
    return Rgba{(uint8_t)((hex >> 16) & 0xff), (uint8_t)((hex >> 8) & 0xff),
                (uint8_t)(hex & 0xff), 255};
}
Rgba RgbaOpacity(Rgba c, float a01);
// A plain per-channel blend, weighted toward `a`. Not Colorize::mix — that is
// RgbaMixHsl below. This is the arithmetic `default_title_bar_background`
// writes out by hand on the two colours' channels.
Rgba RgbaMix(Rgba a, Rgba b, float t);

// gpui::Hsla — color.rs. Four floats 0..1, which is how GPUI carries a colour
// from the theme all the way to the GPU. This tree carries `Rgba` bytes
// instead, so an Hsla here is the *working* form: the shape a colour is put
// into for the operations Rust does in HSL — lightness, hue, `Colorize::mix`,
// the animation Lerp — and converted straight back out of.
//
// The cost of the difference is quantisation. Rust does a chain of these on
// floats and rounds once at the end; every step here goes back through eight
// bits a channel, so a long chain can drift a byte from what Rust computes.
// That is the same trade the rest of the palette makes (see ToByte in
// gpui.cpp), not a new one.
struct Hsla {
    float h = 0;
    float s = 0;
    float l = 0;
    float a = 0;
};

// gpui::hsla(): the four clamped into 0..1. Rust clamps here and nowhere
// else, so a hue computed past 1 is pinned rather than wrapped.
Hsla HslaNew(float h, float s, float l, float a);
// `impl From<Rgba> for Hsla`. A colour with no lightness left, or all of it,
// reports no saturation — Rust's `l == 0. || l == 1.` arm.
Hsla HslaFromRgba(Rgba c);
// `impl From<Hsla> for Rgba`. Nothing on the way in is clamped; the three
// channels are clamped on the way out, which is what lets a saturation or a
// lightness that ran past 1 land on a colour rather than on nonsense.
Rgba HslaToRgba(Hsla c);
// The two together, for a caller that has four numbers and wants a colour:
// `hsla(h, s, l, a).to_rgb()`.
Rgba RgbaHsla(float h, float s, float l, float a01);
// Colorize::hue: the same color turned to a new hue, keeping its saturation,
// lightness and alpha.
Rgba RgbaWithHue(Rgba c, float h01);
// Colorize::mix: the two colours interpolated in HSL, weighted toward `a`,
// with the hue taking the shorter way round the circle — so red mixed with
// blue is magenta and not the grey the same mix in RGB gives.
Rgba RgbaMixHsl(Rgba a, Rgba b, float factor);

// ─── background ───────────────────────────────────────────────────────────
//
// gpui::Background. A fill is one colour or a two-stop linear gradient, and
// every place that paints a surface takes one — GPUI's `Style::background` is
// a `Fill`, and `.bg(..)` accepts anything that converts into one. A theme
// file spells the gradient the way CSS does:
//
//     "primary.background": "linear-gradient(180deg, #1E293B, #0F172A)"
//
// so the type has to survive from `theme/color.rs`'s parser all the way to
// the D2D / cairo / Core Graphics brush. `color` is the solid fill and, for a
// gradient, its representative colour — the first stop, which is what Rust's
// `try_parse_theme_color` keeps for the flat `ThemeColor` field beside the
// renderable token. Reading `bg.color` off a gradient is therefore never
// wrong, only flat.
struct ColorStop {
    Rgba color = {};
    // Where along the gradient line this stop sits, 0..1.
    float percentage = 0;
};

struct Background {
    Rgba color = {};
    ColorStop from = {};
    ColorStop to = {};
    // CSS degrees: 0 points at the top of the box and turns clockwise, so 90
    // is `to right` and 180 — the default, and what a two-argument
    // `linear-gradient` means — is `to bottom`.
    float angle = 180.f;
    bool gradient = false;

    Background() = default;
    // Implicit, so the several hundred `->Bg(theme.foo)` calls that mean one
    // colour go on saying so. Rust gets the same from `impl From<Hsla> for
    // Background`.
    Background(Rgba c) : color(c) {}
};

// gpui::BoxShadow. The source style owns a Vec of these; a frame element
// borrows an arena copy through Style::shadows below. Keeping the primitive
// in gpui (rather than in Base's theme-token facade) lets ordinary elements
// paint the same multi-layer shadows the Rust Styled API carries.
struct BoxShadow {
    float x = 0;
    float y = 0;
    float blur = 0;
    float spread = 0;
    Rgba color = {};
    bool inset = false;
};

// gpui::linear_gradient(angle, from, to).
Background BackgroundLinear(float angle, ColorStop from, ColorStop to);
inline ColorStop ColorStopAt(Rgba c, float pct) {
    return ColorStop{c, pct};
}
// Background::opacity: every stop scaled by the same factor, which is what
// fading a whole element does to its fill.
Background BackgroundOpacity(Background b, float factor);
// Each stop's alpha capped independently at `max` — theme/color.rs's
// `try_parse_background_clamped`. Unlike scaling, a bright `to` stop cannot
// push the rendered highlight past the cap.
Background BackgroundClampAlpha(Background b, float max);
inline bool BackgroundIsSolid(const Background& b) {
    return !b.gradient;
}

// The text-field engine, in the input section below. El and HitRect name one
// before it is defined, the way they name SliderState.
struct InputState;

constexpr float kAuto = -1.f;
constexpr float kFill = -2.f;
constexpr float kPi = 3.14159265358979f;

// ─── Colorize (crates/ui/src/theme/color.rs) ─────────────────────────────
//
// The colour maths every theme fallback is written in. They are here rather
// than beside the registry because the palettes in code derive their own
// tokens with them.

// gpui::transparent_black(), which every `mix_oklab` toward nothing takes.
Rgba RgbaTransparent();
// Hsla::blend: `over` composited onto `base` by its own alpha. The result
// keeps the base's alpha, which is why `background.blend(x)` is opaque
// however faint `x` is.
Rgba RgbaBlend(Rgba base, Rgba over);
// Colorize::lighten / ::darken, which scale the HSL lightness rather than
// mixing toward white or black.
Rgba RgbaLighten(Rgba c, float amount);
Rgba RgbaDarken(Rgba c, float amount);
// Colorize::to_hex: `#RRGGBB`, and `#RRGGBBAA` when the colour is
// translucent. Upstream holds every colour as an `Hsla` and turns it back into
// bytes to print it, truncating each channel — so a colour that arrived as a
// hex string prints one below itself wherever the conversion does not land on
// a byte boundary, and that is the string a reader sees beside a swatch. A
// byte here has not been through that conversion, so the round trip is made
// here before the digits are written. A colour this tree mixed out of an HSL
// of its own is already on the far side of it and should be printed as it
// stands rather than through this.
Str RgbaToHex(Arena* a, Rgba c, bool upper = true);

// Colorize::mix_oklab, which is CSS `color-mix(in oklab, a factor%, b)`: the
// alpha is interpolated first and the Oklab channels are premultiplied by it,
// so mixing toward transparent fades without dragging the hue to black.
Rgba RgbaMixOklab(Rgba a, Rgba b, float factor);

// The semantic token layer is `base/theme_tokens.h`, and the word and
// line boundaries `base/text_boundary.h`: both are gpui-base modules that
// read a `Theme` or a `Str` and are used from `src/base` and up.

// ─── geometry ───────────────────────────────────────────────────────────
//
// crates/gpui/src/geometry.rs. Rust spells these `Point<T>`, `Size<T>`,
// `Bounds<T>` and `Edges<T>`, where `T` is not an element type but a *unit* —
// `Pixels`, `ScaledPixels`, `DevicePixels`, `Rems`, `Length` — so the compiler
// refuses to add device pixels to logical ones. Everything above Paint.h here
// is DIPs and always has been, which leaves that generic with one instantiation
// (`Point<Pixels>` is 170 of the 185 `Point<T>` in gpui-kit), so these
// are plain float structs and the arithmetic is written out once. The units
// that are not DIPs get their own named struct instead of a parameter:
// `WinSize` carries both the DIP and the device-pixel size of a window, and the
// backends scale on the way to Direct2D / cairo / Core Graphics.
//
// They are values: aggregates, no constructors, copied by the byte. Code that
// reads or writes one component at a time — the layout pass over `El`, a mouse
// event's position — keeps its flat fields; these are for what is produced,
// returned or passed as a unit.

// gpui::Axis, from the same file. `Along` is the field it picks out.
enum class Axis : uint8_t {
    Horizontal,
    Vertical
};

// The three float shapes are `base::PointF` / `SizeF` / `RectF`, shared with
// the taffy port — one definition, no conversion at the seam. gpui keeps its
// own names for them: `Size` and `Edges` read better in a widget than `SizeF`
// and `RectF` do, and `Edges<Pixels>` is what Rust calls the second one.
//
// `Size` keeps the `.w` / `.h` it always had. `Edges` does not keep its field
// *order*: the shared one is left, right, top, bottom, where Rust's
// Edges<Pixels> is top, right, bottom, left, so a braced `Edges{...}` means
// something different than it used to. `Edges::New(l, r, t, b)` says which.
using Point = base::PointF;
using Size = base::SizeF;
using Edges = base::RectF;

// Bounds<Pixels>. Rust composes it from an origin and a size; here the four
// floats are the struct, so there is no `.origin` or `.size` to reach for.
struct Bounds {
    float x = 0, y = 0, w = 0, h = 0;

    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    float CenterX() const { return x + w * 0.5f; }
    float CenterY() const { return y + h * 0.5f; }
    // Bounds::contains: the top and left edges are inside, the bottom and
    // right ones are not, so abutting boxes never both claim a point.
    bool Contains(Point p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
    // Bounds::inset, which is Bounds::dilate with the amount negated, so a
    // positive amount shrinks the box.
    Bounds Inset(float d) const { return {x + d, y + d, w - d - d, h - d - d}; }
    // Bounds::extend, negated the same way: the content box inside padding.
    Bounds Inset(Edges e) const {
        return {x + e.left, y + e.top, w - e.HorizontalAxisSum(),
                h - e.VerticalAxisSum()};
    }
};

// Bounds::new(origin, size), for the callers that hold the two apart.
inline Bounds BoundsAt(Point origin, Size size) {
    return {origin.x, origin.y, size.w, size.h};
}

// Where a CSS `linear-gradient` puts its two ends inside a box. The gradient
// line runs through the centre at `angle`, and is long enough that the two
// corners it points between land exactly on 0% and 100% — which is what makes
// a 45-degree gradient reach the corners rather than stopping short of them.
// The points come back at the stops' own percentages, so the caller hands the
// backend two colours and two positions and nothing else: all three clamp
// beyond their ends (D2D's default extend, cairo's PAD, Core Graphics' draws-
// before/after), so a stop at 25% still paints the quarter behind it.
void BackgroundLine(const Background& b, Bounds box, Point* p0, Point* p1);

// ─── entities ─────────────────────────────────────────────────────────────
//
// GPUI keeps view state in `App` and hands out `Entity<T>` handles; a view
// implements `Render` and mutates itself through `Context<T>`. The same shape
// here, minus the refcounting: `App` owns the state, `Entity<T>` is a POD
// generational handle, and `Ctx` is the one context type (GPUI splits it into
// `&mut App` / `&mut Window` / `&mut Context<T>` only to satisfy the borrow
// checker).

struct Window;
struct Ctx;
struct El;
struct SliderState;
// The shaped run a text element measured to; Paint.h owns the type.
struct TextLayout;

struct EntityId {
    int32_t index = -1;
    uint32_t gen = 0; // 0 == null handle

    bool IsValid() const { return index >= 0 && gen != 0; }
};

inline bool operator==(EntityId a, EntityId b) {
    return a.index == b.index && a.gen == b.gen;
}
inline bool operator!=(EntityId a, EntityId b) {
    return !(a == b);
}

using RenderFn = El* (*)(void* self, Ctx* cx);
using DropFn = void (*)(void* self);

struct EntitySlot {
    void* ptr = nullptr;
    uint32_t gen = 0;
    RenderFn render = nullptr;
    DropFn drop = nullptr;
};

// Where one transition has got to, kept per window and per id. Separate from
// KeyedSlot because the lifetime is GPUI's element state rather than Rust's
// keyed state: a slot nothing asked for while the frame was built is dropped,
// so a dialog that closes and opens again animates its way in a second time.
struct MotionSlotRec {
    uint32_t key = 0;
    // The frame that last asked for it. `frameSeq` at the time, so the sweep
    // is a comparison rather than a flag to clear.
    uint64_t frame = 0;
    void* ptr = nullptr;
};

// window.use_keyed_state: per-window state owned by a RenderOnce element that
// has nowhere else to keep it.
struct KeyedSlot {
    uint32_t key = 0;
    void* ptr = nullptr;
    DropFn drop = nullptr;
    // Set when the slot was taken through KeyedEntity: the app owns the
    // memory then, and the window only remembers which entity the key means.
    EntityId entity = {};
};

// ─── mouse input ──────────────────────────────────────────────────────────
// crates/gpui/src/interactive.rs, field for field, minus the four things a
// C++ struct cannot say the same way:
//   * `MouseButton::Navigate(NavigationDirection)` carries its direction; a
//     C++ enumerator cannot, so the two directions are their own constants.
//   * `Option<MouseButton>` on a move is a `pressed` flag plus the button.
//   * `ScrollDelta::Pixels | Lines` is a delta plus `precise`. Rust defers the
//     multiply to `pixel_delta(line_height)`; the three windows here turn a
//     notch into DIPs at the seam, so nothing downstream needs a line height.
//   * `Point<Pixels>` is `x` and `y`. There is a `Point` above, but an
//     event's position is read a component at a time, and flattening it
//     spares every handler a `.position`.
// What is missing outright: touch, pinch and pressure, which none of these
// three windows report.

enum class MouseButton : uint8_t {
    Left,
    Right,
    Middle,
    NavigateBack,
    NavigateForward
};

// GPUI's Modifiers. `platform` is the Windows key, X11's Super and macOS's
// Command; `function` is Fn, which only macOS reports.
struct Modifiers {
    bool control = false;
    bool alt = false;
    bool shift = false;
    bool platform = false;
    bool function = false;

    bool Modified() const {
        return control || alt || shift || platform || function;
    }
    // The semantically secondary modifier: Command on macOS, Control on the
    // other two — Modifiers::secondary().
    bool Secondary() const {
#if GPUI_OS_MAC
        return platform;
#else
        return control;
#endif
    }
    int Count() const {
        return (int)control + (int)alt + (int)shift + (int)platform +
               (int)function;
    }
};

// The phase of a scroll gesture. A wheel notch is one Moved; a trackpad on
// macOS runs Started -> Moved -> Ended.
enum class TouchPhase : uint8_t {
    Started,
    Moved,
    Ended,
    Cancelled
};

// gpui::OngoingScroll. Precise scrolling is a gesture rather than a series of
// unrelated wheel notches, so the axis chosen by its first delta stays chosen
// while a trackpad wobbles. A strong turn (twice as much motion on the other
// axis) releases the lock, matching GPUI's filter used by
// gpui-base::OngoingScrollExt.
struct OngoingScroll {
    Axis axis = Axis::Horizontal;
    bool active = false;

    void Filter(Point* delta, TouchPhase phase);
};

// DispatchPhase, from GPUI's `Window::dispatch_event`. A mouse event is
// offered to the chain of elements under the pointer twice: outside-in in the
// Capture phase, where an ancestor can pre-empt what is inside it, and then
// inside-out in the Bubble phase, which is where a handler that only cares
// about its own element sits. `WindowStopPropagation` is `cx.stop_propagation`
// — the rest of the chain does not hear it.
enum class DispatchPhase : uint8_t {
    Capture,
    Bubble
};

struct MouseDownEvent {
    MouseButton button = MouseButton::Left;
    float x = 0;
    float y = 0;
    // The box of the element the press landed on, when it has an identity —
    // the same thing ClickEvent::el carries, and what a handler needs to
    // place something where the press was inside it. A Rust hitbox has the
    // bounds too; the event does not, because the closure already has them.
    Bounds el = {};
    Modifiers modifiers = {};
    // How many presses this one is in an unbroken run: 1, 2, 3… What Rust's
    // on_double_click tests — `on_click(|ev, ..| ev.click_count() == 2)`.
    int clickCount = 1;
    // The press that also activated the window. Windows knows from
    // WM_MOUSEACTIVATE; X11 has no such notion and a Cocoa view does not
    // accept the first mouse, so it is false on those two.
    bool firstMouse = false;
    // Which pass of the chain this is. A handler registered for one phase only
    // ever sees that phase; the field is there for one that took both.
    DispatchPhase phase = DispatchPhase::Bubble;

    // MouseDownEvent::is_focusing.
    bool IsFocusing() const { return button == MouseButton::Left; }
};

struct MouseUpEvent {
    MouseButton button = MouseButton::Left;
    float x = 0;
    float y = 0;
    // The box of the element that heard it, the way MouseDownEvent carries
    // one. The chain fills it in as the event walks.
    Bounds el = {};
    Modifiers modifiers = {};
    int clickCount = 1;
    DispatchPhase phase = DispatchPhase::Bubble;

    bool IsFocusing() const { return button == MouseButton::Left; }
};

struct MouseMoveEvent {
    float x = 0;
    float y = 0;
    // Filled for an element-level on_mouse_move listener. Window-level moves
    // leave it empty, as GPUI's window subscription does.
    Bounds el = {};
    // Rust's Option<MouseButton>: `pressed` is the Some, `pressedButton` its
    // value. With no button down, pressedButton means nothing.
    bool pressed = false;
    MouseButton pressedButton = MouseButton::Left;
    Modifiers modifiers = {};

    // MouseMoveEvent::dragging.
    bool Dragging() const {
        return pressed && pressedButton == MouseButton::Left;
    }
};

// What a drag carries. Rust's `on_drag(payload, ..)` takes a value of any
// type and `DragMoveEvent<T>` only reaches the handlers that named that type;
// there is no type to match on here, so the payload says what kind of thing
// is being dragged by name and which one of that kind by index.
struct DragPayload {
    Str kind = {};
    int ix = 0;
    void* data = nullptr;

    bool IsValid() const { return kind.s != nullptr; }
};

// DragMoveEvent<T>: what is being dragged, and the move that carried it.
struct DragMoveEvent {
    DragPayload drag = {};
    MouseMoveEvent event = {};
    // The dragged element's box, which is `bounds` on the entity the drag
    // names in Rust. It is the box the last frame laid out, so a handler that
    // moves the element reads its own answer back on the next move.
    Bounds el = {};
};

// on_drop::<T>: a drag that let go over this element. Rust matches the drop
// handler by the payload's type; here the element says which `kind` it takes,
// and a drag carrying anything else passes over it as if it were not there.
struct DropEvent {
    DragPayload drag = {};
    // Where the button came up, in window coordinates.
    float x = 0;
    float y = 0;
    // The box of the element that took the drop, so a handler can work out
    // where inside itself the drop landed.
    Bounds el = {};
};

// The pointer left the window. GPUI's MouseExitEvent is a MouseMoveEvent in
// all but name — it derefs to one — so it carries the same three things.
struct MouseExitEvent {
    float x = 0;
    float y = 0;
    bool pressed = false;
    MouseButton pressedButton = MouseButton::Left;
    Modifiers modifiers = {};
};

struct ScrollWheelEvent {
    float x = 0;
    float y = 0;
    // DIPs, already scaled: one wheel notch is 48. Positive scrolls the view
    // up and left, which is the sign WM_MOUSEWHEEL reports.
    float deltaX = 0;
    float deltaY = 0;
    // ScrollDelta::Pixels rather than ::Lines — a trackpad or another device
    // that measures the gesture itself, not a wheel with detents.
    bool precise = false;
    Modifiers modifiers = {};
    TouchPhase phase = TouchPhase::Moved;
    // cx.propagate(): an `El::OnScrollWheel` handler that leaves this true
    // lets the gesture carry on to the scrolled box underneath. Unused by the
    // window-level `WindowOnScrollWheel`, which is the last thing to see it.
    bool propagate = true;
};

// GPUI's PlatformInput: what a platform window hands to the window layer.
// Rust's enum carries its payload; here a kind and a union of the same structs
// do. Only the mouse variants exist — keys still arrive through WindowKeyDown
// and WindowChar, whose KeyEvent is a merged key-and-character event rather
// than GPUI's Keystroke, and nothing here produces a file drop or a gesture.
enum class PlatformInputKind : uint8_t {
    MouseDown,
    MouseUp,
    MouseMove,
    MouseExited,
    ScrollWheel
};

struct PlatformInput {
    PlatformInputKind kind = PlatformInputKind::MouseMove;
    union {
        MouseDownEvent mouseDown = {};
        MouseUpEvent mouseUp;
        MouseMoveEvent mouseMove;
        MouseExitEvent mouseExited;
        ScrollWheelEvent scrollWheel;
    };
};

// GPUI's ClickEvent is the down and up pair (ClickEvent::Mouse) or the Enter
// or Space that activated a focused element (ClickEvent::Keyboard). This one
// is flat: it fires from the release, like Rust's, and carries the position
// the release landed at with the count and the modifiers the press had. It
// also carries what our hit rect knows and a Rust hitbox does not have to —
// which element this was, and where it is.
struct ClickEvent {
    float x = 0;
    float y = 0;
    MouseButton button = MouseButton::Left;
    // The element's click id, when it has one. Lets one handler serve a list.
    int id = 0;
    // The box that was hit, so a handler can place the click inside it — what
    // a slider needs to turn a press on its track into a value. This is also
    // KeyboardClickEvent::bounds, the only position a keyboard click has.
    Bounds el = {};
    int clickCount = 1;
    Modifiers modifiers = {};
    // ClickEvent::Keyboard: Space or Enter on the focused element, with no
    // pointer anywhere near it. ClickEvent::is_keyboard.
    bool keyboard = false;
    // KeyboardClickEvent::button: which of the two activated it, KeyReturn or
    // KeySpace. 0 when the pointer made this click.
    int keyboardKey = 0;
};

// Portable key codes. Where Win32 has a VK_* value, that is the value here, so
// the Windows window passes wParam straight through and the other windows map
// their native keys onto it. F25..F35 and the XF86-only commands start at 256:
// Windows has no virtual-key values for them, but Rust GPUI names them on the
// platforms that do.
enum : int {
    KeyBack = 8,
    KeyTab = 9,
    KeyReturn = 13,
    KeyShift = 16,
    KeyControl = 17,
    KeyAlt = 18,
    // Compatibility for the old direct VK_MENU spelling. The context-menu
    // key is KeyApps and is written "menu" in a binding.
    KeyMenu = KeyAlt,
    KeyEscape = 27,
    KeySpace = 32,
    KeyPageUp = 33,
    KeyPageDown = 34,
    KeyEnd = 35,
    KeyHome = 36,
    KeyLeft = 37,
    KeyUp = 38,
    KeyRight = 39,
    KeyDown = 40,
    KeyInsert = 45,
    KeyDelete = 46,
    // Letters and digits are their ASCII uppercase / digit codes.
    KeyA = 65,
    KeyC = 67,
    KeyE = 69,
    KeyF = 70,
    KeyH = 72,
    KeyV = 86,
    KeyX = 88,
    KeyY = 89,
    KeyZ = 90,
    KeyApps = 93,
    KeyF1 = 112,
    KeyF24 = 135,
    KeyBrowserBack = 166,
    KeyBrowserForward = 167,
    // Win32's OEM punctuation keys. X11 and Cocoa map both the unshifted and
    // shifted character on each physical key onto the same code.
    KeySemicolon = 186,
    KeyEqual = 187,
    KeyComma = 188,
    KeyMinus = 189,
    KeyPeriod = 190,
    KeySlash = 191,
    KeyBacktick = 192,
    KeyLeftBracket = 219,
    KeyBackslash = 220,
    KeyRightBracket = 221,
    KeyQuote = 222,
    // Portable-only codes, outside the uint8_t Win32 VK range.
    KeyF25 = 256,
    KeyF35 = 266,
    KeyCut = 267,
    KeyCopy = 268,
    KeyPaste = 269,
    KeyNew = 270,
    KeyOpen = 271,
    KeySave = 272,
    KeyKpAdd = 273,
    KeyKpSubtract = 274,
    KeyKpMultiply = 275,
    KeyKpDivide = 276,
    KeyKpDecimal = 277,
    KeyKpSeparator = 278,
    KeyKpEqual = 279,
    KeyKpBegin = 280
};

struct KeyEvent {
    int vk = 0;      // a Key* code, 0 for a typed character
    uint32_t ch = 0; // typed codepoint, 0 for key down/up
    bool down = false;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    // Command on macOS, the Windows/Super key elsewhere.
    bool platform = false;
    // Fn. Only macOS reports it as a modifier of an ordinary key.
    bool function = false;
    // cx.propagate(): an `El::OnKeyDown` handler that leaves this true passes
    // the keystroke on outwards, the way an action handler does. A
    // window-level `WindowOnKey` is last; clearing this there tells Windows
    // not to perform default handling for a WM_SYSKEYDOWN Alt chord.
    bool propagate = true;
};

// The pointer shape the window asks the OS for. GPUI spells this
// CursorStyle and has a dozen; these are the two the element tree can tell
// apart today.
enum class CursorKind : uint8_t {
    Arrow,
    IBeam,
    // cursor_pointer: the hand, which says a thing is there to be clicked.
    // GPUI's own default for a div is the arrow, so this is opt-in; Rust asks
    // for it on links and on the button variants that look like one.
    Pointer,
    // cursor_col_resize, which a table's column edge asks for.
    ColResize,
    // cursor_row_resize: the handle between two panels stacked one over the
    // other.
    RowResize,
    // cursor_crosshair: a canvas that is drawn on rather than clicked.
    Crosshair
};

// Fired by a window timer; GPUI does this with cx.spawn + Timer::after.
struct TickEvent {
    int ms = 0;
};

// What GPUI's `on_hover` hands its closure: a `&bool` saying whether the
// pointer just entered the element or just left it. It fires on the change,
// not on every move inside.
struct HoverEvent {
    bool hovered = false;
};

// TextView::max_lines reports whether its natural content ran past the capped
// box after layout. Kept in gpui because the line clamp itself is an element
// seam: ui/text supplies the state listener, while the runtime owns the final
// boxes and content mask.
struct LineClampEvent {
    bool clamped = false;
};

// cx.listener(...): a handler plus the entity it runs against. Dispatch looks
// the entity up and drops the event if the handle went stale.
//
// `arg` is what the Rust closure would have captured — the tab index in
// `cx.listener(move |this, _, _, cx| this.tab = ix)`. Without it a view has to
// hand out element ids and decode them again in one big switch.
using ListenerFn = void (*)(void* self, Ctx* cx, const void* ev);
using ListenerArgFn = void (*)(void* self, Ctx* cx, const void* ev,
                               intptr_t arg);

struct Listener {
    // User-space code addresses and wasm table indexes leave the top two bits
    // free. Keeping the argument-shape flags there preserves unaligned MSVC
    // incremental-link thunks without shifting the callback on either path.
    static constexpr int kPointerBits = sizeof(uintptr_t) * 8;
    static constexpr uintptr_t kHasArg = uintptr_t(1) << (kPointerBits - 1);
    static constexpr uintptr_t kArgBound = uintptr_t(1) << (kPointerBits - 2);
    static constexpr uintptr_t kFlags = kHasArg | kArgBound;

    uintptr_t fn = 0;
    EntityId view = {};
    intptr_t arg = 0;

    template <typename F>
    void SetFn(F value) {
        fn = (uintptr_t)value | (fn & kFlags);
    }

    uintptr_t Fn() const { return fn & ~kFlags; }

    bool HasArg() const { return (fn & kHasArg) != 0; }
    bool ArgBound() const { return (fn & kArgBound) != 0; }
    void SetHasArg() { fn |= kHasArg; }
    void SetArgBound() { fn |= kHasArg | kArgBound; }
    bool IsValid() const { return Fn() != 0; }
};

static_assert(sizeof(Listener) <= 24,
              "keep Listener flags packed into the function word");

// One armed timer. GPUI has no timer list: it spawns a task per timer and the
// Task handle cancels on drop. Here the window keeps them, and dispatch drops
// one whose view went stale — which is the same lifetime, spelled differently.
struct TimerSub {
    int id = 0; // what WindowCancelTimer takes
    int ms = 0;
    double dueAt = 0; // TimeNow() deadline
    bool repeat = false;
    Listener l;
};

// ─── style / element ──────────────────────────────────────────────────────

enum class ElKind : uint8_t {
    Div,
    Text,
    Chart,
    Progress,
    Icon,
    // gpui's img(..): a decoded bitmap, sized by its own pixels unless the
    // caller says otherwise. An image whose source cannot be decoded — a
    // remote URL, a format the platform does not read — paints its `text`
    // instead, which is the alt text a document gave it.
    Image
};

// How an image's intrinsic size is placed inside the box layout assigned it.
// This is gpui::ObjectFit, with the CSS object-fit variants GPUI exposes.
enum class ObjectFit : uint8_t {
    Fill,
    Contain,
    Cover,
    ScaleDown,
    None,
};

enum class ImageLoadState : uint8_t {
    Loading,
    Ready,
    Failed,
};

struct PaintApp;
struct RenderImage;

enum class ImageSourceKind : uint8_t {
    Resource,
    Render,
    Image,
    Custom,
};

// Rust's custom ImageSource closure returns Option<Result<Arc<RenderImage>>>.
// The callback expresses that as a state and writes a borrowed image for
// Ready. Its owner must keep that image alive through the frame; recorded
// scenes retain it when they need it longer.
using ImageSourceLoader = ImageLoadState (*)(PaintApp* paint, void* user,
                                             RenderImage** image);

// A frame-local, non-owning image source. Resource strings and encoded bytes
// are borrowed until the element has been painted. Render and Custom images
// are borrowed from their caller, matching Entity handles elsewhere in this
// API; RenderImageRetain gives a caller explicit longer ownership.
struct ImageSource {
    Str resource = {};
    const uint8_t* bytes = nullptr;
    RenderImage* render = nullptr;
    ImageSourceLoader loader = nullptr;
    void* user = nullptr;
    int bytesLen = 0;
    ImageSourceKind kind = ImageSourceKind::Resource;

    static ImageSource FromResource(Str resource);
    static ImageSource FromRender(RenderImage* image);
    static ImageSource FromImage(const uint8_t* bytes, int len);
    static ImageSource FromCustom(ImageSourceLoader loader, void* user);
};

Bounds ObjectFitBounds(ObjectFit fit, Bounds bounds, Size imageSize);

// gpui's Display. `div()` is a block container, the way an unstyled HTML
// element is: children stack down the page at the container's full width, and
// nothing is stretched or shrunk to make them fit. `flex()` — or either of the
// h_flex/v_flex helpers that call it — is what turns on the flex model.
enum class Display : uint8_t {
    Block,
    Flex
};

enum class FlexDir : uint8_t {
    Row,
    Col,
    // flex_row_reverse / flex_col_reverse: the same axis, laid out from the
    // far end. A toolbar that pins its buttons to the right without asking
    // for a justification is written this way.
    RowReverse,
    ColReverse
};
// Flex-box cross-axis alignment. Named apart from Base Positioner's public
// `Align`, which is the leading/center/trailing alignment of an overlay along
// whichever side it resolved to.
enum class FlexAlign : uint8_t {
    Start,
    Center,
    End,
    Stretch
};
enum class Justify : uint8_t {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround
};
// gpui's Overflow, per axis: `overflow_hidden` clips and
// `overflow_x_scroll` / `overflow_y_scroll` scroll.
enum class Overflow : uint8_t {
    Visible,
    Hidden,
    Scroll
};

// ScrollbarMode, crates/base/src/scrollbar.rs. Rust's default is Scrolling —
// the bar is up while the offset moves, holds for FADE_OUT_DELAY idle seconds
// and then fades over the rest of FADE_OUT_DURATION. It needs a clock per
// scroll area, which is keyed off `El::ScrollId` the way Rust keys its state
// off the element id; an area with no id of its own has nowhere to keep the
// clock and stays up. The theme default is Rust's Scrolling; the story's
// Appearance menu offers all three.
enum class ScrollbarMode : uint8_t {
    Always,
    Hover,
    Scrolling
};

// The deliberately small style seam consumed by the GPUI runtime. The
// component theme projects its active palette into this value; the renderer
// never depends on the much larger component-specific Theme/ThemeTokens
// surface. Defaults keep the bare runtime usable without src/ui.
struct RuntimeStyle {
    Rgba background{Rgb(0xfa, 0xfa, 0xfa)};
    Rgba foreground{Rgb(0x17, 0x17, 0x17)};
    Rgba mutedForeground{Rgb(0x73, 0x73, 0x73)};
    Rgba border{Rgb(0xe5, 0xe5, 0xe5)};
    Rgba ring{Rgb(0x17, 0x17, 0x17)};
    Rgba inspectorAccent{Rgb(0x3b, 0x82, 0xf6)};
    Rgba popover{Rgb(0xfa, 0xfa, 0xfa)};
    Rgba popoverForeground{Rgb(0x17, 0x17, 0x17)};
    Background progress{Rgb(0x17, 0x17, 0x17)};
    Background scrollbarThumb{Rgba8(0x17, 0x17, 0x17, 0x33)};
    Background scrollbarThumbHover{Rgba8(0x17, 0x17, 0x17, 0x66)};
    Background scrollbarTrack{Rgba8(0, 0, 0, 0)};

    // Compatibility styling for the old arena-only ButtonEl helper. New UI
    // code uses component::Button and never reads these fields directly.
    Background legacyPrimary{Rgb(0x17, 0x17, 0x17)};
    Rgba legacyPrimaryForeground{Rgb(0xfa, 0xfa, 0xfa)};
    Rgba legacyPrimaryHover{Rgb(0x35, 0x35, 0x35)};
    Background legacyMuted{Rgb(0xf5, 0xf5, 0xf5)};
    Background legacySecondary{Rgb(0xf5, 0xf5, 0xf5)};
    Rgba legacySecondaryForeground{Rgb(0x17, 0x17, 0x17)};
    Rgba legacySecondaryHover{Rgb(0xe5, 0xe5, 0xe5)};
    Rgba legacySecondaryActive{Rgb(0xd4, 0xd4, 0xd4)};

    float radius = 6.f;
    float fontSize = 16.f;
    ScrollbarMode scrollbarMode = ScrollbarMode::Scrolling;
    bool focusRing = true;
};

const RuntimeStyle& RuntimeStyleNow(const App* app);
void RuntimeStyleInstall(App* app, const RuntimeStyle& style);

// FADE_OUT_DELAY / FADE_OUT_DURATION, in seconds. The curve between them is
// Rust's `1 - (elapsed - delay)^10`: flat for most of the second, then a
// drop off the end.
// `WindowOptions::inactive_frame_interval`: how long a window that is not
// the active one waits between animation frames. 500 ms caps background
// animation at 2 FPS, which is what the story app asks for upstream.
const double kInactiveFrameInterval = 0.5;

// RADIUS_FULL: past half the shorter side, so the paint clamps it to exactly
// as round as the box goes.
const float kRadiusFull = 9999.f;

// The stacking layers a frame paints in, in the order it paints them. Rust
// gives a deferred element a `with_priority` and lets GPUI's scene sort on
// it — `POPUP_PRIORITY` is 100 and `TOOLTIP_PRIORITY` is 200, which is what
// keeps a tip above a dialog or a popup — and the walks here already run in
// that order, so what the numbers have to do is keep the same relation.
const int kPaintLayerTree = 0;
// The deferred and fixed elements the tree painted over: popups, dialogs,
// menus. Rust's POPUP_PRIORITY.
const int kPaintLayerPopup = 1;
// TooltipOverlay, over everything the frame drew.
const int kPaintLayerTooltip = 2;
// The inspector's highlights, which GPUI paints over everything.
const int kPaintLayerInspector = 3;

// crates/base/src/scrollbar.rs `ScrollbarEntrance`: how a bar arrives. The
// styled layer chooses the choreography; base only plays it.
enum class ScrollbarEntrance : uint8_t {
    // Fade in without moving.
    Fade,
    // Slide in from the nearest edge while fading.
    SlideAndFade
};

// `ScrollbarMotion`, in seconds rather than Duration. Base installs no motion
// of its own — every duration there defaults to zero and both visibility and
// thumb width snap — and the timing below is what crates/ui projects onto it
// in `theme/mod.rs`'s `scrollbar_motion`.
struct ScrollbarMotion {
    // How long visibility is held after the last scroll, drag, or hover.
    float idle = 2;
    // How long the bar takes to become fully visible.
    float enter = 0;
    // How long it takes to fade away once the idle hold expires.
    float exit = 0;
    // How long the thumb takes to reach a new width.
    float expand = 0;
    ScrollbarEntrance entrance = ScrollbarEntrance::Fade;
    ScrollbarEntrance thumbHoverEntrance = ScrollbarEntrance::Fade;

    ScrollbarMotion WithIdle(float value) const {
        ScrollbarMotion out = *this;
        out.idle = value;
        return out;
    }
    ScrollbarMotion WithEnter(float value) const {
        ScrollbarMotion out = *this;
        out.enter = value;
        return out;
    }
    ScrollbarMotion WithExit(float value) const {
        ScrollbarMotion out = *this;
        out.exit = value;
        return out;
    }
    ScrollbarMotion WithExpand(float value) const {
        ScrollbarMotion out = *this;
        out.expand = value;
        return out;
    }
    ScrollbarMotion WithEntrance(ScrollbarEntrance value) const {
        ScrollbarMotion out = *this;
        out.entrance = value;
        return out;
    }
    ScrollbarMotion WithThumbHoverEntrance(ScrollbarEntrance value) const {
        ScrollbarMotion out = *this;
        out.thumbHoverEntrance = value;
        return out;
    }
};

// The motion this design system projects for a mode. Scrolling and track
// hover reveal a bar by fading it in place; in hover mode, pointing at the
// thumb slides it in from the nearest edge as it fades. Reduced motion zeroes
// every duration, which is how a policy with no motion arrives here.
ScrollbarMotion ScrollbarMotionFor(ScrollbarMode mode);

// What `VisibilityAnimation::sample` answers: how much of the bar is there,
// how far along its slide it is, and whether it is still moving.
struct ScrollbarVisibility {
    float opacity = 0;
    float position = 0;
    bool running = false;
};

// The animation itself, keyed by scroll id the way the painted bars are.
// These are the seam tests/ScrollbarTests.cpp drives the curves through —
// `now` is the clock rather than TimeNow(), so a test can step it — and
// nothing else needs them: a frame reaches the same state through paint.
void ScrollbarVisibilitySet(int scrollId, bool visible,
                            ScrollbarEntrance entrance, float enter, float exit,
                            double now);
ScrollbarVisibility ScrollbarVisibilityAt(int scrollId, double now);
// `visibility_translation`: how far off its edge the bar is drawn at that
// point in the slide. Zero once it has arrived.
float ScrollbarSlideOffset(float trackWidth, float position);

// Drops what the Scrolling bars remember. The app's own teardown; a caller
// has no reason to.
void ScrollFadeClear();

enum class IconName : uint8_t {
    None = 0,
    ALargeSmall,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    Asterisk,
    Battery,
    BatteryCharging,
    BatteryFull,
    BatteryLow,
    BatteryMedium,
    BatteryWarning,
    Bell,
    BookOpen,
    Bot,
    Building2,
    Calendar,
    CaseSensitive,
    ChartPie,
    Check,
    ChevronDown,
    ChevronLeft,
    ChevronRight,
    ChevronUp,
    ChevronsUpDown,
    CircleCheck,
    CircleUser,
    CircleX,
    Close,
    Copy,
    Cpu,
    Dash,
    Delete,
    Ellipsis,
    EllipsisVertical,
    ExternalLink,
    EyeOff,
    Eye,
    File,
    FileText,
    Folder,
    FolderClosed,
    FolderOpen,
    Frame,
    GalleryVerticalEnd,
    Github,
    Globe,
    HardDrive,
    Heart,
    HeartOff,
    Inbox,
    Info,
    Inspector,
    LayoutDashboard,
    Loader,
    LoaderCircle,
    Map,
    Maximize,
    MemoryStick,
    Menu,
    Minimize,
    Minus,
    Moon,
    Network,
    Palette,
    PanelBottom,
    PanelBottomOpen,
    PanelLeft,
    PanelLeftClose,
    PanelLeftOpen,
    PanelRight,
    PanelRightClose,
    PanelRightOpen,
    Pause,
    Play,
    Plus,
    Redo,
    Redo2,
    Replace,
    ResizeCorner,
    RotateCw,
    Search,
    Settings,
    Settings2,
    SortAscending,
    SortDescending,
    SquareTerminal,
    Star,
    StarFill,
    StarOff,
    Sun,
    ThumbsDown,
    ThumbsUp,
    TriangleAlert,
    Undo,
    Undo2,
    User,
    WindowClose,
    WindowMaximize,
    WindowMinimize,
    WindowRestore,

    // Compatibility with the earlier port. Pinned gpui-kit calls this
    // asset `Close`; keeping X avoids forcing applications to migrate in one
    // release while both names remain backed by their exact SVGs.
    X,
};

struct PaintCtx;

// A run of a text element painted differently from the rest of it: GPUI's
// HighlightStyle over a range, which is what a syntax highlighter's captures
// and an editor's TextDecorations both come to. The runs are UTF-8 offsets
// into the element's own text and must not overlap; whatever they leave over
// paints in the element's own colour.
//
// Weight and slant are not here. A run drawn in another face would shape to
// other widths, and every run of an element shares one shaped layout — the
// colour, the wash behind it and the rule under it are what can change
// without re-shaping.
struct TextSpan {
    int lo = 0;
    int hi = 0;
    Rgba color = {};
    // The wash behind the run, painted before the glyphs. Alpha 0 is none,
    // which is what a run that only recolours its glyphs wants — and the
    // reason it is spelled out: an Rgba defaults to opaque.
    Rgba bg = {0, 0, 0, 0};
    // UnderlineStyle: a rule under the run in its own colour.
    bool underline = false;
    // UnderlineStyle::wavy — the squiggle a diagnostic is marked with.
    bool wavy = false;
};

// Which of crates/ui/src/chart's charts this series is. They share the axis,
// the grid and the labels; what differs is the shape drawn over them.
enum class ChartKind : uint8_t {
    Area,
    Line,
    Bar,
    Candlestick,
    Radar
};

// plot::StrokeStyle. How a run of points is joined: the Catmull-Rom curve
// GPUI draws by default, straight segments, or a stair that steps after each
// point.
enum class ChartStroke : uint8_t {
    Natural,
    Linear,
    StepAfter
};

// BarAlignment: which edge of the plot a bar grows from. Bottom is the usual
// column; Left and Right lay the bands down the side and make it a row chart.
enum class BarAlign : uint8_t {
    Bottom,
    Top,
    Left,
    Right
};

// One more band or line over the same axes. Rust's AreaChart takes a `y`
// accessor per series — `.y(..).stroke(..).fill(..).name(..)`, as many times
// as there are series — and they share one domain and one grid. This is that
// list, past the first, which lives on the ChartSeries itself.
struct ChartSeriesExtra {
    const float* ys = nullptr;
    Rgba stroke = {};
    Rgba fillTop = {};
    Rgba fillBot = {};
    // The series' own name in the tooltip, the way `name(..)` sets it.
    Str name = {};
};

struct ChartSeries {
    ChartKind kind = ChartKind::Area;
    const float* ys = nullptr;
    int n = 0;
    // The series after the first. They share `n`, the domain and the axes.
    const ChartSeriesExtra* more = nullptr;
    int nMore = 0;
    int tickMargin = 15;
    // The x-axis labels, one per point; without them the index is drawn.
    const char* const* labels = nullptr;
    // A second series drawn over the first, as a stacked area chart does.
    bool overlay = false;
    Rgba stroke = {};
    Rgba fillTop = {};
    Rgba fillBot = {};
    // The value domain the y axis is scaled to. Both zero takes it from the
    // data, which is what a ScaleLinear over the data's own extent does; the
    // system monitor's charts say 0..100 instead.
    float domainMin = 0;
    float domainMax = 0;
    // Candlestick: the other three values per point, and the two colors a
    // candle takes depending on which way it closed.
    const float* opens = nullptr;
    const float* highs = nullptr;
    const float* lows = nullptr;
    Rgba up = {};
    Rgba down = {};
    // Bar: ScaleBand's inner padding, and how round the top of a bar is.
    float bandPadding = 0.2f;
    float barRadius = 4;
    BarAlign barAlign = BarAlign::Bottom;
    // Stack: where each bar starts, so a series drawn over another one sits
    // on top of it rather than in front of it. Null starts every bar at zero.
    const float* bases = nullptr;
    // BarChart::label: the value written at the bar's growing end.
    bool barLabels = false;
    // BarChart::value_axis: tick labels down the value axis, which reserve
    // kValueAxisGap along the band axis for themselves.
    bool valueAxis = false;
    // BarChart::value_tick_count: how many even intervals the value axis is
    // divided into, which drives the grid spacing and the labels alike. A
    // count, unlike tickMargin, which is a stride over the band categories.
    int valueTickCount = 4;
    // BarChart::fill(|d, ..|): a colour per bar rather than one for the lot.
    const Rgba* barFills = nullptr;
    // BarChart::fill_gradient: the two stops a bar is filled between. Run
    // across the chart's own range by default, or down each bar on its own
    // when barGradientPerBar is set.
    bool barGradient = false;
    bool barGradientPerBar = false;
    // fill(|_, bar, chart, _|): one ramp across the whole plot, on its
    // bottom-left to top-right diagonal, with every bar filled by the slice
    // of it that falls under its own footprint. The stops then differ from
    // bar to bar, which the other two modes' single pair cannot express.
    bool barGradientDiagonal = false;
    Rgba barFillFrom = {};
    Rgba barFillTo = {};
    // StrokeStyle and LineChart::dot, both of which the area chart shares.
    ChartStroke strokeStyle = ChartStroke::Natural;
    bool dot = false;
    // CandlestickChart::body_width_ratio: how much of a band the body takes.
    float bodyWidthRatio = 0.8f;
    // RadarChart::outer_radius / grid_levels, and its own dot flag.
    float radarRadius = 0;
    int gridLevels = 4;
    // AreaChart::id in Rust: a chart with one takes the pointer, and shows a
    // crosshair and a tooltip for whatever it is over.
    bool tooltip = false;
    // The name the tooltip's row goes by.
    Str name = {};
};

// Corners<Pixels>: a radius per corner, which is what `rounded_tl(..)` and its
// three siblings build. `Style::radius` is the four of them at once and stays
// what almost everything says; this is for a box whose corners differ, which
// is a control butted up against its neighbour — a NumberInput's step buttons
// round only the outer pair, to follow the frame around them.
struct Corners {
    float tl = 0;
    float tr = 0;
    float br = 0;
    float bl = 0;

    bool IsUniform() const { return tl == tr && tr == br && br == bl; }
};

// GPUI's FontWeight values. Zero remains "inherit/unset" on Style; an
// explicit Normal is distinct so a child can override a bold parent.
enum class FontWeight : uint16_t {
    Thin = 100,
    ExtraLight = 200,
    Light = 300,
    Normal = 400,
    Medium = 500,
    Semibold = 600,
    Bold = 700,
    ExtraBold = 800,
    Black = 900
};

// gpui::Anchor. Base's Popup and Positioner import this runtime vocabulary in
// Rust; keeping it here avoids each component inventing a near-copy.
enum class Anchor : uint8_t {
    TopLeft,
    TopCenter,
    TopRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    LeftCenter,
    RightCenter
};

// Dependency-neutral result used by Base Positioner and the runtime's
// post-layout element path. `placement` uses Base Placement's stable
// Top/Bottom/Left/Right ordinals and is -1 for corner positioning.
struct AnchoredPosition {
    Bounds bounds = {};
    int8_t placement = -1;
};

AnchoredPosition AnchoredSideResolve(Bounds trigger, Size popup, Size view,
                                     float margin, int preferred, int align,
                                     float offset);
AnchoredPosition AnchoredCornerResolve(Anchor anchor, Point at, Size popup,
                                       Size view, float margin);

struct Style {
    // Keep pointer-aligned members together at the front, then 32-bit values,
    // then the 16- and 8-bit tail. Style is copied into every frame element,
    // so padding here is paid throughout the UI tree.
    // Arena-owned copy of Styled::shadow(Vec<BoxShadow>). Shadows do not
    // participate in layout; they paint behind the element in declaration
    // order, as GPUI's box-shadow list does.
    const BoxShadow* shadows = nullptr;
    Str tooltip;

    float width = kAuto;
    float height = kAuto;
    // w_1_2 / w_2_3 / …: a fraction of the parent's content box, which GPUI
    // has as first-class widths. 0 = unset.
    float widthFrac = 0;
    // min_width / min_height. kAuto is CSS's `auto`, the content-based
    // automatic minimum size, and is what an element that never names one
    // gets. An explicit zero is a different thing — Rust's `min_w_0()`, the
    // idiom for "this may shrink past its content" — so the two cannot share
    // a sentinel.
    float minW = kAuto;
    float minH = kAuto;
    float maxW = 1e9f;
    float maxH = 1e9f;
    // max_width as a fraction of the parent's content box — `max_w(relative(
    // f))`, which a chat bubble caps itself to 80% of its row with. Zero is
    // unset, and `maxW` is the same limit in DIPs. kFill in `maxW` is already
    // relative(1.), so this is only needed for a fraction that is not the
    // whole line.
    float maxWFrac = 0;
    // aspect_ratio, width over height. Only an image sets it, and it sets it
    // from the decoded bitmap: gpui's `Img::request_layout` stamps the ratio
    // on the style so a clamped width carries the height with it. 0 = unset.
    float aspect = 0;
    float flexGrow = 0;
    float flexShrink = 1;
    // flex-basis, the main size a flex item starts from before grow and
    // shrink are handed the leftover. kAuto is CSS's `auto` — start from the
    // item's own width or height — and is what every element that never says
    // otherwise gets. Zero is what `flex_1()` means, and it is a different
    // instruction: siblings then split the line by their grow factors alone,
    // rather than each keeping its content's width and splitting only what
    // is left over.
    float flexBasis = kAuto;
    // flex-basis as a fraction of the line, which is `relative(f)` in Rust.
    // Zero is unset, and a basis in DIPs is the field above.
    float flexBasisFrac = 0;
    Edges pad = {};
    Edges margin = {};
    // gap, per axis. `Gap()` sets both, which is what `gap_N` does; a style
    // that names only one — `gap_x_2` — leaves the other where it was.
    float gapX = 0;
    float gapY = 0;
    float border = 0;
    float borderT = 0;
    float borderB = 0;
    float borderL = 0;
    float borderR = 0;
    float radius = 0;
    // The four corners, when they are not all `radius`. `hasCorners` below is
    // what says to read them at all.
    Corners corners = {};
    Background bg = {};
    Rgba borderColor = {};
    Rgba color = {};
    int shadowCount = 0;
    // Transformation::rotate: turns clockwise about the element's own centre,
    // where 1 is a whole one. Only an icon reads it — a rotated box would want
    // the whole element tree in on it, and nothing in the port asks for one.
    float rotate = 0;
    // Style::opacity. 1 is untouched; anything less fades this element and
    // everything under it.
    float opacity = 1;
    float fontSize = 0; // 0 = inherit
    // line_height as a multiple of the font size. 0 = GPUI's default, phi.
    float lineHeight = 0;
    // Dash on/off lengths for a dashed border, in stroke widths. GPUI's
    // border_dashed draws 2 on, 1 off; a dashed Separator paints its own path
    // with 4 on, 2 off.
    float dashOn = 2;
    float dashOff = 1;
    float anchorGap = 0;
    float anchorMargin = 4;
    // Base Positioner's standalone element path. Unlike AnchorCorner and
    // AnchorBelow, which derive the anchor from the parent El, this carries
    // the captured window-coordinate bounds/point Rust gives Positioner.
    Bounds positionerTrigger = {};
    Point positionerPoint = {};
    float absTop = kAuto, absLeft = kAuto, absBottom = kAuto, absRight = kAuto;
    // left(relative(f)) / right(relative(f)): the offset is that fraction of
    // the parent's width, added to the pixel one. A stepper's connector needs
    // it to reach from the middle of one step to the middle of the next.
    float absLeftRel = 0, absRightRel = 0;
    float absTopRel = 0, absBottomRel = 0;
    Background hoverBg = {};
    // hover(|style| style.text_color(..)): what the subtree under a hovered
    // element paints with, for the descendants that set no color of their own.
    Rgba hoverFg = {};
    // active(|style| style.bg(..)): what the box paints with while it is held
    // down. GPUI's `clicked_state` is set by the press and cleared by the
    // release, so it stays on while the pointer slides off the element.
    Background activeBg = {};
    // .group_hover("", |this| this.bg(c)): the fill while the pointer is
    // anywhere inside the group. The element's own hover and active fills win.
    Background groupHoverBg = {};
    int focusId = 0;
    // FocusHandle::tab_index groups traversal by index, then paint order.
    int tabIndex = 0;
    // div().key_context(".."): the context a keystroke is resolved against
    // while focus is anywhere in this subtree. Hashed, since that is all an
    // id is; KeyContextOf keeps the parse behind the hash.
    uint32_t keyContext = 0;
    int trapId = 0;

    // These 32 one-bit fields occupy one uint32_t allocation unit. Keep the
    // three non-zero/default-sensitive focus flags in the byte-sized tail.
    uint32_t hasAlignSelf : 1 = false;
    uint32_t hasCorners : 1 = false;
    uint32_t truncate : 1 = false;
    uint32_t wrap : 1 = false;
    // flex_wrap on a row: children that do not fit start a new line.
    uint32_t flexWrap : 1 = false;
    uint32_t hasBg : 1 = false;
    uint32_t hasColor : 1 = false;
    uint32_t fontBold : 1 = false;
    uint32_t fontSemibold : 1 = false;
    uint32_t fontMedium : 1 = false; // DWrite weight 500
    uint32_t fontMono : 1 = false;   // font_family("Consolas")
    uint32_t underline : 1 = false;  // text_decoration_1()
    uint32_t strike : 1 = false;     // ~~del~~ or HTML <s>/<del>
    uint32_t italic : 1 = false;     // *emphasis*
    uint32_t borderDashed : 1 = false;
    uint32_t absolute : 1 = false;
    // fixed is out-of-flow in window coordinates; deferred paints an
    // in-layout element after the page so popups draw and hit-test above it.
    uint32_t fixed : 1 = false;
    uint32_t deferred : 1 = false;
    // Side placement can flip; the other anchors position against the parent.
    uint32_t anchorFlip : 1 = false;
    uint32_t anchorBelow : 1 = false;
    uint32_t anchorAbove : 1 = false;
    uint32_t anchorCenterX : 1 = false;
    uint32_t anchorCorner : 1 = false;
    uint32_t explicitPositioner : 1 = false;
    uint32_t positionerCorner : 1 = false;
    uint32_t hasHoverBg : 1 = false;
    uint32_t hasHoverFg : 1 = false;
    uint32_t hasActiveBg : 1 = false;
    // A group resolves descendant group_hover styles from this element.
    uint32_t group : 1 = false;
    // Invisible until its group is hovered, while retaining its layout box.
    uint32_t groupHoverVisible : 1 = false;
    uint32_t hasGroupHoverBg : 1 = false;
    // El::PathId supplies focusId after the tree path is known.
    uint32_t focusFromPath : 1 = false;

    uint16_t fontWeight = 0;
    Display display = Display::Block;
    FlexDir dir = FlexDir::Row;
    FlexAlign align = FlexAlign::Stretch;
    // align_self, which overrides the line's align_items for this item alone
    // — `self_start()` / `self_end()`, how a chat bubble sits at one edge of
    // a column that stretches everything else. Unset is "follow the line".
    FlexAlign alignSelf = FlexAlign::Stretch;
    Justify justify = Justify::Start;
    Overflow overflowY = Overflow::Visible;
    Overflow overflowX = Overflow::Visible;
    // Rust sorts deferred elements by priority. Zero is the normal popup
    // layer; TooltipOverlay asks for the dedicated layer above it.
    uint8_t deferredLayer = 0;
    // Side placement rather than corner anchoring: the requested side when the
    // popup fits there, the opposite side when it does not, and the roomier of
    // the two when neither does — `Positioner::side`, which is what a dropdown
    // uses upstream so a menu near the bottom of the window opens upward
    // instead of being clamped against the edge.
    Anchor anchor = Anchor::TopLeft;
    // placement is Placement's Top/Bottom/Left/Right ordinal, or -1 for its
    // default Top preference; align is Positioner Align's ordinal.
    int8_t positionerPlacement = -1;
    uint8_t positionerAlign = 1;
    bool tabStop = true;
    // Whether a press on this element moves focus to it. GPUI's `track_focus`
    // does not: every widget in gpui-kit that takes focus from a click
    // calls `focus_handle.focus(window, cx)` itself — the input, the otp
    // field, the tree, the list, the select and the colour picker do, and the
    // button, the checkbox, the radio, the switch and the link do not. A
    // press used to focus anything with a handle here, which is why a clicked
    // button kept a focus ring the Rust one never shows.
    bool focusOnPress = false;
    // Whether this focused element asked for the component focus appearance.
    // FocusId / TrackFocus only route keyboard input; Rust draws no generic
    // outline for them. A themed control calls focus_ring_style explicitly,
    // which is El::FocusRing(true) here. Turning it off drops the tinted
    // border along with the outside ring.
    bool focusRing = false;
};

static_assert(sizeof(Style) <= 408, "keep Style members packed by alignment");

// One `on_action` handler. The tree is frame-arena, so a handful of these
// chained off an element costs a pointer each and dies with the frame.
struct ActionSlot {
    uint32_t action = 0;
    Listener fn = {};
    ActionSlot* next = nullptr;
};

// What an action handler is called with. Rust hands over the action itself,
// which carries whatever fields it was declared with; an action here is a
// name, so this is the name and the one thing a handler answers back.
struct ActionEvent {
    uint32_t action = 0;
    // What the action carries. Rust puts fields on the action type —
    // `Confirm { secondary }`, `SelectScrollbarMode(mode)`,
    // `MenuClick(name)` — and matches the whole value; an action here is the
    // hash of its name, and this is the rest of it. A number, a bool or an
    // enum is itself; anything larger is a pointer to something that outlives
    // the dispatch, which for a binding means a literal.
    intptr_t arg = 0;
    // cx.propagate(): the handler looked and did not want it, so the search
    // carries on outwards. Not setting it is Rust's default, which stops.
    bool propagate = false;
};

// text/state.rs SelectionFormat: what a copy of the window's selection says.
// Plain is the rendered text, which is all a run knows by itself. Source
// rebuilds the Markdown the run was rendered from, out of the affixes below.
enum class SelectionFormat : uint8_t {
    Plain = 0,
    Source
};

// The block a selectable run belongs to, for SelectionFormat::Source. Rust
// reconstructs a selection by walking the BlockNode tree it rendered from
// (node.rs `text_by_kind`); the window's selection here knows only the flat
// list of painted runs, so the tree's shape rides along on them. One record
// is shared by every run of a block, and `!=` is what says the selection has
// crossed into another one.
struct SelBlock {
    // Emitted when the selection first enters the block and when it leaves —
    // a code fence, a heading's `## `, a table row's leading `| `. `pre`
    // already carries `linePre`, so entering a block emits `pre` alone.
    Str pre;
    Str post;
    // Prefixed to every further line the block contributes, and to every
    // line break inside one of its runs: `> ` for a blockquote, the indent
    // under a list marker.
    Str linePre;
    // Whether entering this block continues the previous block's line rather
    // than starting a new one. Table cells in a row do.
    bool join = false;
};

// What SelectionFormat::Source needs from one selectable run: the marks
// around it, and the block it sits in.
//
// One record is shared by every adjacent run that carries the same marks, and
// the copier closes the group only when the record changes. That is what
// `reconstruct_markdown` does by walking mark *ranges* rather than words: a
// bold phrase split into three word elements has to copy as `**one two
// three**`, not as three wrapped words.
struct SelSource {
    // node.rs `wrap_with_mark`: the Markdown around the run's own text, in
    // the order that function nests it — code innermost, link outermost. A
    // partial selection still gets both halves, which is what Rust does with
    // a slice of a marked range.
    Str pre;
    Str post;
    // The block above. Null for a selectable run that is not Markdown, which
    // is every run outside a TextView.
    const SelBlock* block = nullptr;
};

struct FocusHandle {
    int id = 0;
    bool IsValid() const { return id != 0; }
    bool operator==(const FocusHandle& o) const { return id == o.id; }
    bool operator!=(const FocusHandle& o) const { return id != o.id; }
};

// The AccessKit roles used by crates/base and crates/ui. GPUI's element tree
// records this information during build and exports one accessibility tree
// after layout, alongside the hit and focus trees.
enum class AccessibilityRole : uint8_t {
    None,
    Unknown,
    TextRun,
    Cell,
    Label,
    Image,
    Link,
    Row,
    ListItem,
    ListMarker,
    TreeItem,
    ListBoxOption,
    MenuItem,
    MenuListOption,
    Paragraph,
    CheckBox,
    RadioButton,
    TextInput,
    Button,
    DefaultButton,
    Pane,
    RowHeader,
    ColumnHeader,
    RowGroup,
    List,
    Table,
    LayoutTableCell,
    LayoutTableRow,
    LayoutTable,
    Switch,
    Menu,
    MultilineTextInput,
    SearchInput,
    DateInput,
    DateTimeInput,
    WeekInput,
    MonthInput,
    TimeInput,
    EmailInput,
    NumberInput,
    PasswordInput,
    PhoneNumberInput,
    UrlInput,
    Abbr,
    Alert,
    AlertDialog,
    Application,
    Article,
    Audio,
    Banner,
    Blockquote,
    Canvas,
    Caption,
    Caret,
    Code,
    ColorWell,
    ComboBox,
    EditableComboBox,
    Complementary,
    Comment,
    ContentDeletion,
    ContentInsertion,
    ContentInfo,
    Definition,
    DescriptionList,
    Details,
    Dialog,
    DisclosureTriangle,
    Document,
    EmbeddedObject,
    Emphasis,
    Feed,
    FigureCaption,
    Figure,
    Footer,
    Form,
    Grid,
    GridCell,
    Group,
    Header,
    Heading,
    Iframe,
    IframePresentational,
    ImeCandidate,
    Keyboard,
    Legend,
    LineBreak,
    ListBox,
    Log,
    Main,
    Mark,
    Marquee,
    Math,
    MenuBar,
    MenuItemCheckBox,
    MenuItemRadio,
    MenuListPopup,
    Meter,
    Navigation,
    Note,
    PluginObject,
    ProgressIndicator,
    RadioGroup,
    Region,
    RootWebArea,
    Ruby,
    RubyAnnotation,
    ScrollBar,
    ScrollView,
    Search,
    Section,
    SectionFooter,
    SectionHeader,
    Slider,
    SpinButton,
    Splitter,
    Status,
    Strong,
    Suggestion,
    SvgRoot,
    Tab,
    TabList,
    TabPanel,
    Term,
    Time,
    Timer,
    TitleBar,
    Toolbar,
    Tooltip,
    Tree,
    TreeGrid,
    Video,
    WebView,
    Window,
    PdfActionableHighlight,
    PdfRoot,
    GraphicsDocument,
    GraphicsObject,
    GraphicsSymbol,
    DocAbstract,
    DocAcknowledgements,
    DocAfterword,
    DocAppendix,
    DocBackLink,
    DocBiblioEntry,
    DocBibliography,
    DocBiblioRef,
    DocChapter,
    DocColophon,
    DocConclusion,
    DocCover,
    DocCredit,
    DocCredits,
    DocDedication,
    DocEndnote,
    DocEndnotes,
    DocEpigraph,
    DocEpilogue,
    DocErrata,
    DocExample,
    DocFootnote,
    DocForeword,
    DocGlossary,
    DocGlossRef,
    DocIndex,
    DocIntroduction,
    DocNoteRef,
    DocNotice,
    DocPageBreak,
    DocPageFooter,
    DocPageHeader,
    DocPageList,
    DocPart,
    DocPreface,
    DocPrologue,
    DocPullquote,
    DocQna,
    DocSubtitle,
    DocTip,
    DocToc,
    ListGrid,
    Terminal
};

enum class AccessibilityToggled : uint8_t {
    Unset,
    False,
    True,
    Mixed
};

enum class AccessibilityOrientation : uint8_t {
    Unset,
    Horizontal,
    Vertical
};

enum AccessibilityActionBits : uint8_t {
    AccessibilityActionNone = 0,
    AccessibilityActionDefault = 1 << 0,
    AccessibilityActionFocus = 1 << 1,
    AccessibilityActionIncrement = 1 << 2,
    AccessibilityActionDecrement = 1 << 3,
    AccessibilityActionSetValue = 1 << 4
};

enum class AccessibilityAction : uint8_t {
    Default,
    Focus,
    Increment,
    Decrement,
    SetValue
};

struct AccessibilityInfo {
    AccessibilityRole role = AccessibilityRole::None;
    // GPUI's `accessibility_id`: a developer-assigned identifier for test
    // and automation clients, separate from the element id used by layout
    // and hit testing.
    Str authorId = {};
    Str label = {};
    Str value = {};
    Str placeholder = {};
    AccessibilityToggled toggled = AccessibilityToggled::Unset;
    AccessibilityOrientation orientation = AccessibilityOrientation::Unset;
    float numericValue = 0;
    float minNumericValue = 0;
    float maxNumericValue = 0;
    float numericValueStep = 0;
    int positionInSet = 0;
    int sizeOfSet = 0;
    int rowCount = 0;
    int columnCount = 0;
    int rowIndex = 0;
    int columnIndex = 0;
    int level = 0;
    // These one-bit fields occupy one unsigned int allocation unit.
    unsigned int hasNumericValue : 1 = false;
    unsigned int hasMinNumericValue : 1 = false;
    unsigned int hasMaxNumericValue : 1 = false;
    unsigned int hasNumericValueStep : 1 = false;
    unsigned int hasPositionInSet : 1 = false;
    unsigned int hasSizeOfSet : 1 = false;
    unsigned int hasRowCount : 1 = false;
    unsigned int hasColumnCount : 1 = false;
    unsigned int hasRowIndex : 1 = false;
    unsigned int hasColumnIndex : 1 = false;
    unsigned int hasLevel : 1 = false;
    unsigned int selected : 1 = false;
    unsigned int hasSelected : 1 = false;
    unsigned int expanded : 1 = false;
    unsigned int hasExpanded : 1 = false;
    unsigned int activeDescendant : 1 = false;
    unsigned int disabled : 1 = false;
};

static_assert(sizeof(AccessibilityInfo) <= 128,
              "keep AccessibilityInfo boolean state packed");

// Interactive refinements are absent from most elements. Keeping five full
// Styles inline cost 2,320 bytes on every El, including plain Markdown text
// and layout-only wrappers. The frame arena owns this rare sidecar and the El
// names it with a four-byte offset. Its drag kind uses the same arena-relative
// representation instead of paying for a pointer plus length beside it.
struct ElStyleStates {
    Style refine = {};
    Style hover = {};
    Style active = {};
    Style focus = {};
    Style dragOver = {};
    uint32_t refineSet = 0;
    uint32_t hoverSet = 0;
    uint32_t activeSet = 0;
    uint32_t focusSet = 0;
    uint32_t dragOverSet = 0;
    ArenaStr dragOverKind = kArenaStrNone;
};

struct El {
    // Members are ordered by decreasing alignment. El is allocated many
    // times in the frame arena, so even small holes here multiply quickly.
    // Keep pointer-sized members first, four-byte members next, and byte-sized
    // enums last. Boolean state shares the unsigned-int allocation unit near
    // the end of the four-byte group.

    // The frame arena this was built on, so a builder that has to allocate —
    // an action handler's slot — has one without being handed it again.
    Arena* arena = nullptr;
    Style style;
    Str id;
    Str text;
    Str iconPath;
    AccessibilityInfo accessibility = {};
    // ElKind::Image: resource, decoded render image, encoded Image bytes, or
    // a custom loader, matching gpui::ImageSource.
    ImageSource imageSource;
    Func0 onClick;

    // Keep every entity Listener together. El is copied and walked as
    // frame-arena data, and this makes its dispatch footprint contiguous.
    Listener listener;
    // Semantic actions that do not need a pointer hit box. These listeners
    // receive a keyboard-shaped ClickEvent, just like a button activated by
    // Enter or Space.
    Listener accessibilityDefault;
    Listener accessibilityIncrement;
    Listener accessibilityDecrement;
    // `div().on_hover(..)`. Fires with a HoverEvent when the pointer enters
    // the element and again when it leaves, never in between.
    Listener onHover;
    // on_mouse_move: every move over this element, without requiring a press.
    Listener onMouseMove;
    Listener onScroll;
    // on_scroll_wheel: the wheel or trackpad gesture itself, offered to this
    // element before the scrolled box under the pointer takes it. GPUI's
    // `InteractiveElement::on_scroll_wheel`; `El::OnScroll` above is the
    // scrolled box's own offset, which is a different question.
    Listener onScrollWheel;
    // window.on_mouse_event::<MouseDownEvent> bound to one element, which is
    // what `div().on_mouse_down(..)` is. A press runs this before the click
    // listener above; unlike the click, it carries the full MouseDownEvent.
    Listener onMouseDown;
    Listener onMouseUp;
    // on_drag_move. GPUI carries a drag entity so the move can name what is
    // being dragged; here the element that took the press keeps the moves
    // until the button comes back up, which is the same thing without the
    // entity. Needs a Click(id): the tree is rebuilt every frame, so the id
    // is what finds the element again.
    Listener onDragMove;
    // on_mouse_down_out: a press anywhere outside this element. Unlike a
    // window subscription this is per element, so several open overlays can
    // independently observe the same press, just as GPUI's interactive
    // element handler does.
    Listener onMouseDownOut;
    // on_mouse_up_out: a release that landed anywhere but on this element.
    // Rust hears it wherever the pointer is, whether or not the press started
    // here, and so does this.
    Listener onMouseUpOut;
    // on_drop::<T>(..): called when a drag of dropKind lands on this element.
    Listener onDrop;
    Listener onLineClamp;

    // The same semantic operations for a frame-local, non-entity callback.
    // Composed controls use this when the behavior belongs to their retained
    // InputState rather than to the view that happened to render them.
    Func0 accessibilityIncrementDirect;
    Func0 accessibilityDecrementDirect;
    intptr_t clickActionArg = 0;
    ActionSlot* actions = nullptr;
    // `div().hover(|this| ..)` and `div().drag_over::<T>(|this, ..| ..)`:
    // refinements that hold only while the pointer is over the box, or while
    // a drag of `dragOverKind` is. Resolved where GPUI resolves them — in
    // `compute_style` during prepaint, against the hover the last frame left
    // — so unlike `HoverBg` they can name any field a refinement can, and a
    // caller no longer has to ask the window whether it is hovered and branch
    // on the answer while building.
    // StatefulInteractiveElement refinements are resolved after element ids
    // have been collected. See ElStyleStates above.
    // on_drag: what a press on this element picks up. The payload rides along
    // on every DragMoveEvent the drag produces.
    DragPayload drag = {};
    // div().cursor_col_resize() and friends: `cursor` below is the shape the
    // pointer takes over this element. Rust hangs it off the style; it needs a
    // Click(id) here for the same reason a hover does, since the hit rect is
    // what the move consults.
    // on_drop::<T>(..) and drag_over::<T>(..): the kind of drag this element
    // takes, and what to do when one lets go over it. `WindowDragOverId` says
    // which element the drag is over right now, which is what a caller styles
    // on — GPUI applies `drag_over` itself because the style is part of the
    // element; an element here is rebuilt every frame and reads the answer
    // back instead.
    Str dropKind = {};
    // Where this element ended up, written at paint. GPUI's DockArea keeps
    // its own `bounds` the same way, through an element whose only job is to
    // report the box layout gave it; a caller that has to answer "what is
    // under the pointer" needs last frame's boxes to do it.
    gpui::Bounds* boundsOut = nullptr;
    // BindSlider: this element is a slider's track, and a press or a drag on
    // it moves that state. GPUI's slider elements capture the state entity in
    // their own closures; there are no closures on an element here, so the
    // element names the state and the window does what those closures do.
    SliderState* slider = nullptr;
    // BindInput: this element is a text field's editor box, so a press in it
    // places the caret and a drag extends the selection. The same trick as
    // BindSlider — Rust's InputElement captures the state entity in the
    // closures it installs, and an element here names the state instead.
    InputState* input = nullptr;
    // BindSliderBounds: this element is the rail a value maps against, and
    // hands its box to the state once it has one — SliderIndicator's
    // `on_prepaint(|bounds| state.set_bounds(bounds))`. The box that catches
    // the press is the taller track, so the two are not the same element.
    SliderState* sliderBounds = nullptr;
    void (*customPaint)(PaintCtx* ctx, El* e, void* user) = nullptr;
    void* customUser = nullptr;
    El* first = nullptr;
    El* last = nullptr;
    El* next = nullptr;
    // StyledImage::with_loading / with_fallback. Both are frame elements;
    // imageReplacement names the one selected during PrepareEl.
    El* imageLoading = nullptr;
    El* imageFallback = nullptr;
    El* imageReplacement = nullptr;
    // The highlighted runs inside this text, in order. The array is the
    // caller's — the frame arena, in practice — and outlives the frame the
    // element was built in.
    const TextSpan* spans = nullptr;
    // Washes under this run, painted where the selection quad is and before
    // the glyphs — which is what Rust's `layout_search_matches` builds paths
    // for. They are a second array rather than more `spans` because the span
    // painter partitions the text and so cannot take two runs over the same
    // bytes, and a search match sits over whatever the highlighter said.
    // Only `lo`, `hi` and `bg` are read.
    const TextSpan* washes = nullptr;
    // Runs that are underlined and nothing else: a diagnostic's squiggle is a
    // HighlightStyle with only `underline` set, so it marks the text without
    // taking the colour the language gave it.
    const TextSpan* underlines = nullptr;
    gpui::Bounds* rangeOut = nullptr;
    float* caretOutX = nullptr;
    float* caretOutY = nullptr;
    // What a copy of this run says, and whether it continues the run before
    // it on the same line. The record is owned by whoever built the element —
    // the frame arena, in practice — and is null on every run that is not
    // Markdown.
    const SelSource* selSrc = nullptr;
    // The taffy node this element was laid out as, this frame. A
    // `taffy::NodeId` is a u64 and is kept as one here so gpui.h does not
    // have to name the layout port's types.
    uint64_t layoutNode = 0;
    // The shaped run LayoutEl measured, borrowed from the text cache so the
    // paint pass can draw it without looking it up a second time. Owned by
    // the cache, which cannot drop it before the frame ends; null when the
    // element has no text or the run could not be cached.
    TextLayout* laidLayout = nullptr;

    // Four-byte-aligned state begins here.
    // Only Chart elements carry this 192-byte payload. All other elements
    // keep one four-byte arena offset instead.
    ArenaPtr<ChartSeries> chart = {};
    float progress = 0; // 0..100
    int clickId = 0;
    // GlobalElementId. GPUI identifies an element by the *stack* of
    // ElementIds from the root down to it. This port folds that path into one
    // flat hash, filled by IdCollect once per frame.
    uint32_t pathId = 0;
    // El::OnClickAction — dispatched from the release, beside onClick.
    uint32_t clickAction = 0;
    // Interactive refinements are held in the arena sidecar above.
    ArenaPtr<ElStyleStates> styleStates = {};
    float lineSpanHeight = 0;
    float lineClampCap = 0;

    float x = 0, y = 0, w = 0, h = 0;
    // The laid-out box as one value. The fields stay flat because the layout
    // pass writes them a component at a time. The return type is qualified
    // because this member hides `Bounds` inside El.
    gpui::Bounds Bounds() const { return {x, y, w, h}; }
    float scrollY = 0;
    // overflow_x_scroll: how far the content is slid to the left. Positive
    // means the view has moved right over it, as scrollY is positive-down.
    float scrollX = 0;

    // Base ScrollbarTheme projected by crates/ui. Plain gpui scroll boxes
    // leave this unset and use the runtime palette fallback; a Base scrollbar
    // carries its own layer-owned motion and paint styles.
    ScrollbarMotion scrollMotion = {};
    Background scrollTrack = {};
    Background scrollTrackHover = {};
    Background scrollTrackActive = {};
    Background scrollThumb = {};
    Background scrollThumbHover = {};
    Background scrollThumbActive = {};
    Rgba scrollTrackBorder = {};
    Rgba scrollTrackHoverBorder = {};
    Rgba scrollTrackActiveBorder = {};
    float scrollTrackWidth = 16;
    float scrollThumbWidth = 6;
    float scrollThumbHoverWidth = 8;
    float scrollThumbActiveWidth = 8;
    float scrollThumbInset = 4;
    float scrollThumbHoverInset = 4;
    float scrollThumbActiveInset = 4;
    float scrollThumbRadius = 0;
    float scrollThumbHoverRadius = 0;
    float scrollThumbActiveRadius = 0;
    float scrollThumbMinLength = 48;
    float scrollThumbHoverMinLength = 48;
    float scrollThumbActiveMinLength = 48;
    int scrollId = 0;
    float contentW = 0;
    float contentH = 0;
    int selLo = -1; // UTF-8 offsets into text, -1 = none
    int selHi = -1;
    int nSpans = 0;
    int nWashes = 0;
    int nUnderlines = 0;
    // RangeOut: where a run of this text landed, in window coordinates, for
    // a caller that has to hit-test against it later.
    int rangeOutLo = -1;
    int rangeOutHi = -1;
    Rgba selColor{Rgba8(0x6b, 0xb3, 0xf0, 90)};
    // The input method's provisional run, underlined the way Rust gives the
    // marked range its own UnderlineStyle. Same offsets, same -1 for none.
    int markLo = -1;
    int markHi = -1;
    // ui/text's retained TextViewState, used to scope selection queries.
    EntityId selectionOwner = {};
    // The caret this run draws, as a UTF-8 offset into it; -1 for none.
    int caretOff = -1;
    Rgba caretColor = {};
    float caretW = 2;
    float laidFont = 0; // resolved font size from last LayoutEl
    float laidMaxW = 0; // MeasureText maxW used (0 = unconstrained)
    // What the measure callback last answered for this text leaf, keyed on
    // the width it was asked about. Taffy asks a leaf for its size several
    // times a pass — the min-content width, the max-content width, then the
    // width the line settled on — and each of those went all the way into the
    // shaped-run cache, which hashes the whole string and then memcmps it.
    // Font size, weight, wrap and line height are settled by PrepareEl before
    // layout starts and the element is built afresh every frame, so the width
    // is the whole key and nothing here outlives the text it measured.
    float measKeyW[4] = {};
    Size measSize[4] = {};

    // These one-bit fields occupy one unsigned int allocation unit.
    // El::PathId: the click id is the path rather than a number the caller
    // picked. An explicit Click(v) clears it and wins.
    unsigned int clickFromPath : 1 = false;
    // The same rule for the scroll handle and an explicit ScrollId(v).
    unsigned int scrollFromPath : 1 = false;
    // El::StopClick — see HitRect::stopClick.
    unsigned int stopClick : 1 = false;
    // cx.stop_propagation() on the left-button mouse-down bubble.
    unsigned int stopMouseDown : 1 = false;
    // The press belongs to a control with selection behavior of its own.
    unsigned int suppressTextSelection : 1 = false;
    // A descendant Inline reports its laid-out vertical extent to the nearest
    // line-clamped ancestor.
    unsigned int lineSpan : 1 = false;
    // TextView::max_lines: lineClampCap is already in DIPs.
    unsigned int lineClamp : 1 = false;
    // Scrollbar configuration presence and visibility.
    unsigned int scrollModeSet : 1 = false;
    unsigned int scrollThemeSet : 1 = false;
    unsigned int noScrollbar : 1 = false;
    unsigned int noScrollbarX : 1 = false;
    unsigned int noScrollbarY : 1 = false;
    // Text selection/caret state.
    unsigned int selectable : 1 = false;
    unsigned int selJoin : 1 = false;
    unsigned int caretLineEndAffinity : 1 = false;
    // StyledImage::grayscale.
    unsigned int imageGrayscale : 1 = false;

    // Byte-sized state stays last so none of it creates alignment holes.
    IconName icon = IconName::None;
    ElKind kind = ElKind::Div;
    CursorKind cursor = CursorKind::Arrow;
    DispatchPhase mouseDownPhase = DispatchPhase::Bubble;
    DispatchPhase mouseUpPhase = DispatchPhase::Bubble;
    Axis sliderAxis = Axis::Horizontal;
    // ImageStyle defaults to Contain in Rust.
    ObjectFit objectFit = ObjectFit::Contain;
    ImageLoadState imageLoadState = ImageLoadState::Loading;
    // The bar this box shows. Unnamed, it is the theme's mode.
    ScrollbarMode scrollMode = ScrollbarMode::Always;
    // ScrollableMask axes: horizontal is bit 0, vertical bit 1.
    uint8_t scrollMaskAxes = 0;
    uint8_t measCount = 0;
    uint8_t measNext = 0;

    // display: flex, leaving the direction at its row default — gpui's
    // `.flex()`, for a box that wants the flex model without saying which way
    // it runs.
    El* Flex();
    El* FlexRow();
    El* FlexCol();
    El* FlexRowReverse();
    El* FlexColReverse();
    El* FlexWrap();
    El* Grow(float g = 1);
    El* Shrink0();
    // flex_1(): grow 1, shrink 1, basis 0. The three together are what makes
    // siblings share a line evenly whatever is inside them, and grow alone
    // does not — with an auto basis each item keeps its content's width and
    // only the slack is split.
    El* Flex1();
    // flex_none(): neither grows nor shrinks, and keeps its own size.
    El* FlexNone();
    El* Basis(float v);
    // flex_basis(relative(f)): the main size a flex item starts from, as a
    // fraction of the line rather than a length. Siblings that all start
    // from the whole line and shrink together end up sharing it in the
    // proportions of their fractions, which is how a table row's cells are
    // sized.
    El* BasisFrac(float f);
    // flex_shrink(f). Shrink0() is the common case; this is the factor.
    El* Shrink(float f);
    El* W(float v);
    El* WFrac(float f);
    // percentage(delta) turns clockwise, which is what a spinner is made of.
    El* Rotate(float turns);
    // Scroll without a bar: the box still takes the wheel and still clips.
    El* HideScrollbar();
    // The bar down one axis only, for a box that scrolls both ways —
    // ScrollbarAxis::Vertical hides the horizontal one and the other way
    // round.
    El* HideScrollbarX();
    El* HideScrollbarY();
    // ScrollableMask::new(axis, handle), collapsed onto the element whose
    // handle the mask controls. Calling it for both axes accumulates bits.
    El* ScrollMask(Axis axis);
    // opacity(f): this element and everything under it, faded together.
    // Nested opacities multiply, as GPUI's do.
    El* Opacity(float f);
    El* Grayscale(bool grayscale = true);
    El* ObjectFitMode(gpui::ObjectFit fit);
    El* WithLoading(El* loading);
    El* WithFallback(El* fallback);
    El* H(float v);
    El* SizeFull();
    El* MinH(float v);
    El* MinW(float v);
    El* MaxW(float v);
    // max_w(relative(f)).
    El* MaxWFrac(float f);
    // aspect_ratio(w / h). An image stamps its own; this is the explicit one
    // — `aspect_ratio(1.)` on a square media preview.
    El* Aspect(float ratio);
    El* MaxH(float v);
    El* Gap(float v);
    El* GapX(float v);
    El* GapY(float v);
    El* Pad(float v);
    El* PadX(float v);
    El* PadY(float v);
    El* PadL(float v);
    El* PadR(float v);
    El* PadT(float v);
    El* PadB(float v);
    El* Margin(float v);
    El* MarginX(float v);
    El* MarginY(float v);
    El* MarginL(float v);
    El* MarginR(float v);
    El* MarginT(float v);
    El* MarginB(float v);
    El* ItemsCenter();
    El* ItemsStart();
    El* ItemsEnd();
    El* ItemsStretch();
    // align_self on this item alone.
    El* SelfStart();
    El* SelfEnd();
    El* SelfCenter();
    El* JustifyBetween();
    El* JustifyAround();
    El* JustifyCenter();
    El* JustifyEnd();
    El* JustifyStart();
    El* Bg(Background c);
    El* Border(float width, Rgba c);
    El* BorderT(float width, Rgba c);
    El* BorderB(float width, Rgba c);
    El* BorderL(float width, Rgba c);
    El* BorderR(float width, Rgba c);
    El* Shadows(const BoxShadow* values, int count);
    El* Radius(float r);
    // rounded_tl / rounded_tr / rounded_br / rounded_bl, as one call. A corner
    // left at 0 is square, which is what `rounded_none` does to all four.
    El* Corners(float tl, float tr, float br, float bl);
    El* Fg(Rgba c);
    El* Font(float px);
    El* LineHeight(float mult);
    El* Truncate();
    El* ClipY();
    El* ScrollY(float off);
    El* ScrollX(float off);
    El* ClipX();
    El* ScrollMode(ScrollbarMode m);
    El* ScrollId(int v);
    El* Click(int v);
    El* Role(AccessibilityRole role);
    El* AccessibilityId(Str authorId);
    El* AriaLabel(Str label);
    El* AriaValue(Str value);
    El* AriaPlaceholder(Str placeholder);
    El* AriaDisabled(bool disabled = true);
    El* AriaToggled(AccessibilityToggled toggled);
    El* AriaSelected(bool selected);
    El* AriaExpanded(bool expanded);
    El* AriaActiveDescendant(bool active = true);
    El* AriaNumericValue(float value);
    El* AriaMinNumericValue(float value);
    El* AriaMaxNumericValue(float value);
    El* AriaNumericValueStep(float value);
    El* AriaOrientation(AccessibilityOrientation orientation);
    El* AriaPositionInSet(int position);
    El* AriaSizeOfSet(int size);
    El* AriaRowCount(int count);
    El* AriaColumnCount(int count);
    El* AriaRowIndex(int index);
    El* AriaColumnIndex(int index);
    El* AriaLevel(int level);
    El* OnAccessibilityDefault(Listener fn);
    El* OnAccessibilityIncrement(Listener fn);
    El* OnAccessibilityDecrement(Listener fn);
    El* OnAccessibilityIncrement(Func0 fn);
    El* OnAccessibilityDecrement(Func0 fn);
    // `div().id(name)` — the whole of it. The element is named, and the id it
    // is found by is that name folded with its ancestors'. This is what a
    // widget should reach for: two `Button::New(cx, StrL("save"))` under
    // different parents are two different elements, the way Rust's two
    // `Button::new("save")` are, and neither caller has to invent a name the
    // other will not also pick.
    El* PathId(Str name);
    // The same without joining the tab order, for a box that is a hit target
    // and nothing else.
    El* PathClick(Str name);
    // The name, and the focus id from the fold — for an element the keyboard
    // reaches without the pointer being able to press it.
    El* PathFocus(Str name);
    // `ScrollHandle::new()` kept on the view: what scrolls is identified by
    // which box it is, not by a name. The box already has a place in the
    // tree, so this takes the scroll id from the fold and leaves the name
    // alone — an element can be named for one thing and scroll as another.
    El* ScrollFromPath();
    El* OnClick(Func0 fn);
    El* OnClick(Listener l);
    // The scrollbar's own handler. Rust's scrollbar writes straight into the
    // shared ScrollHandle; here the view owns the offset, so dragging the
    // thumb or pressing the track reports one for it to store.
    El* OnScroll(Listener l);
    El* OnHover(Listener l);
    El* OnMouseMove(Listener l);
    El* OnMouseDown(Listener l, DispatchPhase phase = DispatchPhase::Bubble);
    El* OnMouseUp(Listener l, DispatchPhase phase = DispatchPhase::Bubble);
    El* OnDragMove(Listener l);
    El* OnDrag(Str dragKind, int ix = 0, void* data = nullptr);
    El* OnMouseDownOut(Listener l);
    El* OnMouseUpOut(Listener l);
    El* StopMouseDown();
    // cx.stop_propagation() for the click this element takes.
    El* StopClick();
    El* SuppressTextSelection();
    El* OnDrop(Str acceptKind, Listener l);
    // StyleRefinement, applied at layout time rather than as the caller
    // chains: a semantic state — selected, disabled — is meant to win over the
    // instance style underneath it, and the instance style is whatever the
    // caller chains onto the element after the primitive handed it back.
    El* Refine(const Style& s, uint32_t fields);
    // `div().hover(..)`. The refinement is a StateStyle, which is what every
    // other refinement in this tree is built with.
    El* Hover(const struct StateStyle& s);
    El* Active(const struct StateStyle& s);
    El* Focus(const struct StateStyle& s);
    // `div().drag_over::<T>(..)`, where `kind` is the drag payload's kind the
    // way `OnDrop` names it.
    El* DragOver(Str dragKind, const struct StateStyle& s);
    ElStyleStates* StyleStates();
    const ElStyleStates* StyleStates() const;
    ElStyleStates* EnsureStyleStates();
    ChartSeries* Chart();
    const ChartSeries* Chart() const;
    El* BoundsOut(gpui::Bounds* out);
    El* ReportLineSpan(float lineHeight);
    El* LineClamp(float cap, Listener onChange = {});
    El* Cursor(CursorKind c);
    El* BindSlider(SliderState* s, Axis axis = Axis::Horizontal);
    El* BindSliderBounds(SliderState* s);
    El* BindInput(InputState* s);
    // The selection quad and the caret an input's text run paints over itself.
    El* SelRange(int lo, int hi, Rgba color);
    // Where the caret this element draws ended up, in window coordinates —
    // the seam a popover anchored to the caret needs, since only the painter
    // knows where inside a shaped run an offset falls. Reported the way
    // BoundsOut reports a box: one frame stale, which is what every other
    // popover here is placed against.
    El* CaretOut(float* outX, float* outY);
    // Report where the bytes [lo, hi) of this run were painted.
    El* RangeOut(int lo, int hi, gpui::Bounds* out);
    El* Washes(const TextSpan* runs, int n);
    El* Underlines(const TextSpan* runs, int n);
    El* Spans(const TextSpan* runs, int n);
    // The marked range, which is drawn underlined in the text's own colour.
    El* MarkRange(int lo, int hi);
    El* Caret(int off, Rgba color, float width = 2,
              bool lineEndAffinity = false);
    El* Child(El* c);
    El* Bold();
    El* Semibold();
    El* Medium();
    El* Weight(FontWeight value);
    El* Mono();
    El* Underline();
    El* Strikethrough();
    El* Italic();
    El* Selectable();
    El* SelectionOwner(EntityId owner);
    // The Markdown this run came from, and whether it continues the run
    // before it rather than starting a line of its own.
    El* SelSrc(const SelSource* s, bool join);
    El* Wrap();
    El* Dashed();
    El* DashArray(float on, float off);
    El* Absolute();
    El* Fixed();
    El* Deferred();
    El* DeferredLayer(int layer);
    El* AnchorBelow(float gap = 0);
    // `Positioner::side`: flip to the other side rather than clamping when
    // the anchored side has no room. Dropdowns say this; a popup placed at a
    // named corner does not.
    El* AnchorFlip(bool on = true);
    El* AnchorAbove(float gap = 0);
    El* AnchorCenterX();
    El* AnchorCorner(Anchor anchor, float margin = 4, float offsetY = 0);
    El* Top(float v);
    El* Left(float v);
    El* Bottom(float v);
    El* Right(float v);
    El* LeftRel(float frac);
    El* RightRel(float frac);
    El* TopRel(float frac);
    El* BottomRel(float frac);
    El* HoverBg(Background c);
    El* HoverFg(Rgba c);
    // .active(|style| style.bg(..)): the fill while the box is held down. It
    // wins over the hover fill, the way Rust refines the active style over
    // the hovered one.
    El* ActiveBg(Background c);
    // div().group("") and .group_hover(..): the group, and a descendant that
    // only paints while the pointer is inside it.
    // The press-focus opt-in above.
    El* FocusOnPress(bool v = true);
    El* Group();
    El* GroupHoverVisible();
    // The other half of `group_hover`: a fill rather than a visibility, for a
    // strip that lights when the pointer is in the box around it.
    El* GroupHoverBg(Background c);
    El* FocusId(int v);
    // `div().track_focus(&handle)`. The element is focusable, and what it is
    // focusable *as* is the handle the caller's state owns rather than
    // anything derived from the element's name.
    El* TrackFocus(FocusHandle handle);
    El* KeyContext(Str name);
    // on_action::<A>(..). The listener is called with an ActionEvent; setting
    // its `propagate` passes the action on outwards, which is cx.propagate().
    El* OnAction(uint32_t action, Listener fn);
    // `.on_click(|_, window, cx| window.dispatch_action(Box::new(Cancel), cx))`
    // — the click runs whatever the keyboard's binding for that action runs,
    // rather than the caller passing the same handler to both. The dispatch
    // starts at the focused element, not at this one, which is what makes a
    // dialog's Cancel button and its escape key one handler.
    El* OnClickAction(uint32_t action, intptr_t arg = 0);
    // div().on_key_down(..): the raw keystroke, offered to the focused element
    // and then out through the elements above it, before the keymap resolves
    // the chord to an action. It is what a field that is not a text editor
    // reads — an OTP input takes a digit and a backspace and nothing else, so
    // there is no action to bind and no `InputState` to hand the window. Both
    // halves of a keystroke arrive here: the key itself, and the character it
    // produced, with `ch` set and `vk` zero.
    El* OnKeyDown(Listener fn);
    // div().on_key_up(..): the release, on the same focus path the press
    // walked. GPUI's `InteractiveElement::on_key_up`; the KeyEvent it carries
    // has `down` false and no character half.
    El* OnKeyUp(Listener fn);
    // div().on_scroll_wheel(..): the gesture, before the scrolled box under
    // the pointer takes it. A handler that leaves `propagate` set lets the
    // scroll carry on, which is `cx.propagate()`.
    El* OnScrollWheel(Listener fn);
    El* TabIndex(int v);
    El* TabStop(bool v);
    // Opt into the component focus appearance. FocusId / TrackFocus alone do
    // not paint anything, matching GPUI's separation between focus routing
    // and the UI layer's explicit focus_ring_style.
    El* FocusRing(bool v = true);
    El* TrapId(int v);
    El* Tip(Str s);
    El* Id(Str s);
};

static_assert(sizeof(unsigned int) == 4,
              "El flags require a four-byte unsigned int");
static_assert(sizeof(El) <= 1800,
              "keep El flags packed and members alignment-ordered");

enum class BtnKind : uint8_t {
    Default,
    Primary,
    Outline
};

El* ButtonEl(Arena* a, int clickId, Str label, BtnKind kind = BtnKind::Default);
El* ButtonSmall(Arena* a, int clickId, Str label, BtnKind kind, bool selected);

El* Div(Arena* a);
El* TextEl(Arena* a, Str s);
El* IconEl(Arena* a, IconName name);
El* IconEl(Arena* a, IconName name, float size);
// gpui's img(src): `alt` is what paints when the source will not decode.
// The size is the image's own unless W() / H() overrides it, and it is
// scaled down to fit the width it is given — object_fit(Contain) with
// max_w(relative(1.)), which is how node.rs renders a markdown image.
El* ImageEl(Arena* a, Str src, Str alt = {});
El* ImageEl(Arena* a, ImageSource source, Str alt = {});
El* ProgressEl(Arena* a, float value01to100, float barW, float barH);
El* ChartEl(Arena* a, const float* ys, int n, Rgba stroke, Rgba fillTop,
            Rgba fillBot, int tickMargin);

// ─── paint / window ───────────────────────────────────────────────────────

struct HitRect {
    // FocusHandle: what a press on this box focuses. Until handles existed
    // this was always `id` — every focusable box derived both numbers from
    // one name — and a box tracking a handle is the first case where the two
    // differ.
    int focusId = 0;
    int id = 0;
    Bounds bounds = {};
    Func0 onClick;
    Listener listener;
    Listener onHover;
    Listener onMouseMove;
    Listener onMouseDown;
    Listener onMouseUp;
    // Which pass of the chain each of the two was registered for.
    DispatchPhase mouseDownPhase = DispatchPhase::Bubble;
    DispatchPhase mouseUpPhase = DispatchPhase::Bubble;
    // The enclosing element that also recorded a hit rect, by index, or -1.
    // The chain a mouse event walks is this, not every box that happens to
    // contain the pointer — two absolutely placed siblings can overlap
    // without either being inside the other.
    int parent = -1;
    Listener onDragMove;
    DragPayload drag = {};
    Listener onMouseDownOut;
    Listener onMouseUpOut;
    // El::OnScrollWheel, carried through the hit test so the wheel can walk
    // the same chain a press does.
    Listener onScrollWheel;
    Str dropKind = {};
    Listener onDrop;
    CursorKind cursor = CursorKind::Arrow;
    // El::Tip. The overlay reads it when the pointer arrives, so it has to
    // survive the hit test rather than only the paint that drew it.
    Str tooltip = {};
    SliderState* slider = nullptr;
    Axis sliderAxis = Axis::Horizontal;
    InputState* input = nullptr;
    // El::OnClickAction: the action a click dispatches, and what it carries.
    uint32_t clickAction = 0;
    intptr_t clickActionArg = 0;
    // El::StopClick: the click stops here rather than carrying on outwards.
    // `cx.stop_propagation()` in a handler, said on the element instead —
    // which is where a port whose listeners cannot wrap one another can say
    // it. A field's clear button is the case: pressing the × must not also be
    // a press on the trigger it sits in.
    bool stopClick = false;
    bool stopMouseDown = false;
    bool suppressTextSelection = false;
    // PaintCtx::paintLayer when this box was recorded. A popup painted over
    // the page must not pick up the I-beam of selectable text behind it.
    int paintLayer = 0;
};

// One laid-out semantic element. `parent` is the nearest semantic ancestor,
// by index in the same Vec; visual-only boxes are skipped without flattening
// their semantic descendants out of the hierarchy. The action payload is a
// frame copy of the element behavior so an OS adapter can invoke it later.
struct AccessibilityNode {
    uint32_t id = 0;
    int parent = -1;
    Bounds bounds = {};
    AccessibilityInfo info = {};
    uint8_t actions = AccessibilityActionNone;
    int clickId = 0;
    int focusId = 0;
    Func0 onClick = {};
    Listener listener = {};
    Listener accessibilityDefault = {};
    Listener accessibilityIncrement = {};
    Listener accessibilityDecrement = {};
    Func0 accessibilityIncrementDirect = {};
    Func0 accessibilityDecrementDirect = {};
    uint32_t clickAction = 0;
    intptr_t clickActionArg = 0;
    SliderState* slider = nullptr;
    InputState* input = nullptr;
};

// A scrolled box the frame painted, and the scrollbar drawn down its right
// edge. Rust's scrollbar reaches its ScrollHandle directly; the offset here
// belongs to whichever view passed it in through El::ScrollY, so a press on
// the bar reports the offset it computed and the view stores it.
struct ScrollRect {
    int id = 0;
    Bounds bounds = {};
    float contentH = 0;
    float scrollY = 0;
    float contentW = 0;
    float scrollX = 0;
    ScrollbarMode mode = ScrollbarMode::Always;
    // Which of the two bars this box shows, from El::HideScrollbar and its
    // per-axis pair: a bar that is not painted is not there to grab either.
    bool barX = true;
    bool barY = true;
    // Whether the bar is on screen at all. A faded-out bar keeps its layout
    // and its band, and takes no press: `tracks_thumb_hover` and the disabled
    // hitbox in scrollbar.rs say the same thing.
    bool barVisible = true;
    float trackWidth = 16;
    float thumbWidth = 6;
    float thumbHoverWidth = 8;
    float thumbActiveWidth = 8;
    float thumbInset = 4;
    float thumbHoverInset = 4;
    float thumbActiveInset = 4;
    float thumbMinLength = 48;
    float thumbHoverMinLength = 48;
    float thumbActiveMinLength = 48;
    uint8_t maskAxes = 0;
    // The hit-chain node made for a masked viewport. A topmost hit that is
    // not below this node occludes the mask, so wheel input must not reach the
    // scroller underneath it.
    int maskHit = -1;
    Listener onScroll;
    // The text field this box scrolls, when it is one. An InputState is not
    // an entity and so cannot be the target of a Listener; the element names
    // the state the way El::BindInput does, and the bar writes the offset
    // straight onto it.
    InputState* input = nullptr;
};

// The scrollbar as it is drawn. THUMB_WIDTH is the thin one a fading
// `Scrolling` bar rests at; THUMB_ACTIVE_WIDTH is what every other mode draws,
// and what any bar grows to under the pointer or in a drag. THUMB_INSET is the
// margin either side, so the band a press counts in — Rust's WIDTH, 4*2+8 — is
// the wide thumb plus both insets.
const float kScrollbarThumbW = 6.f;
const float kScrollbarThumbActiveW = 8.f;
const float kScrollbarThumbMargin = 4.f;
const float kScrollbarBandW =
    kScrollbarThumbActiveW + kScrollbarThumbMargin * 2.f;

// What El::OnScroll hands its handler: the box that was scrolled and where it
// should now be. Positive-down, as El::ScrollY takes it.
struct ScrollEvent {
    int id = 0;
    float offsetY = 0;
    // The horizontal offset, for a box that scrolls both ways. A handler that
    // only scrolls down can ignore it; it is whatever it already was.
    float offsetX = 0;
};

struct TextHit {
    Bounds bounds = {};
    Str text;
    float font = 14;
    float maxW = 0;
    bool wrap = false;
    int docOff = 0;
    EntityId owner = {};
    // El::SelSrc, for a copy in SelectionFormat::Source. Null otherwise, and
    // then the run copies as its plain text in both formats.
    const SelSource* src = nullptr;
    // Whether this run continues the one before it on the same line. A
    // paragraph is one `InlineState.text` in Rust; here it is a row of word
    // elements, and this is what keeps a copy of it on one line — in both
    // formats, since that is the document's shape and not its syntax.
    bool join = false;
    // A run with no text of its own that copies as its `src->pre` alone: an
    // inline image, which node.rs gives no selection (`Paragraph::text`
    // concatenates the children's text and an image child has none) but whose
    // `![alt](url)` `selected_source` emits when the selection runs into it.
    // It holds a place in the document order so the selection can reach it,
    // and copies as nothing in Plain.
    bool atom = false;
    // TextSelectionScopeId: the focus trap this run sits inside, 0 for the
    // page itself. A selection belongs to one scope, so a drag that started
    // in a dialog does not run on into the page behind it.
    int scope = 0;
    // PaintCtx::paintLayer when this run was recorded. Cursor I-beam ignores
    // runs behind the stacking layer the pointer is actually in.
    int paintLayer = 0;
};

// Shaped-text cache (see TextMeas* in Gpui.cpp). Opaque slots. Entries stay
// until the table is large, so a virtualized editor jumping around a file
// does not reshape every line it just left — and so EndFrame does not
// rebuild the table on every notch.
struct TextMeasCache {
    void* slots = nullptr;
    int cap = 0;
    int used = 0;
    uint32_t frame = 0;
};

// The 2D backend. Direct2D + DirectWrite on Windows, cairo + Pango on Linux;
// both are opaque here and only Paint_win.cpp / Paint_linux.cpp look inside.
// `PaintApp` is the process-wide half (factories, fonts), `PaintTarget` the
// per-window drawing surface.
struct PaintApp;
struct PaintTarget;

// What the inspector is looking at. GPUI's Inspector picks an element out of
// the window and shows where it came from; an element here has no source
// location — nothing takes `#[track_caller]` — so what it can say for itself
// is its id, the box layout gave it, and the style it was built with.
// The style fields the inspector's editor can name. Rust serialises the whole
// `StyleRefinement` with serde and parses it back; there is no reflection
// table here, so this is the subset written out by hand — the ones the panel
// already reports, plus the two colours and the opacity beside them.
enum StyleField : uint32_t {
    StyleFieldBg = 1u << 0,
    StyleFieldColor = 1u << 1,
    StyleFieldBorderColor = 1u << 2,
    StyleFieldPad = 1u << 3,
    StyleFieldGap = 1u << 4,
    StyleFieldRadius = 1u << 5,
    StyleFieldBorder = 1u << 6,
    StyleFieldFontSize = 1u << 7,
    StyleFieldWidth = 1u << 8,
    StyleFieldHeight = 1u << 9,
    StyleFieldOpacity = 1u << 10,
    // Not fields the inspector's editor offers — a hover colour is not a
    // property of the box, it is what the box becomes under the pointer — but
    // they are fields a StyleRefinement can name, and `state_style.h` names
    // them.
    StyleFieldHoverBg = 1u << 11,
    StyleFieldHoverFg = 1u << 12,
    StyleFieldActiveBg = 1u << 13,
    // One side each, because Rust's `.border_l_2()` names the left edge and
    // leaves the other three alone — a refinement that copied all four would
    // clear whatever the box already had on them.
    StyleFieldBorderT = 1u << 14,
    StyleFieldBorderB = 1u << 15,
    StyleFieldBorderL = 1u << 16,
    StyleFieldBorderR = 1u << 17,
    StyleFieldMargin = 1u << 18
};

// StyleRefinement::refine, over the fields `fields` names and no others. The
// inspector's live edit and a control's semantic state are both refinements
// of a whole style, so they go through the same place.
void StyleApplyFields(Style* into, const Style& over, uint32_t fields);

// A live style override, which is what Rust's DivInspector writes back into
// the `StyleRefinement` of the element it took over. The tree is rebuilt every
// frame and its `El`s go with it, so the override is keyed by the element's
// click id and applied on the way through layout. One table, since there is
// one inspector.
void StyleOverrideSet(int clickId, uint32_t fields, const Style& style);
void StyleOverrideClear(int clickId);
void StyleOverrideClearAll();
// Patches `e->style` in place with whatever is on file for its click id.
void StyleOverrideApply(El* e);

struct InspectorPick {
    int id = 0;
    Str elId = {};
    // The whole style the element was built with, which is what the editor
    // serialises and what Reset puts back.
    Style style = {};
    Bounds bounds = {};
    // The kind of element, as El::kind reads it.
    int kind = 0;
    int depth = 0;
    bool hasBg = false;
    Rgba bg = {};
    float pad = 0;
    float gap = 0;
    float radius = 0;
    float border = 0;
    bool row = true;
    float font = 0;
    // The text a Text element holds, so a picked label says which one it is.
    Str text = {};
};

// window.toggle_inspector / Inspector::is_picking. The panel is the caller's
// to render — `component::Inspector` is the one this tree ships — and this is
// the state it reads.
struct InspectorState {
    bool on = false;
    bool picking = false;
    bool hasPick = false;
    InspectorPick pick = {};
    // A press while picking names the element it landed on, but the frame
    // that painted last was aimed at wherever the pointer was then. The pick
    // is settled on the next frame instead, against the press itself.
    bool pending = false;
    float pendingX = 0;
    float pendingY = 0;
};

namespace scene {
struct State;
}

struct PaintCtx {
    App* app = nullptr;
    Window* window = nullptr;
    PaintApp* pa = nullptr;
    PaintTarget* rt = nullptr;
    // The recorded and previous frames, damage history and retained path
    // geometry belong to this target's window. Opaque here so the portable
    // public drawing context does not expose the scene implementation.
    scene::State* sceneState = nullptr;
    // Window::element_opacity: the Style::opacity of everything this element
    // is inside, multiplied together. Every colour painted is faded by it, so
    // a subtree fades as one thing rather than each of its boxes separately.
    float opacity = 1;
    float dpi = 96;
    float viewW = 0;
    float viewH = 0;
    // Window::client_inset. A client-decorated Root writes its shadow inset
    // while building; every shared Positioner adds it to its requested edge
    // margin, matching the window-coordinate viewport Rust resolves against.
    float clientInset = 0;
    int hoverId = 0;
    // Which drop target the pointer is over and what is being dragged, so a
    // `DragOver` refinement can be resolved beside the hover one. GPUI reads
    // the same pair off the window in `compute_style`.
    int dragOverId = 0;
    Str dragKind = {};
    // The element holding the press, which is what `Style::activeBg` is
    // matched against — GPUI's `clicked_state.element`. It is the id the
    // press landed on for as long as the button is down, and 0 otherwise,
    // so it does not follow the pointer the way hoverId does.
    int activeId = 0;
    // Whether the pointer is inside the nearest enclosing `Group()` box,
    // pushed down as the tree paints the way element opacity is.
    bool groupHovered = false;
    int focusId = 0;
    // window.focus_generation: bumped every time the focus moves, so a
    // keystroke can tell that it stayed put without holding onto the element.
    int focusGen = 0;
    // Where the pointer is, which is what a Hover-mode scrollbar consults.
    float mouseX = -1;
    float mouseY = -1;
    // The scrolled box whose bar is being dragged, and which of its two. A
    // dragged thumb wears the same appearance a hovered one does — Rust's
    // `dragged_axis` — and the pointer may be nowhere near it by then, so the
    // drag has to reach the paint pass rather than being inferred from where
    // the pointer is.
    int scrollDragId = 0;
    bool scrollDragHorizontal = false;
    // Something painted this frame is part-way through a transition and wants
    // the window back: a Scrolling scrollbar fading out. The window asks for
    // an animation frame once, after the tree has painted, rather than each
    // fading bar asking for itself.
    bool wantsAnimFrame = false;
    // The enclosing hit rect while the tree paints, which is what a hit rect
    // records as its parent.
    int hitParent = -1;
    // GPUI's current ContentMask, projected onto hitboxes as well as drawing.
    // Overflow and TextView's whole-line clamp intersect this on the way down
    // the paint tree, so content hidden by a viewport cannot still be clicked.
    Bounds hitMask = {};
    bool hasHitMask = false;
    // The inspector picking an element: every box under the pointer overwrites
    // this as it paints, so the deepest one wins — which is the one a click
    // would land on.
    bool picking = false;
    bool pickHit = false;
    // How deep in the tree the element being painted is, which is what the
    // pick prefers on: an overlay layer painted last is not the element under
    // the pointer just because it went down after everything else.
    int paintDepth = 0;
    // Which stacking layer the tree is painting in — the `kPaintLayer*`
    // constants below. GPUI's primitives carry the order of the stacking
    // context they were built in and the scene sorts on it; here the paint
    // walks already run in that order, and the field is what lets
    // src/gpui/scene.cpp record it rather than infer it.
    int paintLayer = 0;
    // How good a candidate the pick so far was: 2 for an element with an id,
    // 1 for one that draws something, so an unnamed label inside a button
    // does not stand in front of the button.
    int pickTier = 0;
    InspectorPick pick = {};
    Vec<HitRect> hits;
    Vec<ScrollRect> scrolls;
    Vec<TextHit> texts;
    // The fields this frame painted, outermost box first, so a press can find
    // the one it landed in. A hit rect would shadow the click on the box
    // around it; these are a list of their own for the same reason GPUI
    // installs the editor's mouse handlers beside the container's, not
    // instead of them.
    Vec<InputState*> inputs;
    int textDocLen = 0;
    int selA = -1;
    int selB = -1;
    // Which scope the range above belongs to; -1 paints it wherever it
    // falls, which is what a caller that knows of no scopes wants.
    int selScope = -1;
    TextMeasCache textCache;

    PaintCtx() = default;
};

// One Inline's laid-out vertical extent. These helpers are public so the
// whole-line rule can be tested independently of a platform text backend.
struct LineSpan {
    float top = 0;
    float bottom = 0;
    float lineHeight = 0;
};

// Rust's line_safe_clip_bottom. True means `outBottom` is a tighter clip than
// boxBottom; false leaves the ordinary box-edge overflow clip in force.
bool LineSafeClipBottom(const LineSpan* spans, int count, float boxBottom,
                        float contentBottom, float* outBottom);

struct FocusRect {
    int id = 0;
    int trapId = 0;
    int tabIndex = 0;
    bool tabStop = true;
    bool focusOnPress = false;
    // Where this element sits in the frame's dispatch list. Rust walks the
    // real tree to find what is above a focused handle; the tree here is gone
    // by the time a key arrives, so the walk is recorded while it is still
    // there — see DispatchNode.
    int dispatchIx = 0;
    Bounds bounds = {};
    // Increment/decrement semantics inherited from the nearest ancestor.
    // A spinbutton puts them on its frame while its editor owns focus.
    Listener accessibilityIncrement = {};
    Listener accessibilityDecrement = {};
    Func0 accessibilityIncrementDirect = {};
    Func0 accessibilityDecrementDirect = {};
};

// One key context or one action handler, recorded in tree order. `subtreeEnd`
// is one past the last node of the element's whole subtree, so a node is
// above position `p` exactly when it was written before `p` and its subtree
// still has not closed: that is an ancestor test that does not care whether
// the elements in between contributed nodes of their own. Walking backwards
// from `p` visits them innermost first, which is the order Rust reads a
// dispatch path in.
struct DispatchNode {
    int subtreeEnd = 0;
    // 0 at the window root. An unfocused dispatch stands just after the
    // depth-0 nodes so a menu action still reaches the root's on_action.
    int depth = 0;
    uint32_t context = 0;
    uint32_t action = 0;
    Listener fn = {};
};

// ─── UTF-8 scanning ───────────────────────────────────────────────────────
//
// What text_boundary.rs and the input engine both walk the text with. A byte
// that is not valid UTF-8 counts as one character of its own value: every
// caller only asks which class it lands in, and every stray byte lands in the
// same one.

// crates/base/src/text_boundary.rs CharacterKind.
enum class CharKind : uint8_t {
    Word,
    Whitespace,
    Newline,
    Other
};

// `CharKindOf` and `Utf8ClipLeft` are text_boundary.rs's own and live in
// `base/text_boundary.h`; these two are the `char` iteration Rust gets
// from `str`, which every caller of either needs first.

// The codepoint at byte `i` and how many bytes it took.
int Utf8At(Str s, int i, uint32_t* out);
// Where the character before `i` starts.
int Utf8Prev(Str s, int i);

// ─── rope ─────────────────────────────────────────────────────────────────
//
// crates/base/src/input/base/rope_ext.rs. Rust's input holds its document in a
// `ropey::Rope` and reaches it through the `RopeExt` trait; here the document
// is a flat UTF-8 buffer and the trait's methods are these functions over a
// `Str`. An input holds a form field or a page of code, not a file, so the
// piece table a rope buys is machinery with nothing to do.

// sum_tree::Bias: which side of a character an offset that lands inside one
// is pulled to.
enum class Bias : uint8_t {
    Left,
    Right
};

// rope_ext.rs Point — a row and a byte column inside it, not a position on
// screen. Named the way Rust names it; gpui::Point is the geometry one.
struct RopePoint {
    int row = 0;
    int column = 0;
};

// Into the text, then to a character boundary on the named side.
int RopeClipOffset(Str text, int offset, Bias bias);
// char_at: the codepoint at `offset`, and how many bytes it took. 0 when the
// offset is past the end — Rust's `None`.
int RopeCharAt(Str text, int offset, uint32_t* out);
int RopeLinesLen(Str text);
int RopeLineStartOffset(Str text, int row);
int RopeLineEndOffset(Str text, int row);
// slice_line: the row without its newline.
Str RopeSliceLine(Str text, int row);
int RopeLineLen(Str text, int row);
RopePoint RopeOffsetToPoint(Str text, int offset);
int RopePointToOffset(Str text, RopePoint point);
int RopeOffsetUtf16ToOffset(Str text, int offsetUtf16);
int RopeOffsetToOffsetUtf16(Str text, int offset);
int RopeCharIndexToOffset(Str text, int charIndex);
int RopeOffsetToCharIndex(Str text, int offset);

// ─── input ────────────────────────────────────────────────────────────────
//
// crates/base/src/input/base. Rust's engine is one `InputBaseState<M>`
// parameterized by a compile-time mode marker (`InputMode`, `TextareaMode`,
// `EditorMode`) so a method that only makes sense for one of them does not
// exist on the others. There are no traits to bound here, so the marker is a
// runtime `InputKind` and the methods that would not compile in Rust return
// early instead — `InputMoveVertical` on a single-line field, say.

// gpui_component::input::InputEvent.
enum class InputEventKind : uint8_t {
    Change,
    PressEnter,
    Focus,
    Blur
};

struct InputEvent {
    InputEventKind kind = InputEventKind::Change;
    // PressEnter { secondary, shift }.
    bool secondary = false;
    bool shift = false;
};

// cursor.rs Selection: a byte range into the text. Empty means the caret sits
// at `start`, with nothing selected.
struct Selection {
    int start = 0;
    int end = 0;

    int Len() const { return end > start ? end - start : 0; }
    bool IsEmpty() const { return start == end; }
    bool Contains(int offset) const { return offset >= start && offset < end; }
};

inline Selection SelectionAt(int offset) {
    return Selection{offset, offset};
}

// undo_manager.rs EditIntent. What kind of edit produced a change, which is
// what decides whether two of them coalesce into one undo step.
enum class EditIntent : uint8_t {
    Typing,
    Backspace,
    DeleteForward,
    Atomic
};

// change.rs Change. Rust's owns two `String`s; these are heap `Str`s the
// transaction that holds them frees.
struct Change {
    Selection oldRange = {};
    Str oldText = {};
    Selection newRange = {};
    Str newText = {};
    Selection selBefore = {};
    Selection selAfter = {};
};

// One undo step. Rust's holds a `Vec<Change>`; a `Vec<T>` here is memcpy-only
// and this lives inside another `Vec`, so the change list is a plain owned
// array the manager grows and frees by hand.
struct UndoTransaction {
    EditIntent intent = EditIntent::Atomic;
    Change* changes = nullptr;
    int len = 0;
    int cap = 0;
};

// undo_manager.rs UndoManager. Every edit makes a transaction; adjacent
// compatible ones coalesce until something breaks the run — a cursor move, a
// paste, a blur. IME composition brackets its callbacks with Begin/Commit.
struct UndoManager {
    Vec<UndoTransaction> undos;
    Vec<UndoTransaction> redos;
    bool ignoring = false;
    bool transactionOpen = false;
    bool hasPending = false;
    Change pending = {};
    // pending_intent: what the next replace should record itself as. Taken by
    // the edit that follows.
    bool hasPendingIntent = false;
    EditIntent pendingIntent = EditIntent::Atomic;
    bool coalescingBoundary = false;

    ~UndoManager();
};

void UndoRecordTransaction(UndoManager* m, Change change, EditIntent intent);
void UndoBeginTransaction(UndoManager* m);
void UndoCommitTransaction(UndoManager* m);
void UndoBreakCoalescing(UndoManager* m);
void UndoSetIgnoring(UndoManager* m, bool ignoring);
bool UndoIsIgnoring(const UndoManager* m);
void UndoClear(UndoManager* m);
// undo() / redo(). Rust clones the change list out; the transaction lives on
// the other stack either way, so these hand back the one that moved and the
// caller walks it — backwards for an undo, forwards for a redo. Null when the
// stack is empty.
const UndoTransaction* UndoPopUndo(UndoManager* m);
const UndoTransaction* UndoPopRedo(UndoManager* m);

// mask_pattern.rs MaskToken. `Sep` carries its character, which the pattern
// string holds, so this is the tag alone.
enum class MaskToken : uint8_t {
    Digit,         // 9  — [0-9]
    Letter,        // A  — [a-zA-Z]
    LetterOrDigit, // # — [a-zA-Z0-9]
    Any,           // *  — any character
    Sep            // anything else, matching only itself
};

enum class MaskKind : uint8_t {
    None,
    Pattern,
    Number
};

// mask_pattern.rs MaskPattern. Rust keeps the parsed `Vec<MaskToken>` beside
// the pattern; a token is a pure function of its character, so the pattern
// string is the whole state and `MaskTokenAt` reads it.
struct MaskPattern {
    MaskKind kind = MaskKind::None;
    // Pattern: owned, freed by MaskPatternFree.
    Str pattern = {};
    // Number: the group separator, 0 for None.
    uint32_t separator = 0;
    // Number: how many fraction digits to keep, -1 for None.
    int fraction = -1;
};

// `(999)999-9999`, `AAAA-99-####`, `*999*`.
MaskPattern MaskPatternNew(Str pattern);
MaskPattern MaskPatternNumber(uint32_t separator);
void MaskPatternFree(MaskPattern* p);
// The token at character index `pos`, and its separator character. False when
// the pattern has no token there.
bool MaskTokenAt(const MaskPattern& p, int pos, MaskToken* out, uint32_t* sep);
bool MaskIsNone(const MaskPattern& p);
bool MaskIsValid(const MaskPattern& p, Str maskText);
bool MaskIsValidAt(const MaskPattern& p, uint32_t ch, int pos);
// mask(): 123456789 through `(999)999-999` is `(123)456-789`.
Str MaskApply(Arena* a, const MaskPattern& p, Str text);
// unmask(): the original text back out of a masked one.
Str MaskUnapply(Arena* a, const MaskPattern& p, Str maskText);
// The cue a pattern shows when empty: `(___) ___-____`. Empty for the other
// two kinds, which is Rust's `None`.
Str MaskPlaceholder(Arena* a, const MaskPattern& p);
// normalize_number_input: full-width and CJK number characters folded to
// their ASCII equivalents, so `123。5` reaches the parser as `123.5`.
Str NormalizeNumberInput(Arena* a, Str text);

// kind.rs. Which of the three states this is. Rust fixes it at the type level;
// `InputIsMultiLine` is its `MULTI_LINE` associated constant.
enum class InputKind : uint8_t {
    Input,
    Textarea,
    Editor
};

// mode.rs LayoutMode. How the input lays its text out — rows and growth. Not
// the same question as `InputKind`: an auto-growing textarea capped at one row
// is still multi-line, which is the bug Rust split these two apart to fix.
enum class LayoutModeKind : uint8_t {
    PlainText,
    AutoGrow,
    CodeEditor
};

struct LayoutMode {
    LayoutModeKind kind = LayoutModeKind::PlainText;
    int rows = 1;
    int minRows = 1;
    int maxRows = 0; // 0 = usize::MAX
    int tabSize = 4;
    bool lineNumber = false;
    // LayoutMode::CodeEditor { folding }. Rust defaults it on and the story
    // turns it off; it is off here until something asks, because a plain
    // textarea has no gutter to hang the chevrons in.
    bool folding = false;
};

// LayoutMode::is_folding(): a code editor, with folding left on.
bool LayoutModeIsFolding(const LayoutMode& m);

void LayoutModeSetRows(LayoutMode* m, int rows);
int LayoutModeRows(const LayoutMode& m);
int LayoutModeMinRows(const LayoutMode& m);

// state.rs InputBaseState. The text a themed Input is bound to, everything
// ─── auto-scroll (crates/base/src/auto_scroll.rs) ─────────────────────────
//
// A drag that reaches the edge of a scrolling box keeps scrolling it while
// the pointer stays there, faster the further out it goes. Rust drives it
// with a 16 ms background task per state; here the frame loop is that clock,
// so what a state carries is only the delta in force and where the pointer
// was — the arithmetic is `AutoScrollComputeDelta` and is the same either way.
// It lives beside InputState because that is what holds one; the logic is in
// src/base/auto_scroll.cpp, the way the rest of the input engine is.

// MIN_SPEED and MAX_SPEED, in DIPs per tick.
const float kAutoScrollMinSpeed = 12.f;
const float kAutoScrollMaxSpeed = 64.f;
// INNER_ZONE: the trigger starts this far *inside* the box, so a drag still
// scrolls in a full-screen window where the pointer cannot get outside the
// element at all.
const float kAutoScrollInnerZone = 16.f;
// OUTER_RAMP: how far past the edge reaches MAX_SPEED. The ramp is the two
// added together, which is what makes one smooth curve with no flat part.
const float kAutoScrollOuterRamp = 80.f;

// compute_delta: how far a pointer at `y` should move the box, positive
// toward the bottom. False inside the dead zone, where nothing scrolls.
bool AutoScrollComputeDelta(float y, Bounds bounds, float* out);

struct AutoScroll {
    // The delta in force. Rust shares an `Option<Pixels>` with its task and
    // writes None to stop it; `active` is that None.
    float delta = 0;
    bool active = false;
    // last_drag_position: where the pointer was, so a tick can re-run the
    // selection at the same place while the content moves under it.
    Point lastDrag = {};
    bool hasLastDrag = false;

    bool IsActive() const { return active; }
    // set(Some(d)) and set(None). The second stops the ticking but keeps the
    // drag position, which is what a move back inside the box does.
    void Set(float d) {
        delta = d;
        active = true;
    }
    void SetNone() {
        delta = 0;
        active = false;
    }
    // stop(): the ticking and the drag position both.
    void Stop() {
        SetNone();
        lastDrag = {};
        hasLastDrag = false;
    }
};

// that edits it, and the undo history behind it. `onChange` is what Rust
// spells cx.subscribe(&input_state, |ev: &InputEvent| ...).
// SearchMatcher, crates/base/src/input/editor/search.rs: a query, where it is
// found in the text, and a cursor into that list. Rust builds an aho-corasick
// automaton over a single literal pattern, which is a substring scan with an
// ASCII case fold on the side — so that is what this is, and no library comes
// with it.
struct SearchMatcher {
    // matched_ranges, in order and non-overlapping.
    Vec<Selection> ranges;
    int current = 0;
    // ascii_case_insensitive, which is the fold aho-corasick is built with.
    bool caseInsensitive = true;
    // The query, owned. Empty is Rust's `None`: no automaton, no matches.
    Str query = {};
    // The text the ranges were found in. Rust clones the rope and compares
    // it to know whether anything moved; a clone is structural there and a
    // copy of the document here, which is the one cost this port pays for
    // keeping the comparison exact.
    Vec<char> text;
    // begin_replacement: set for the one update that a replacement causes, so
    // the cursor is clamped into the shorter list rather than reset to the
    // top. Cleared by that update, whether or not anything moved.
    bool replacing = false;

    ~SearchMatcher() {
        VecReset(ranges);
        VecReset(text);
        StrFree(query);
    }
};

// new(). A matcher is a plain value, so this is only for putting a used one
// back to the start.
void SearchMatcherReset(SearchMatcher* m);
// update(&text): the text is what it is now, and the matches follow.
void SearchMatcherUpdate(SearchMatcher* m, Str text);
void SearchMatcherUpdateQuery(SearchMatcher* m, Str query, bool insensitive);
inline int SearchMatcherLen(const SearchMatcher* m) {
    return m->ranges.len;
}
inline bool SearchMatcherIsEmpty(const SearchMatcher* m) {
    return m->ranges.len == 0;
}
inline int SearchMatcherIndex(const SearchMatcher* m) {
    return m->current;
}
// label(): "3/17", or "0/0" when nothing matched.
Str SearchMatcherLabel(Arena* a, const SearchMatcher* m);
// set_current_match_index: clamped into the list, as Rust's `.min(len - 1)`.
void SearchMatcherSetIndex(SearchMatcher* m, int ix);
void SearchMatcherBeginReplacement(SearchMatcher* m);
bool SearchMatcherHasNextWithoutWrap(const SearchMatcher* m);
// peek(): the range `next` would land on, without moving the cursor.
bool SearchMatcherPeek(const SearchMatcher* m, Selection* out);
// The current range, if there is one.
bool SearchMatcherCurrent(const SearchMatcher* m, Selection* out);
// update_cursor_by_offset: the first match at or after the offset, which is
// where a freshly opened panel starts from.
void SearchMatcherCursorByOffset(SearchMatcher* m, int offset);
// Iterator::next and DoubleEndedIterator::next_back, both of which wrap.
bool SearchMatcherNext(SearchMatcher* m, Selection* out);
bool SearchMatcherPrev(SearchMatcher* m, Selection* out);

/* Port of crates/base/src/input/editor/display_map — the folding half.

   Rust's display map is two projections stacked: buffer -> wrap (soft wrap)
   and wrap -> display (folding). The rows here are logical lines already — a
   soft-wrapped line is one row as tall as the text in it, so `rowBoxes` is
   indexed by line and the wrap projection has nowhere to live — which leaves
   the fold projection, and its two ends are line and display row rather than
   wrap row and display row. Everything else is `fold_map.rs` as written.

   Where the candidates come from is the themed layer's business, the way it
   is Rust's: `apply_highlighter_fold_candidates` takes whatever the
   highlighter found. */

// folding.rs FoldRange. A foldable run of lines, both ends inclusive.
struct FoldRange {
    int startLine = 0;
    int endLine = 0;
};

// fold_map.rs FoldMap. `candidates` is what could be folded and `folded` is
// what is; the two index vectors are the projection built from them, rebuilt
// lazily because a keystroke changes the text far more often than it changes
// which lines are hidden.
struct FoldMap {
    // Sorted by startLine, at most one range per startLine.
    Vec<FoldRange> candidates;
    // A subset of `candidates`, sorted the same way.
    Vec<FoldRange> folded;
    // display row -> line (fold_map's `visible_wrap_rows`).
    Vec<int> visibleLines;
    // line -> display row, -1 for a line inside a closed fold.
    Vec<int> lineToDisplayRow;
    bool needsRebuild = true;
    // The line count the projection was last built against, so a rebuild can
    // be skipped when neither the text nor the folds have moved.
    int cachedLineCount = 0;
};

// set_candidates: a full replacement. Sorts, drops all but the first range
// per start line, and forgets any fold whose candidate is gone.
void FoldMapSetCandidates(FoldMap* m, const FoldRange* ranges, int n);
// set_folded / toggle_fold. A start line that is not a candidate is ignored.
void FoldMapSetFolded(FoldMap* m, int startLine, bool folded);
void FoldMapToggle(FoldMap* m, int startLine);
bool FoldMapIsFolded(const FoldMap* m, int startLine);
bool FoldMapIsCandidate(const FoldMap* m, int startLine);
// clear_folds: everything opens, the candidates stay.
void FoldMapClearFolds(FoldMap* m);
// adjust_folds_for_edit: a fold or a candidate overlapping the edited lines
// is dropped, and one after them is shifted by however many lines the edit
// added or removed. Cheaper than re-extracting on every keystroke, and what
// keeps a fold attached to its text while it is typed above.
void FoldMapAdjustForEdit(FoldMap* m, int editStartLine, int editEndLine,
                          int lineDelta);
// rebuild: the projection, against a document of `lineCount` lines. A no-op
// unless something moved.
void FoldMapRebuild(FoldMap* m, int lineCount);
// How many rows are on screen — the line count when nothing is folded.
int FoldMapDisplayRowCount(const FoldMap* m);
// wrap_row_to_display_row / display_row_to_wrap_row, on lines. -1 for a line
// that is hidden, or a display row past the end.
int FoldMapDisplayRow(const FoldMap* m, int line);
int FoldMapLineAt(const FoldMap* m, int displayRow);
// True when a closed fold hides the line outright.
bool FoldMapLineHidden(const FoldMap* m, int line);
// nearest_visible_display_row, answered as a line: the line itself when it is
// visible, and the nearest one above it when it is not.
int FoldMapNearestVisibleLine(const FoldMap* m, int line);

// One chevron's box, from the frame the gutter last built. Rust inserts a
// hitbox per icon during prepaint and hangs a mouse-down listener on it; a
// press here is routed by the window through the field it landed in, so what
// the frame has to leave behind is where the icons were.
struct FoldIconBox {
    int line = 0;
    Bounds bounds = {};
};

// SearchSession: the panel's state, kept on the field so it survives the
// panel being closed and opened again.
struct SearchSession {
    bool open = false;
    bool replaceMode = false;
    bool caseInsensitive = true;
    Str query = {};       // owned
    Str replacement = {}; // owned
    // anchor_offset: where the view was when the panel opened, so the first
    // match chosen is the one nearest what you were looking at. -1 is None.
    int anchorOffset = -1;
    SearchMatcher matcher;

    ~SearchSession() {
        StrFree(query);
        StrFree(replacement);
    }
};

void SearchSessionSetQuery(SearchSession* s, Str query, bool insensitive);
void SearchSessionSetReplacement(SearchSession* s, Str replacement);

// input/editor/diagnostics.rs DiagnosticSeverity, in the order the colours
// are read by.
enum class DiagnosticSeverity : uint8_t {
    Hint,
    Error,
    Warning,
    Info
};

// lsp_types::DiagnosticTag. The numeric values are the LSP wire values, so a
// provider can translate them without a switch.
enum class DiagnosticTag : uint8_t {
    Unnecessary = 1,
    Deprecated = 2
};

// lsp_types::DiagnosticRelatedInformation, flattened to this editor's byte
// range convention. `uri` names the related document; empty means the
// document already in the field.
struct DiagnosticRelatedInformation {
    Str uri = {};
    Selection range = {};
    Str message = {};
};

using RelatedInformation = DiagnosticRelatedInformation;

// One diagnostic over a range of the document. Rust keeps the LSP's own
// struct — related information, tags and a serde_json payload with it — and
// this keeps what an editor draws and says: where it is, how bad it is, what
// it says, and where it came from.
struct Diagnostic {
    Selection range = {};
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    Str message = {};
    Str source = {};
    Str code = {};
    Str codeDescriptionUri = {};
    const DiagnosticRelatedInformation* relatedInformation = nullptr;
    int nRelatedInformation = 0;
    const DiagnosticTag* tags = nullptr;
    int nTags = 0;
    // The dependency-free form of LSP's arbitrary JSON `data`: a provider
    // may preserve its serialized spelling and hand it back with an action.
    Str data = {};
};

// lsp_types::CompletionItem, cut to what the menu shows and what accepting
// one writes. Rust carries the whole LSP struct — the sort text, the edits,
// the command that may follow — and the menu reads these five of it.
// lsp_types::TextEdit: a range of the document and what replaces it. Rust's
// range is a pair of positions; every seam in this tree speaks byte offsets,
// so this is the same edit written the way the rest of the input engine
// writes one.
struct TextEditItem {
    Selection range = {};
    Str newText = {};
};

// apply_lsp_edits: a list of them, in order. Each edit's range is resolved
// against the document *as the edits before it left it*, which is why a
// server sends them last-first — and each is its own undo step, which is what
// Rust's loop over `replace_text_in_range_silent` records (the `silent` there
// suppresses the completion trigger and says nothing about the history).
void InputApplyEdits(InputState* s, App* app, Window* win,
                     const TextEditItem* edits, int n);

struct CompletionItem {
    // What is shown, and what the query is matched against.
    Str label = {};
    // Shown muted and italic beside the label; the LSP's `detail`.
    Str detail = {};
    // What replaces the query. Empty means the label itself.
    Str insertText = {};
    // Markdown, in the pane beside the list.
    Str documentation = {};
    bool deprecated = false;
    // Whether `completionItem/resolve` has been asked about this item. An
    // item that came with documentation is never asked about.
    bool resolved = false;
    // `additionalTextEdits`: what else accepting this item writes — the
    // import a name needs, at the top of the document, while the name itself
    // goes in at the caret. Applied with the insert, as one undo step.
    const TextEditItem* additionalEdits = nullptr;
    int nAdditionalEdits = 0;
};

// CompletionProvider::completions, without the task: the provider is handed
// the document, where the caret is and the word being typed. It returns the
// total number of available items and writes the first min(total, cap) when
// `out` is non-null. Returning the total is important: the caller retries
// with a larger buffer instead of silently turning Rust's Vec into a C++
// limit. Rust answers a future; there is nothing to await on here, so a
// provider that has to go somewhere slow does the going itself and answers
// what it has.
using CompletionFn = int (*)(void* data, Str text, int offset, Str query,
                             CompletionItem* out, int cap);

// ColorInformation: a range of the document that names a colour, and the
// colour it names — `#1e90ff`, `rgb(0 0 0)`, and what a provider makes of
// them. The range is painted in that colour behind the text it covers.
struct DocumentColor {
    Selection range = {};
    Rgba color = {};
};

// DocumentColorProvider::document_colors, without the task: the provider is
// handed the document and writes what it found, in document order. Like all
// input collection providers it returns the total and writes at most `cap`.
using DocumentColorFn = int (*)(void* data, Str text, DocumentColor* out,
                                int cap);

// The one intentional limit in this family. Rust rejects an entire provider
// response above 10,000 rather than displaying a truncated prefix.
const int kMaxDocumentColors = 10000;

// CodeAction, flattened: a title, and the one edit it makes — the range it
// replaces and what it puts there. Rust carries a WorkspaceEdit, which is a
// map of documents to edit lists; a field is one document and every action
// upstream writes makes one edit, so this is that edit.
struct CodeActionItem {
    Str title = {};
    // Which provider answered with it, so performing it goes back to that
    // one — Rust's `provider_id`.
    int provider = 0;
    Selection range = {};
    Str newText = {};
    // The whole edit list, for an action that is more than one. Rust carries
    // a WorkspaceEdit — a map of documents to edit lists — and a field is one
    // document, so an action is that document's list; the pair above is the
    // shorthand that every action upstream writes fits in, and is what is
    // used when this is empty.
    const TextEditItem* edits = nullptr;
    int nEdits = 0;
};

// CodeActionProvider::perform_code_action: the provider does the action
// itself rather than leaving its edits to the editor. True when it did.
struct CodeActionItem;
using CodeActionPerformFn = bool (*)(void* data, InputState* s, App* app,
                                     Window* win, const CodeActionItem* item);

// CodeActionProvider::code_actions, without the task: the provider is handed
// the document and what is selected, returns the total action count and
// writes at most `cap`. Strings it answers are allocated out of `a`, which
// lives as long as the menu is up.
using CodeActionFn = int (*)(void* data, Arena* a, Str text, Selection sel,
                             CodeActionItem* out, int cap);

// One entry in Rust's Vec<Rc<dyn CodeActionProvider>>. Function pointers and
// explicit capture data are the dependency-free equivalent of the trait
// object; the Vec preserves Rust's unbounded provider count.
struct CodeActionProviderEntry {
    CodeActionFn provide = nullptr;
    void* data = nullptr;
    CodeActionPerformFn perform = nullptr;
};

// The code action menu while it is up — CodeActionMenu's own state.
struct CodeActionSession {
    bool open = false;
    int selected = 0;
    Vec<CodeActionItem> items;
    // Bumped whenever the content changes. See CompletionSession::revision.
    uint64_t revision = 0;
    // What the titles and the replacement texts were written into, thrown
    // away and taken again each time the menu is asked for.
    Arena* arena = nullptr;

    ~CodeActionSession();
};

// HoverProvider::hover, without the task: the provider is handed the document
// and the offset the pointer is over, and answers the markdown to show, or an
// empty string for nothing to say. What it answers has to outlive the frame —
// a provider answers out of its own store, not off the stack.
using HoverFn = Str (*)(void* data, Str text, int offset);

// CompletionProvider::is_completion_trigger: whether what was just typed at
// that offset should open, keep or close the menu. Rust asks the provider on
// every keystroke; a provider that names none of this gets the rule below,
// which is what every provider in this tree has wanted.
enum class CompletionTrigger : uint8_t {
    // A word character: carry a menu that is up, open one that is not.
    Continue,
    // `.` and the like: open one where the caret stands, whatever is behind
    // it.
    Open,
    // Anything else, which puts the menu away.
    Close
};

using CompletionTriggerFn = CompletionTrigger (*)(void* data, Str text,
                                                  int offset, Str typed);

// CompletionProvider::resolve_completions — `completionItem/resolve`. The
// menu asks about the item the selection is on, once, when it arrived with
// no documentation of its own: a server that sends a thousand items sends
// them thin and fills one in when it is looked at. What it answers is
// allocated out of `a`, which lives as long as the menu.
using CompletionResolveFn = Str (*)(void* data, Arena* a,
                                    const CompletionItem* item);

// CompletionMenuOptions: how wide the popover may be. 320 is Rust's default,
// "fine for most identifiers"; a host that surfaces longer labels widens it.
const float kCompletionMenuMaxW = 320.f;

// CompletionProvider::inline_completion, without the task: the provider is
// handed the document and the caret and answers the text to suggest after it,
// or nothing. `textDocument/inlineCompletion` — the ghost text a suggestion
// engine puts in front of the caret, which Tab accepts. What it answers is
// allocated out of `a`, which lives until the suggestion is dropped.
using InlineCompletionFn = Str (*)(void* data, Arena* a, Str text, int offset);

// DEFAULT_INLINE_COMPLETION_DEBOUNCE: how long the typing has to stop before
// the provider is asked.
const float kInlineCompletionDebounceMs = 300.f;

// The suggestion in front of the caret, while there is one.
struct InlineCompletion {
    // What the provider answered, and where the caret was when it did. A
    // caret that has moved since drops it.
    Str text = {};
    int at = -1;
    // When the provider may be asked, and whether it has been. Rust spawns a
    // task with a timer; the frame is the clock here, the way every other
    // delay in this tree is.
    double dueAt = 0;
    bool asked = true;
    Arena* arena = nullptr;

    ~InlineCompletion();
};

// ─── range semantic tokens (input/editor/lsp/semantic_tokens.rs) ─────────
//
// The highlighting a language server publishes, layered over the built-in
// highlighter rather than replacing it. A token arrives delta-encoded — five
// numbers, each position relative to the token before it — and names its type
// by an index into a legend the provider declares. The *name* is what is
// cached: the colour is resolved from it at paint, so a theme change recolours
// with nothing refetched.

// One token as the wire carries it.
struct SemanticToken {
    uint32_t deltaLine = 0;
    uint32_t deltaStart = 0;
    uint32_t length = 0;
    uint32_t tokenType = 0;
    uint32_t tokenModifiers = 0;
};

// One decoded token: where it is in line and column — a token never spans a
// line — and the legend name of its type.
struct SemanticSpan {
    int line = 0;
    int col = 0;
    int len = 0;
    Str name = {};
};

// A decoded token resolved against a document: the bytes it covers, and the
// name a caller looks a colour up by.
struct SemanticRange {
    Selection range = {};
    Str name = {};
};

// DocumentRangeSemanticTokensProvider::semantic_tokens, without the task: the
// provider is handed the document and a byte range, returns the total token
// count and writes at most `cap`, delta-encoded as a server would send them.
// The legend beside it is `DocumentRangeSemanticTokensProvider::legend`.
using SemanticTokensFn = int (*)(void* data, Str text, Selection range,
                                 SemanticToken* out, int cap);

// decode_semantic_tokens: the delta encoding unpacked into absolute
// positions, in document order. A token whose type is not in the legend is
// skipped, which is what an out-of-range index means.
int SemanticTokensDecode(const SemanticToken* toks, int n, const Str* names,
                         int nNames, SemanticSpan* out, int cap);

// semantic_tokens_for_range: the tokens touching `visible`, as byte ranges.
// The cache is in document order, so the window is binary-searched rather
// than scanned — a document of ten thousand tokens pays for the ones on
// screen. A token resolving to an empty range is skipped.
int SemanticTokensForRange(const SemanticSpan* toks, int n, Str text,
                           Selection visible, SemanticRange* out, int cap);

// LocationLink, flattened. `uri` is the document the target is in: empty
// means this one, and the target range is then an offset pair into the text
// being edited. An `http`/`https` uri is a page rather than a document, and
// goes to whatever the desktop opens links with.
struct DefinitionLink {
    // origin_selection_range: the symbol that was asked about. Empty lets the
    // word under the offset stand for it, which is what Rust falls back to.
    Selection origin = {};
    Str uri = {};
    // target_selection_range: what to select once we are there.
    Selection target = {};
};

// DefinitionProvider::definitions, without the task: the provider is handed
// the document and the offset, returns the total location count and writes at
// most `cap`. Strings it answers are allocated out of `a`, which lives until
// the next question is asked.
using DefinitionFn = int (*)(void* data, Arena* a, Str text, int offset,
                             DefinitionLink* out, int cap);

// ShowDocumentHandler: the host's chance to show a document itself, which is
// the `window/showDocument` request. True means it did and the built-in
// handling is skipped — an external uri going to the browser, anything else
// jumping inside this document.
using ShowDocumentFn = bool (*)(void* data, Str uri, bool external,
                                Selection selection);

// HoverDefinition: what a secondary-hover found under the pointer, and what
// it found last. The last pair is what the GoToDefinition action goes by:
// the hover clears as soon as the modifier comes up, and the action still has
// to know what the symbol under the caret was.
struct HoverDefinition {
    Selection symbolRange = {};
    Vec<DefinitionLink> locations;
    Selection lastRange = {};
    Vec<DefinitionLink> lastLocations;
    // Where the symbol was last painted, in window coordinates — Rust inserts
    // a hitbox over exactly this, to put the hand cursor on it.
    Bounds bounds = {};
    // What the uris were written into, taken again each time the provider is
    // asked.
    Arena* arena = nullptr;

    ~HoverDefinition();
};

// The completion menu while it is up — CompletionMenu's own state.
struct CompletionSession {
    bool open = false;
    // Where the word being completed began, and the caret it was asked at.
    int triggerStart = -1;
    int offset = 0;
    int selected = 0;
    // ContextMenuDelegate::query. The menu keeps its own copy so a provider's
    // transient query can still color the matching label prefix next frame.
    Str query = {};
    // What the provider answered, in its own order.
    Vec<CompletionItem> items;
    // Bumped whenever the content changes. A renderer that mirrors this menu
    // — a host drawing its own popover — compares revisions to decide whether
    // to rebuild, so it never has to compare the item list itself.
    uint64_t revision = 0;
    // What `completionItem/resolve` wrote into, dropped with the menu.
    Arena* arena = nullptr;

    ~CompletionSession();
};

// EditorStyle::diagnostics: the colour a severity underlines in.
struct DiagnosticColors {
    Rgba error = {};
    Rgba warning = {};
    Rgba info = {};
    Rgba hint = {};
};
// Declared here because the overlay hook below names it and the state that
// holds the hook comes before the action table.
enum class InputAction : uint8_t;

enum class InputOverlayKind : uint8_t {
    Completion,
    CodeAction
};

// set_overlay_action_handler: asked before the editor's own menu handling,
// with the menu that is open and the action that arrived. True means the host
// took it.
using OverlayActionFn = bool (*)(void* data, InputOverlayKind kind,
                                 InputAction action);

// tree_sitter::InputEdit: where an edit began and where the old and new text
// end, in bytes and in points. `oldEndByte` of -1 is Rust's `edit: None` —
// the edit could not be tracked (several splices piled up, or the whole
// value was replaced) and an incremental implementation should re-scan the
// document. The text funnels fill only the byte half; `New` computes the
// points from the old text for an implementation that wants them.
// (Lived in input_rope.h until InputState carried one.)
struct InputEdit {
    int startByte = 0;
    int oldEndByte = 0;
    int newEndByte = 0;
    RopePoint startPosition = {};
    RopePoint oldEndPosition = {};
    RopePoint newEndPosition = {};

    static InputEdit New(Str oldText, Selection range, Str inserted);
};

// highlighting.rs HighlightStyleResolver: semantic capture names resolved
// into renderable styles. Base deliberately knows nothing about a concrete
// syntax theme; the themed layer provides the resolver.
struct HighlightStyleResolver {
    void* data = nullptr;
    bool (*style)(void* data, Str name, TextSpan* out) = nullptr;

    bool Style(Str name, TextSpan* out) const;
};

using SharedHighlightStyleResolver = HighlightStyleResolver;

/* crates/base/src/input/editor/highlighting.rs `InputHighlighter`: the
   parser-independent highlighting seam the editor consumes. Implementations
   own parsing, incremental state and language behaviour; the element only
   asks for styled runs over a range and for fold candidates. Rust spells it
   as a trait object behind a factory, and upstream's tree-sitter lives
   *behind* this seam as an optional cargo feature — crates/base itself
   depends on no parser, and the wasm build highlights nothing. Here it is
   function pointers and a data word, the way the LSP providers are carried;
   the one implementation is the hand-written lexer in src/ui/highlighter.cpp,
   and a tree-sitter port would be a second implementation of these entries,
   not a rewrite. (Lived unwired in input_editor.h until the editor consumed
   it; moved here when InputState grew the installed instance.)

   What the simplification drops: `update` runs synchronously where Rust
   hands a too-large parse to the background executor and shows stale styles
   meanwhile. */
struct InputHighlighter {
    void* data = nullptr;
    Str (*language)(void* data) = nullptr;
    // The document changed: bring whatever is cached up to date. `edit` null
    // means re-scan whole, the way Rust's `edit: None` does. The driver
    // gates calls on InputState::docVersion, which is the flat-buffer
    // spelling of upstream's `self.text.eq(text)` early-out.
    void (*update)(void* data, const InputEdit* edit, Str text,
                   bool folding) = nullptr;
    // Ordered, non-overlapping runs covering `range`, allocated from `a`;
    // answers how many. A gap between runs is unstyled text. Only ever asked
    // for the visible band — element.rs groups the visible lines and asks
    // per group — which is what lets a document of any size highlight
    // correctly with no whole-document span list anywhere.
    int (*styles)(void* data, Selection range,
                  const HighlightStyleResolver* resolver, Arena* a,
                  TextSpan** out) = nullptr;
    // Every foldable block, allocated from `a`. `changedRange` is
    // fold_ranges_for_edit's refinement; an implementation may ignore it and
    // answer for the whole document.
    int (*foldRanges)(void* data, Str text, Selection changedRange, Arena* a,
                      FoldRange** out) = nullptr;
    // The state is going away; free `data`.
    void (*drop)(void* data) = nullptr;

    Str Language() const;
    void Update(const InputEdit* edit, Str text, bool folding) const;
    int Styles(Selection range, const HighlightStyleResolver* resolver,
               Arena* a, TextSpan** out) const;
    int FoldRanges(Str text, Selection changedRange, Arena* a,
                   FoldRange** out) const;
};

// element.rs compose_decoration_collections, flat: the `decs` win over the
// `spans` they overlap — every span is cut back to what the decorations
// leave it, and the decorations go in whole. Both lists are ordered by `lo`
// and so is the result, written back into `spans` through the caller's
// scratch `tmp` (at least `cap` long).
int InputComposeSpans(TextSpan* spans, int n, const TextSpan* decs, int nDecs,
                      int cap, TextSpan* tmp);

struct InputState {
    InputKind kind = InputKind::Input;
    LayoutMode mode = {};
    // InputBaseState::focus_handle. All three mode aliases expose this same
    // retained handle, and AnyInputState forwards it without inspecting the
    // frame tree.
    FocusHandle focus = {};
    // Rust's `Rope`. NUL-terminated past `len` so a `const char*` reader still
    // works; the terminator is not counted.
    Vec<char> text;
    // Bumped by every splice of `text`, so whatever is derived from the whole
    // document — the syntax cache below — can tell an unchanged document from
    // a changed one without comparing it.
    uint64_t docVersion = 0;
    // ropey's line index, flat: the byte offset each line starts at, one
    // entry per line, lineStarts[0] = 0. Rust's Rope answers line_to_byte_idx
    // and byte_to_line_idx in O(log n) from its tree; this is the same answer
    // as a lookup, rebuilt lazily when docVersion moves. Read it through
    // InputLineStarts, never directly.
    Vec<int> lineStarts;
    uint64_t lineStartsVersion = 0;
    bool lineStartsValid = false;
    // The installed highlighter, if any — Rust's
    // `Rc<RefCell<Option<Box<dyn InputHighlighter>>>>` on the editor mode.
    // The facade that knows the language installs one; the destructor drops
    // it. The element queries it for the visible range every frame; the
    // implementation caches across frames and keys on docVersion.
    InputHighlighter highlighter = {};
    // The edit envelope since the highlighter last updated, recorded by the
    // text funnels — what Rust passes to `update` from on_text_changed. One
    // splice is kept exactly; a second before it is consumed collapses it to
    // the whole-document marker (oldEndByte -1).
    InputEdit pendingEdit = {};
    bool hasPendingEdit = false;
    Selection selectedRange = {};
    bool selectionReversed = false;
    // selected_word_range: what a double click took, kept so dragging out of
    // it cannot shrink back inside the word.
    bool hasSelectedWordRange = false;
    Selection selectedWordRange = {};
    UndoManager undo;
    MaskPattern maskPattern = {};
    bool maskPatternSet = false;
    Str placeholder = {}; // owned
    bool focused = false;
    // The window this field is the focused one of, so that a field taken out
    // of the tree while focused can take its registration with it. Rust drops
    // a stale one lazily — `focused_input` checks the handle is still focused
    // — which needs a handle that outlives the view; here the state *is* the
    // registration, so it clears itself when it goes.
    Window* focusWin = nullptr;
    bool disabled = false;
    bool readonly = false;
    bool loading = false;
    // A masked field draws one bullet per character. InputMode only.
    bool masked = false;
    bool cleanOnEscape = false;
    bool submitOnEnter = false;
    // searchable / replaceable: whether ctrl-f opens a find bar over this
    // field at all, and whether that bar may write back. Rust defaults the
    // first to false and turns it on for the code editor, and the second to
    // true — a field that cannot be edited is not replaceable anyway, which
    // `InputIsReplaceable` is what says.
    bool searchable = false;
    bool replaceable = true;
    SearchSession search;
    uint64_t searchActivationRevision = 0;
    // Code folding. The projection survives an edit; the icon boxes are the
    // last frame's and are rebuilt with it.
    FoldMap folds;
    Vec<FoldIconBox> foldIcons;
    // The line-number + fold strip, which is what says where the gutter is.
    // Rust hangs a hitbox over the whole column and shows the chevrons
    // while it is hovered; the column is the same x for every row, so one
    // visible row's cell locates it. y/h of this box are one row; the
    // hover test uses the editor's clip for the vertical extent.
    Bounds gutterBox = {};
    bool softWrap = true;
    // show_whitespaces: paint a mid-dot on every space and an arrow on
    // every tab. Off until the editor's status bar turns it on.
    bool showWhitespaces = false;
    // scroll_beyond_last_line / cursor_surrounding_lines: -1 is None
    // (the default heuristic), >= 0 is Some(n). VSCode's
    // editor.scrollBeyondLastLine and editor.cursorSurroundingLines.
    int scrollBeyondLastLine = -1;
    int cursorSurroundingLines = -1;
    // text_align: 0 left, 1 center, 2 right.
    int align = 0;
    // A press is down and every move until the release extends the selection.
    bool selecting = false;
    // A selection drag that has reached the edge of a scrolled field keeps
    // scrolling it. Single-line fields have nowhere to go and never set it.
    AutoScroll autoScroll;
    // This field's caret clock, InputState::blink_cursor. Created on first
    // use, so an InputState stays a plain value.
    EntityId blink = {};
    Listener onChange = {};
    // validate: `Fn(&str, &mut App) -> bool`. A plain function pointer plus
    // its captured value, the way Listener carries one.
    bool (*validate)(Str text, intptr_t arg) = nullptr;
    intptr_t validateArg = 0;
    // The text run the element last painted, so a press can be turned into an
    // offset. Rust keeps `last_bounds` + `last_layout` for the same reason;
    // in a multi-line field this is the *first* row, and the ones under it are
    // found by stepping `lastLineH` down from it — unless soft wrap made them
    // different heights, which is what `rowBoxes` is for.
    Bounds lastBounds = {};
    float lastFont = 0;
    float lastLineH = 0;
    // Whether those rows were drawn in the monospace family, so a press is
    // measured against the same advances they were laid out with.
    bool lastMono = false;
    // display_map.rs: the box each logical line was last laid out in. Soft
    // wrap makes them uneven — a line that wrapped is two of those boxes tall
    // or more — so a press cannot be turned into a row by arithmetic, and
    // neither can the caret's y. Empty when nothing wrapped, where the
    // arithmetic is right and cheaper. Sized before the rows are built, so
    // the pointers the elements are handed stay put for the frame.
    Vec<Bounds> rowBoxes;
    // DiagnosticSet: what a provider published over this document, in
    // document order. Rust keeps a SumTree so a range query is a seek; there
    // are tens of these on a screen, so this is the flat list the painter and
    // the hover both walk.
    Vec<Diagnostic> diagnostics;
    // The one the pointer is over, and where it was — `state.diagnostic_
    // popover()` in Rust, which the overlay turns into a popover. -1 is none.
    int hoverDiagnostic = -1;
    float hoverDiagnosticX = 0;
    float hoverDiagnosticY = 0;
    // The symbol the pointer is resting on, and who is asked about it —
    // `hover_popover` in Rust. The range is the word it was asked for, which
    // is what keeps it from asking again while the pointer stays inside it.
    HoverFn hoverProvider = nullptr;
    void* hoverData = nullptr;
    Str hoverText = {};
    Selection hoverRange = {};
    float hoverX = 0;
    float hoverY = 0;
    // The 150 ms Rust waits before asking, and only when nothing is showing:
    // a popover already up moves from word to word with no delay at all,
    // which is `should_delay = hover_popover.is_none()`. A frame is the clock
    // here, so this is when the question may be asked and what it is about.
    double hoverDueAt = 0;
    Selection hoverPending = {};
    bool hoverAsked = true;
    // The colours a provider found in the document, and who is asked. Rust
    // asks on a timer after every edit and diffs the answer; this asks the
    // frame after the edit, which is the same answer a frame sooner.
    DocumentColorFn documentColorProvider = nullptr;
    void* documentColorData = nullptr;
    Vec<DocumentColor> documentColors;
    bool documentColorsDirty = true;
    // The code action menu, and who fills it — cmd-. / ctrl-. asks whatever
    // is selected. Rust asks every registered provider and puts the answers
    // in one list.
    CodeActionSession codeActions;
    // Rust holds a `Vec<Rc<dyn CodeActionProvider>>` and asks every one of
    // them, putting the answers in one list; each item remembers which
    // provider it came from so performing it goes back to that one.
    Vec<CodeActionProviderEntry> codeActionProviders;
    // The first slot, under the name the one-provider callers already use.
    // Writing it is the same as registering one provider.
    CodeActionFn codeActionProvider = nullptr;
    void* codeActionData = nullptr;
    // The inline suggestion in front of the caret, and who is asked for one.
    // A field with no provider never shows one.
    InlineCompletionFn inlineCompletionProvider = nullptr;
    void* inlineCompletionData = nullptr;
    // CompletionProvider::inline_completion_debounce. Rust defaults to 300
    // ms but a provider may choose another duration.
    float inlineCompletionDebounceMs = kInlineCompletionDebounceMs;
    InlineCompletion inlineCompletion;
    // The semantic tokens a provider published, and who is asked. Rust
    // debounces the request 100 ms after an edit and diffs the answer; this
    // asks the frame after the edit, like the document colours beside it.
    SemanticTokensFn semanticTokensProvider = nullptr;
    void* semanticTokensData = nullptr;
    // The legend the provider's `token_type` indexes into. Rust asks the
    // provider for it every time; a provider here declares it once beside
    // the function.
    const Str* semanticLegend = nullptr;
    int nSemanticLegend = 0;
    Vec<SemanticSpan> semanticTokens;
    bool semanticTokensDirty = true;
    // Go to definition: who is asked, what the last question found, and the
    // host's hook for showing a document itself. A state with no provider
    // never underlines anything and never answers the action.
    DefinitionFn definitionProvider = nullptr;
    void* definitionData = nullptr;
    ShowDocumentFn showDocument = nullptr;
    void* showDocumentData = nullptr;
    HoverDefinition hoverDef;
    // The completion menu, and who fills it. A state with no provider never
    // opens one, which is every field that is not a code editor.
    CompletionSession completion;
    CompletionFn completionProvider = nullptr;
    void* completionData = nullptr;
    // set_overlay_action_handler: a host drawing its own popover takes the
    // keys before the editor's own menu does.
    OverlayActionFn overlayAction = nullptr;
    void* overlayActionData = nullptr;
    // completion_inserting / silent_replace_text: an edit the editor made on
    // the reader's behalf — accepting an item, performing an action — is not
    // typing, so it does not open a menu or ask for a suggestion.
    bool silentReplace = false;
    // is_completion_trigger and completionItem/resolve, both optional: the
    // built-in rule and no resolving are what a provider that names neither
    // gets.
    CompletionTriggerFn completionTrigger = nullptr;
    CompletionResolveFn completionResolve = nullptr;
    // CompletionMenuOptions::max_width, which the menu is drawn to.
    float completionMenuMaxW = kCompletionMenuMaxW;
    // The box the rows were laid out in as a whole, which is the scrolled
    // height once soft wrap has had its say.
    Bounds contentBox = {};
    // input_bounds: the whole field, what a press outside the run maps against.
    Bounds inputBounds = {};
    // input/popovers::Popover's trigger and laid-out surface. RangeOut fills
    // the first from the active symbol/diagnostic; BoundsOut fills the second
    // after Positioner has moved it. Together they keep the hover alive while
    // the pointer crosses from the editor into its popover.
    Selection popoverTriggerRange = {};
    Bounds popoverTriggerBounds = {};
    Bounds popoverBounds = {};
    // scroll_handle: how far the field has scrolled under its own box, and
    // the box it scrolls inside. Positive-down, as El::ScrollY takes it; Rust
    // keeps the same pair, negative, on a ScrollHandle.
    float scrollX = 0;
    float scrollY = 0;
    float viewW = 0;
    float viewH = 0;
    // Where the caret was last painted, in window coordinates. `cursor_
    // layout()` in Rust, which is what a completion menu and a hover popover
    // are placed under.
    float caretWinX = 0;
    float caretWinY = 0;
    // The caret's x inside the run, measured when it was last painted, and
    // the whole scrolled height. `last_layout` is what Rust reads them off.
    float caretX = 0;
    float contentW = 0;
    float contentH = 0;
    // Suppressed while set_value writes the text, so a programmatic write is
    // not reported as the user having typed.
    bool emitEvents = true;
    // ime_marked_range: the text the input method has put in provisionally,
    // which is the document's until the composition commits or is abandoned.
    // Rust keeps an Option; `imeMarking` is the Some.
    Selection imeMarked = {};
    bool imeMarking = false;
    // preferred_column: the column a vertical move aims for, so walking down
    // past a short line and back up returns to where it started. -1 for none.
    int preferredColumn = -1;
    // The x Rust remembers beside it, which is what a walk over *display*
    // rows aims at — a column means nothing halfway through a wrapped line.
    // -1 for none; cleared by every move that is not part of the walk.
    float preferredX = -1;
    // Which side of a soft-wrap boundary the live caret belongs to. The byte
    // offset alone cannot distinguish the previous row's end from the next
    // row's start.
    bool cursorLineEndAffinity = false;

    ~InputState();
};

// Which way a move went, which decides whether scroll_to may pull the view
// back the other way. Rust's MoveDirection.
enum class InputMoveDir : uint8_t {
    None,
    Up,
    Down
};

// scroll_to: the offset that brings the caret into view, from where the field
// is now. `caretY` is the top of the caret's line and `caretX` its position
// across the run; the answer keeps the caret a line's clearance from either
// edge and never scrolls past the content. A move that went up will not be
// answered with a downward scroll, and the other way about — Rust clamps the
// same way, so a vertical walk does not fight itself.
void InputScrollToCaret(InputState* s, float caretX, float caretY,
                        InputMoveDir dir);
// empty_bottom_height / cursor_surrounding_padding. `overrideRows` and
// `overrideLines` are -1 for None. The editor example's status bar is
// what turns the overrides.
float InputEmptyBottomHeight(bool isCodeEditor, int overrideRows,
                             float viewportH, float lineH);
float InputCursorSurroundingPadding(bool isAutoGrow, int overrideLines,
                                    int visibleLines, float lineH);
// A negative `caretX` leaves the sideways offset alone, which is what
// scrolling to something that is not the caret wants: a search match is a
// row to bring into view, and how far across it sits is not measurable
// outside a paint.
void InputScrollToOffset(InputState* s, int offset, InputMoveDir dir);
// The same, for wherever the caret is now: the row it is on and the x the
// last paint measured.
void InputScrollToCursor(InputState* s, InputMoveDir dir);

// value() / the NUL-terminated view of it. Neither allocates.
Str InputValue(const InputState* s);
// The line index over the document — InputState::lineStarts, rebuilt here
// when docVersion moved. Every answer the flat Rope* helpers scan the
// document for is a lookup against it; use these wherever an InputState is
// in hand and the Rope* spelling only where there is none. The pointer they
// take is const because reading a lazily filled cache is a read.
const Vec<int>& InputLineStarts(const InputState* s);
int InputLinesLen(const InputState* s);
int InputLineStartOffset(const InputState* s, int row);
Str InputSliceLine(const InputState* s, int row);
RopePoint InputOffsetToPoint(const InputState* s, int offset);
const char* InputCStr(const InputState* s);
// unmask_value(): the text with the mask's separators taken back out.
Str InputUnmaskValue(Arena* a, const InputState* s);
// selected_text().
Str InputSelectedValue(const InputState* s);
bool InputIsMultiLine(const InputState* s);
bool InputIsSingleLine(const InputState* s);
bool InputIsEditable(const InputState* s);
// is_copyable: whether the selection may leave the field. A masked one may
// not — what it shows is not what it holds, and a copy or a cut would put
// what it holds on the clipboard.
bool InputIsCopyable(const InputState* s);
// cursor(): the caret offset, which end of the selection depends on which way
// it was dragged.
int InputCursor(const InputState* s);
// cursor_position(): the row and column the caret is on.
RopePoint InputCursorPosition(const InputState* s);

// set_value(): replaces the text, resets the selection to the end, and clears
// the undo history — the programmatic write, not an edit.
void InputSetValue(InputState* s, Str value);
// replace_all(): the same replacement, but recorded so it can be undone.
void InputReplaceAll(InputState* s, App* app, Window* win, Str value);
void InputSetPlaceholder(InputState* s, Str value);
void InputSetMaskPattern(InputState* s, MaskPattern pattern);
// clean(): empties the field.
void InputClean(InputState* s, App* app, Window* win);
// insert() / replace(): a programmatic edit, recorded as one atomic step.
void InputInsert(InputState* s, App* app, Window* win, Str value);

// previous_boundary / next_boundary: one character either way.
int InputPreviousBoundary(const InputState* s, int offset);
int InputNextBoundary(const InputState* s, int offset);
// start_of_line / end_of_line, and the two word boundaries movement.rs asks
// for. A single-line field answers 0 and len for the first pair, as Rust does.
// A code editor that soft-wraps answers the *visual* row's ends first and the
// logical line's on a second press, which is what `soft_wrap &&
// is_code_editor()` gates in Rust; the window is what the wrapped row is
// measured against, and without one the answer is the logical line.
int InputStartOfLine(const InputState* s, Window* win = nullptr);
int InputEndOfLine(const InputState* s, Window* win = nullptr);
int InputPreviousStartOfWord(const InputState* s);
int InputNextEndOfWord(const InputState* s);

// move_to(): drops the selection and puts the caret at `offset`.
void InputMoveTo(InputState* s, App* app, Window* win, int offset);
void InputMoveToWithAffinity(InputState* s, App* app, Window* win, int offset,
                             bool lineEndAffinity);
// select_to(): drags the live end of the selection to `offset`.
void InputSelectTo(InputState* s, App* app, Window* win, int offset);
void InputSelectToWithAffinity(InputState* s, App* app, Window* win, int offset,
                               bool lineEndAffinity);
void InputSelectAll(InputState* s, App* app, Window* win);
void InputUnselect(InputState* s, App* app, Window* win);
void InputSetSelectedRange(InputState* s, App* app, Window* win, int a, int b);
// selection.rs: what a double and a triple click take.
void InputSelectWord(InputState* s, App* app, Window* win, int offset);
void InputSelectLine(InputState* s, App* app, Window* win, int offset);

// The actions state.rs binds, one per `impl` method there. The window turns a
// key chord into one of these with InputActionForKey and hands it over — which
// is what GPUI's action dispatch does for the focused element.
enum class InputAction : uint8_t {
    None,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    MoveHome,
    MoveEnd,
    MoveToStart,
    MoveToEnd,
    MoveToPreviousWord,
    MoveToNextWord,
    MovePageUp,
    MovePageDown,
    SelectLeft,
    SelectRight,
    SelectUp,
    SelectDown,
    SelectAll,
    SelectToStart,
    SelectToEnd,
    SelectToStartOfLine,
    SelectToEndOfLine,
    SelectToPreviousWordStart,
    SelectToNextWordEnd,
    Backspace,
    Delete,
    DeleteToBeginningOfLine,
    DeleteToEndOfLine,
    DeleteToPreviousWordStart,
    DeleteToNextWordEnd,
    Enter,
    Escape,
    // indent.rs IndentInline / OutdentInline, which is what tab and shift-tab
    // are bound to inside a field. A single-line input, or one whose layout
    // has nothing to indent, does not handle them — that is Rust's
    // cx.propagate(), and here it is `false` back out of InputPerform, which
    // leaves the keystroke to the window's focus ring.
    IndentInline,
    OutdentInline,
    // The block pair, ctrl-] / ctrl-[. They work on whole lines: a caret
    // sitting halfway along one still moves the line, where the inline pair
    // would have put the tab where the caret is.
    Indent,
    Outdent,
    Copy,
    Cut,
    Paste,
    Undo,
    Redo,
    // ctrl-f and ctrl-h, which open the find bar over the field — the second
    // with its replace row already out. Rust binds both in the input's key
    // context and both do nothing on a field that is not searchable.
    Search,
    Replace,
    // cmd-. / ctrl-.: the code action menu over whatever is selected.
    ToggleCodeActions
};

// `platform` is Command on macOS and the Windows key elsewhere;
// `KeySecondary(ctrl, platform)` is the shortcut modifier the copy, paste and
// undo chords are written with.
InputAction InputActionForKey(const InputState* s, int vk, bool shift,
                              bool ctrl, bool alt, bool platform = false);
// True when the input consumed it, so the window does not also treat Enter as
// a click on the focused element. `shift` is Enter's modifier, which decides
// whether a submit-on-enter textarea inserts a newline.
bool InputPerform(InputState* s, App* app, Window* win, InputAction action,
                  bool shift);

// ─── completion ───────────────────────────────────────────────────────────

// The word being typed in front of the caret: where it starts, and what it
// is. A caret that is not after a word answers an empty query starting where
// it stands, which is what a trigger character like `.` completes on.
Str InputCompletionQuery(const InputState* s, int* startOut);
// Ask the provider and open the menu if it answered anything. Rust does this
// from the editor's own `on_input` when the typed character is a trigger, and
// from ctrl-space; `force` is the second, which asks whatever was typed.
void InputRequestCompletion(InputState* s, App* app, Window* win, bool force);
// Escape, a click elsewhere, or an edit that leaves nothing to complete.
void InputDismissCompletion(InputState* s);
// Accept the selected item: the query range is replaced by its insert text.
void InputAcceptCompletion(InputState* s, App* app, Window* win);
// The four keys the menu takes while it is up, answering whether it took the
// chord — `CompletionMenu::handle_action`.
bool InputCompletionAction(InputState* s, App* app, Window* win,
                           InputAction action);
// ShowCompletions: ctrl-space asks whatever the caret is on, which is Rust's
// second way in beside a trigger character.
void InputShowCompletions(InputState* s, App* app, Window* win);

// ─── document colours ─────────────────────────────────────────────────────

// Ask the provider what colours the document names, if it has changed since
// it was last asked. The row builder does this; a caller that publishes its
// own set writes `documentColors` and leaves the provider null.
void InputUpdateDocumentColors(InputState* s);

// ─── code actions ─────────────────────────────────────────────────────────

// ─── the overlay seam (input/editor/lsp/overlay.rs) ──────────────────────
//
// Rust keeps only the *state* of the two menus in the editor and hands the
// drawing to whoever is hosting it: an application can present its own items,
// draw its own popover, and take the keys the menu would have taken. This
// tree draws both menus itself — `component::Highlighter` does, under the
// caret — and everything below is the same seam beside it, so a host that
// wants its own can have one.

// present_completion_items: the host pushing a list in, rather than the
// editor pulling one from a provider. The items are the caller's and outlive
// the menu, the way a provider's do.
void InputPresentCompletionItems(InputState* s, int triggerStart, Str query,
                                 const CompletionItem* items, int n);
// present_code_actions, the same for the other menu.
void InputPresentCodeActions(InputState* s, const CodeActionItem* items, int n);
// present_hover / present_diagnostic / clear_diagnostic_popover: the two
// popovers, put up by the host rather than found by the editor.
void InputPresentHover(InputState* s, Selection symbolRange, Str text);
void InputPresentDiagnostic(InputState* s, int index);
void InputClearDiagnosticPopover(InputState* s);
// route_overlay_action: whichever menu is open takes the action. True when
// one did.
bool InputRouteOverlayAction(InputState* s, App* app, Window* win,
                             InputAction action);
// dismiss_completion_overlay / dismiss_code_action_overlay /
// dismiss_lsp_overlays.
void InputDismissLspOverlays(InputState* s);
// is_context_menu_open: either menu.
bool InputIsContextMenuOpen(const InputState* s);
// insert_completion: write one item in over `fallback`, which is what the
// menu's own accept does with the range the query occupied. A host that drew
// its own popover calls this with the item the reader picked.
void InputInsertCompletion(InputState* s, App* app, Window* win,
                           const CompletionItem* item, Selection fallback);

// The documentation of the item the selection is on, resolved through the
// provider the first time it is looked at. Empty when there is none, and
// what the item already carried when it came with some.
Str InputCompletionDocumentation(InputState* s);

// schedule_inline_completion: the typing stopped, so the provider may be
// asked once the debounce has run. Called by every edit, which is also what
// drops the suggestion that was showing.
void InputScheduleInlineCompletion(InputState* s);
// The frame's half of that clock: ask the provider if the debounce is up and
// nothing has moved. True when it wants another frame to keep waiting.
bool InputUpdateInlineCompletion(InputState* s, bool menuOpen);
// has_inline_completion / clear_inline_completion / accept_inline_completion.
bool InputHasInlineCompletion(const InputState* s);
void InputClearInlineCompletion(InputState* s);
bool InputAcceptInlineCompletion(InputState* s, App* app, Window* win);

// CodeActionProvider registration. The one-provider form is the field above;
// this is what a second and a third go through.
void InputAddCodeActionProvider(InputState* s, CodeActionFn fn, void* data,
                                CodeActionPerformFn perform = nullptr);

// Lsp::update: what the document changing under the LSP layer means — the
// caches that are derived from it are asked for again. Rust calls it from
// the edit path; the frame builder is where it is called here, since the
// answers are wanted for the frame being built.
void InputLspUpdate(InputState* s);
// Lsp::reset: everything the layer had cached or was showing, dropped.
void InputLspReset(InputState* s);

// Lsp::update's semantic half: ask the provider again when the document has
// changed under it. Nothing happens without a provider, which is every field
// that is not a code editor.
void InputUpdateSemanticTokens(InputState* s);

// handle_hover_definition: ask the definition provider about the offset the
// pointer is over, unless the last answer already covers it. What it finds is
// underlined in the editor and takes the hand cursor.
void InputHoverDefinition(InputState* s, int offset);
// The other half: the modifier came up, or the pointer left the field.
void InputClearHoverDefinition(InputState* s);
// handle_click_hover_definition: a secondary-click inside a symbol the hover
// found goes to its first location. True when it did, which is what keeps the
// same press from also moving the caret.
bool InputClickDefinition(InputState* s, App* app, Window* win, int offset,
                          bool secondary);
// The GoToDefinition action, which goes by the last thing a hover found
// rather than by what is under the pointer now — the pointer has moved on by
// the time a menu row is picked.
void InputGoToDefinition(InputState* s, App* app, Window* win);
// `can_go_to_definition`: whether the field has a provider at all, which is
// what greys the menu row out.
bool InputCanGoToDefinition(const InputState* s);
// go_to_definition: the host first, then the browser for an external uri, and
// otherwise the selection moved to the target inside this document.
void InputFollowDefinition(InputState* s, App* app, Window* win,
                           const DefinitionLink& link);

// ToggleCodeActions: ask the provider about what is selected and open the
// menu on what it offers. Nothing offered leaves the menu down.
void InputToggleCodeActions(InputState* s, App* app, Window* win);
void InputDismissCodeActions(InputState* s);
// Perform the selected action: its range is replaced by its text, as one
// undo step, and the menu goes away.
void InputPerformCodeAction(InputState* s, App* app, Window* win);
// The four keys the menu takes while it is up — `CodeActionMenu::
// handle_action`, which is the completion menu's, over the other list.
bool InputCodeActionAction(InputState* s, App* app, Window* win,
                           InputAction action);

// replace_text_in_range: the one path every edit goes through. A null range is
// the current selection. Returns false when the edit was rejected — readonly,
// or a mask or validator that said no.
bool InputReplaceTextInRange(InputState* s, App* app, Window* win,
                             const Selection* range, Str newText);
// Input methods count in UTF-16 on both platforms that have one to talk to;
// a field counts in bytes. These are the two directions across. An offset
// past the end clamps to it, which is what a platform handing over a stale
// range needs.
int Utf8OffsetToUtf16(Str s, int u8);
int Utf16OffsetToUtf8(Str s, int u16);
// marked_text_range(): what the input method is still deciding, if anything.
bool InputMarkedRange(const InputState* s, Selection* out);
// replace_and_mark_text_in_range(): the input method's provisional insert.
// A null `range` means "over the mark, or the selection if there is none",
// which is what makes each keystroke of a composition replace the last one.
// `sel` is where the caret should sit inside the new text, in bytes from its
// start; null puts it at the end. Empty text ends the composition.
void InputReplaceAndMarkText(InputState* s, App* app, Window* win,
                             const Selection* range, Str newText,
                             const Selection* sel);
// unmark_text(): the composition is over and what it left stands. Commits the
// undo transaction the composition opened, so the whole of it undoes at once.
void InputUnmarkText(InputState* s, App* app, Window* win);
// The typed character, once the platform has decoded it.
void InputTypeChar(InputState* s, App* app, Window* win, uint32_t ch);

// ─── the find bar, crates/base/src/input/editor/search.rs ─────────────────

// open_search: the bar opens over the field, with whatever is selected as
// its first query and the match nearest the top of the view as its first
// match. A field that is not searchable ignores it.
void InputOpenSearch(InputState* s, App* app, Window* win, bool replaceMode);
uint64_t InputSearchActivationRevision(const InputState* s);
void InputCloseSearch(InputState* s, App* app, Window* win);
// is_replaceable(): the field allows it and is editable right now.
bool InputIsReplaceable(const InputState* s);
void InputSetSearchReplaceMode(InputState* s, App* app, Window* win, bool on);
void InputSetSearchQuery(InputState* s, App* app, Window* win, Str query,
                         bool insensitive);
// next_search_match / previous_search_match: the cursor moves, wrapping, and
// the view follows. False when nothing matched.
bool InputSearchNext(InputState* s, App* app, Window* win, Selection* out);
bool InputSearchPrev(InputState* s, App* app, Window* win, Selection* out);
// replace_current_search_match: the match under the cursor becomes the
// replacement, and the cursor is left on what is now under it.
bool InputSearchReplaceOne(InputState* s, App* app, Window* win, Str with);
// replace_all_search_matches: every match, back to front so the earlier
// offsets stay good. Answers how many there were.
int InputSearchReplaceAll(InputState* s, App* app, Window* win, Str with);
// update_search: the matcher takes the text as it is now. Every edit goes
// through this, so a find bar left open follows what is typed.
void InputUpdateSearch(InputState* s);

// on_focus / on_blur. Points win->input at this field, starts its caret, and
// emits the event; blurring commits the typing session, which is what makes a
// later undo stop at the right place.
void InputFocus(InputState* s, App* app, Window* win);
void InputBlur(InputState* s, App* app, Window* win);

// index_for_mouse_position: the offset a press at (x, y) lands on, against the
// run the element last painted.
int InputIndexForPosition(const InputState* s, PaintCtx* ctx, float x, float y,
                          bool* lineEndAffinity = nullptr);
// The fold chevron a press at (x, y) landed on, or -1. The boxes are the ones
// the last frame's gutter left behind.
int InputFoldIconAt(const InputState* s, float x, float y);
// Open or close the fold that starts on `line`, and redraw.
void InputToggleFold(InputState* s, App* app, Window* win, int line);
// unfold_at: open every fold that hides `position`, and only those. A fold
// keeps its own first and last line visible, so a position on either opens
// nothing; nested folds all open, since opening only the outermost would
// leave the position hidden; sibling folds stay shut and the candidates stay,
// so the gutter can fold the ranges again. What a caller reveals a target
// with before moving the caret to it, since the caret stops at a fold
// boundary. Rust takes an lsp `Position`; the row and byte column are what
// this tree spells a document position as. Returns whether any fold opened.
bool InputUnfoldAt(InputState* s, App* app, Window* win, RopePoint position);
// apply_highlighter_fold_candidates: what the highlighter found, taken only
// when this field is a code editor with folding on.
void InputSetFoldCandidates(InputState* s, const FoldRange* ranges, int n);
// The field a press at (x, y) landed in, or null.
InputState* InputAtPosition(PaintCtx* ctx, float x, float y);

// crates/base/src/slider.rs. SliderValue is Rust's enum — `Single(f32)` or
// `Range(f32, f32)` — flattened into the pair plus the flag that says which
// variant this is. `hi` is `end()`, the one a single-value slider uses.
struct SliderValue {
    float lo = 0;
    float hi = 0;
    bool range = false;

    float Start() const { return range ? lo : hi; }
    float End() const { return hi; }
};

inline SliderValue SliderSingle(float v) {
    return {0, v, false};
}
inline SliderValue SliderRange(float lo, float hi) {
    return {lo, hi, true};
}
// SliderValue::clamp.
SliderValue SliderValueClamp(SliderValue v, float min, float max);
// SliderValue::set_start / set_end: a range keeps its ends in order.
void SliderValueSetStart(SliderValue* v, float value);
void SliderValueSetEnd(SliderValue* v, float value);

// SliderScale. Logarithmic gives finer control near the low end, which is what
// a volume or a playback speed wants.
enum class SliderScale : uint8_t {
    Linear,
    Logarithmic
};

// SliderEvent. Change comes on every press and every move that shifts the
// value; Release comes once, when the button goes up after one of those.
enum class SliderEventKind : uint8_t {
    Change,
    Release
};

struct SliderEvent {
    SliderEventKind kind = SliderEventKind::Change;
    SliderValue value = {};
};

// SliderState: what a slider is between frames, the way InputState is what an
// input is. Rust keeps it in an `Entity<SliderState>` and the element closures
// capture that handle; an element here names its state with `El::BindSlider`
// and the window applies the same behavior, which is how InputState works.
// `onChange` is `cx.subscribe(&state, |ev: &SliderEvent| ...)`.
struct SliderState {
    float min = 0;
    float max = 100;
    float step = 1;
    SliderValue value = {};
    // percentage: Range<f32>. A single-value slider only uses `hi`, with `lo`
    // pinned at 0, which is what Rust's `0.0..percentage` says.
    float pctLo = 0;
    float pctHi = 0;
    // The box the value maps against, recorded when the slider is pressed.
    Bounds bounds = {};
    SliderScale scale = SliderScale::Linear;
    // Set by a press, cleared by the release, so a release with no press
    // behind it emits nothing.
    bool dragging = false;
    // Which end of a range the press took, for the moves that follow. Rust
    // carries it in the DragThumb payload, which a drag here has no room for.
    bool dragStart = false;
    Listener onChange = {};
};

// SliderState::new().min(..).max(..).step(..).scale(..).default_value(..),
// which is a builder chain in Rust and one call here, so a view can write its
// slider as a field initializer.
SliderState SliderStateNew(float min, float max, SliderValue value,
                           float step = 1,
                           SliderScale scale = SliderScale::Linear);

// `.min()` / `.max()`, which re-derive the thumb position. Rust panics when a
// logarithmic slider is given a min <= 0 or a max <= min; there are no
// exceptions here, so the limits are pushed to the nearest usable pair
// instead — a widget that draws itself wrong is better than one that exits.
void SliderSetLimits(SliderState* s, float min, float max);
// `.step()`, the quantum a value snaps to.
void SliderSetStep(SliderState* s, float step);
// `.scale()`.
void SliderSetScale(SliderState* s, SliderScale scale);
// `.default_value()` / `set_value()`.
void SliderSetValue(SliderState* s, SliderValue v);
// set_bounds, the box a position maps against.
inline void SliderSetBounds(SliderState* s, Bounds b) {
    s->bounds = b;
}

// percentage_to_value / value_to_percentage.
float SliderPctToValue(const SliderState* s, float pct);
float SliderValueToPct(const SliderState* s, float value);
// update_thumb_pos: the percentages that follow from the value.
void SliderUpdateThumbPos(SliderState* s);

// update_value_by_position. `isStart` moves the low end of a range. Rust ends
// this with `cx.emit(SliderEvent::Change)`; the window here raises the event
// instead, so this returns whether the value actually moved.
bool SliderUpdateByPosition(SliderState* s, Axis axis, Point pos, bool isStart);
// Which end of a range a press at `pos` takes, by the midpoint between the two
// thumbs — the `is_start` arm of SliderTrack's mouse-down handler.
bool SliderIsStartAt(const SliderState* s, Axis axis, Point pos);
// handle_release: true when a Release event is due.
bool SliderHandleRelease(SliderState* s);
// The a11y half of slider.rs that is reachable here: `on_a11y_action` binds
// Increment and Decrement, and this is what the two of them do to the value.
// There is no accessibility layer in this tree, so the arrows are what carries
// them — and the arrows are what a keyboard user needs either way. `dir` is +1
// or -1, and `isStart` picks which end of a range moves. Answers whether the
// value moved.
bool SliderStepBy(SliderState* s, int dir, bool isStart);

struct Overlay {
    int kind = 0; // 0 none, 1 dialog, 2 sheet
    char title[128] = {};
    char body[2048] = {};
};

struct MenuState {
    bool open = false;
    float x = 0, y = 0;
    char items[8][32] = {};
    int nItems = 0;
    int clickBase = 0;
};

struct WinSize {
    float dipW = 0;
    float dipH = 0;
    int pxW = 0;
    int pxH = 0;
};

float PxToDip(PaintCtx* ctx, int px);
int DipToPx(PaintCtx* ctx, float dip);

Size MeasureText(PaintCtx* ctx, Str s, float fontSize, float maxW,
                 bool wrap = false, int weight = 0, float lineH = 0);
// One run drawn from its baseline rather than from the top of its line box,
// which is the point an SVG <text> names. Its one caller is drawops.cpp, and
// it lives here rather than there because the run has to go through the
// frame's measurement cache: the cache is what holds the shaped run after the
// walk that drew it returns, and the scene replays a text primitive by the
// pointer it recorded.
void DrawTextBaseline(PaintCtx* ctx, Str s, float x, float baselineY,
                      float fontSize, Rgba color, int weight = 0);
void TextMeasBeginFrame(PaintCtx* ctx);
void TextMeasEndFrame(PaintCtx* ctx);
void TextMeasClear(PaintCtx* ctx);
// The inverse of TextIndexAt: where a UTF-8 offset sits in a run once it has
// been laid out and wrapped. `outY` is the top of the visual row the offset
// is on and `outH` that row's height. False when there was nothing to
// measure against.
//
// `mono` and `lineHeight` have to be the ones the run was painted with:
// Consolas advances differently from the proportional face, so measuring a
// code editor's row without them answers for a line of text that was never
// drawn.
bool TextPointAt(PaintCtx* ctx, Str s, float fontSize, float maxW, bool wrap,
                 int off, float* outX, float* outY, float* outH,
                 bool mono = false, float lineHeight = 0,
                 bool lineEndAffinity = true);
int TextIndexAt(PaintCtx* ctx, Str s, float fontSize, float maxW, bool wrap,
                float relX, float relY, bool mono = false,
                float lineHeight = 0);
// `weight` and `lineH` have to be the ones the run was laid out with, or the
// rects come back measured against a different font: the mono family is a
// weight sentinel here, so a code row measured with 0 drifts further from the
// glyphs the further along the line it is.
void PaintTextRange(PaintCtx* ctx, Str s, float fontSize, float maxW, bool wrap,
                    uint8_t weight, float lineH, float x, float y, int u8a,
                    int u8b, Rgba color);
void PaintTextUnderline(PaintCtx* ctx, Str s, float fontSize, float maxW,
                        bool wrap, uint8_t weight, float lineH, float x,
                        float y, int u8a, int u8b, Rgba color,
                        bool wavy = false);
// The taffy tree a window lays out in, kept between frames so taffy's own
// per-node caches are. It is reconciled against the element tree rather than
// rebuilt: an element whose style and content are the ones its node already
// has is not laid out again, and a subtree of them is not walked at all.
// See the block above `LayoutNode` in gpui.cpp, and `__layout_reuse=off`
// to take it back out. `GPUI_LAYOUT_REUSE=off` is the same switch if argv
// did not set it.
struct LayoutCache;

// Consumed by GpuiTakeRuntimeArgs: `__layout_reuse=off|on` (also 0|1).
// Invalid values are still consumed and leave the current/default choice.
bool LayoutReuseTakeArg(Str arg);
// Whether the taffy tree is kept between frames. Default on.
bool LayoutReuseOn();

LayoutCache* LayoutCacheNew();
void LayoutCacheFree(LayoutCache* lc);

// What the last frame's reconcile did, for GPUI_FRAME_BENCH.
struct LayoutCacheStats {
    // Nodes the element tree asked for.
    int nodes = 0;
    // Of those, the ones that had to be made — a page that has just changed
    // makes them all, a page that has not makes none.
    int made = 0;
    int dropped = 0;
    // Nodes told about a new style, and measured leaves told their content
    // moved. Each of those is a node taffy has to lay out again, and its
    // ancestors with it.
    int restyled = 0;
    int remeasured = 0;
    // Times InsertNode actually `new`'d a NodeData this pass. Recycled
    // slots do not count. `__layout_reuse=off` still rebuilds, but this
    // stays 0 after the tree has reached its peak size.
    int allocs = 0;
};

LayoutCacheStats LayoutCacheLastStats(const LayoutCache* lc);
// Live taffy nodes in the cache, including ones the last reconcile no longer
// has an element for. A virtualized list that leaks on scroll grows this.
int LayoutCacheNodeCount(const LayoutCache* lc);
// Taffy node slots that have ever been allocated, including dead ones on
// the free list. A virtualized list that InsertNode's on every scroll
// grows this even when the live count does not.
int LayoutCacheSlotCount(const LayoutCache* lc);
// The scratch cache MeasureEl and a cache-less LayoutEl share, given back at
// AppFree.
void LayoutScratchFree();

// `lc` is the window's cache, and the reason layout is cheap on a frame that
// changed little. A caller without one — a test, a measurement — passes none
// and gets a scratch cache that is reset per call, which is what every caller
// got before there was a cache at all.
void LayoutEl(PaintCtx* ctx, El* e, float x, float y, float availW,
              float availH, float inheritFont, Rgba inheritFg,
              LayoutCache* lc = nullptr);
// AnyElement::layout_as_root(size(MinContent, MinContent)): what one element
// wants to be, laid out on its own and away from the tree it will go into.
// A virtualized list measures a row this way and then places every row at
// that size, since it cannot ask the layout what the rows it did not build
// would have come out as.
//
// It runs the same pass `LayoutEl` does and leaves the boxes on the element,
// so the caller may either read the returned size or go on to use `e`. It has
// a layout cache of its own, reset per call, so a measure in the middle of a
// frame does not disturb the window's.
Size MeasureEl(PaintCtx* ctx, El* e, float inheritFont = 0,
               Rgba inheritFg = {});
void PaintEl(PaintCtx* ctx, El* e);
int HitTest(PaintCtx* ctx, float x, float y);
const HitRect* HitTestRect(PaintCtx* ctx, float x, float y);
// The scroll box of an id as the frame before this one painted it — the
// `bounds()` a Rust `ScrollHandle` remembers from its last layout. Null for an
// id nothing painted, and for 0, which several boxes share.
const ScrollRect* WindowLastScrollRect(const Window* win, int id);
// The topmost element under the pointer that takes a drag of this kind, of
// those that asked for one at all. A drop target that does not want what is
// being dragged is not in the way of one that does.
const HitRect* HitTestDrop(PaintCtx* ctx, float x, float y, Str kind);
const ScrollRect* HitScrollRect(PaintCtx* ctx, float x, float y);
int TextHitOffsetAt(PaintCtx* ctx, float x, float y, bool nearest);
// The same, confined to one selection scope (-1 for any), and reporting the
// scope the point landed in. `minLayer` skips runs painted on a stacking
// layer below it, so a popup does not inherit the I-beam of the page.
int TextHitOffsetIn(PaintCtx* ctx, float x, float y, bool nearest, int scope,
                    int* outScope, int minLayer = 0);
int CopyTextHits(PaintCtx* ctx, int selA, int selB, char* out, int cap);
// `fmt` is what each run contributes: its rendered text, or — where the run
// carries a SelSource — the Markdown it was rendered from.
int CopyTextHitsIn(PaintCtx* ctx, int selA, int selB, int scope, char* out,
                   int cap, SelectionFormat fmt = SelectionFormat::Plain);
int CopyTextHitsInEntity(PaintCtx* ctx, int selA, int selB, int scope,
                         EntityId owner, char* out, int cap,
                         SelectionFormat fmt = SelectionFormat::Plain);
// points_for_multi_click: the document range a press of `clickCount` selects
// under (x, y) — 2 takes the word, 3 or more the whole run — in the same
// offsets TextHitOffsetAt and CopyTextHits speak. False for a single click,
// or when no selectable text is there.
bool TextMultiClickRange(PaintCtx* ctx, float x, float y, int clickCount,
                         int* outA, int* outB);
bool TextMultiClickRangeIn(PaintCtx* ctx, float x, float y, int clickCount,
                           int scope, int* outA, int* outB, int* outScope);
int HashClickId(Str s);

// Which of the three fills a box paints. GPUI resolves this by refining the
// hovered style and then the active one over the base, so the pressed fill
// wins where a box has both and is being held. Split out from the paint pass
// because it is the whole of what `Style::activeBg` means and the pointer
// cannot be driven from a test.
//
// Both states need a click id of their own: without one the box would match a
// `hoverId` / `activeId` of 0, which is what "nothing is hovered" and
// "nothing is held" are spelled as.
enum class BoxFill : uint8_t {
    Base,
    Hover,
    Active
};
BoxFill BoxFillFor(bool hasActiveBg, bool hasHoverBg, int clickId, int activeId,
                   int hoverId);

// Whether a release makes a click. GPUI holds the press as
// `pending_mouse_down` and fires on_click from the mouse-up handler, where it
// asks three things: that a press is waiting at all, that the button coming
// up is the one that went down, and that the element under the pointer is the
// one the press landed on. A press that slid off somewhere else is no click,
// and a drag takes the release the click would have had.
//
// `pending` is Rust's Option being Some: a press the scrollbar, the inspector
// or a non-focusing button took is nobody's pending click. `upId` and
// `pressedId` are 0 for the page itself, which is a click too — that is the
// outside press an overlay dismisses on.
bool ClickFromRelease(bool pending, int pressedId, MouseButton pressedButton,
                      bool dragged, int upId, MouseButton upButton);

// The same for the keyboard: whether the release of Enter or Space makes a
// click. GPUI arms on the key down — `pending_keyboard_down` is the focus
// generation the keystroke went down at — and makes the click from the key
// up, if that stamp still matches. A generation rather than the focused
// element itself, because focus that left and came back is not the same
// press; any other key in between clears the stamp, and a modifier held on
// either half means the keystroke was a shortcut, not an activation.
bool ClickFromKeyRelease(bool pending, int pendingGen, int focusGen, int key,
                         bool modified);

// The window chrome's own click ids (WM_NCHITTEST). Negative, so they cannot
// collide with a hashed one: HashClickId is non-negative by construction.
// They were 100/101/102/200 once, and collided with the showcase overview
// grid — which is what a flat id space costs when two people pick numbers.
enum : int8_t {
    ClickWinMin = -1,
    ClickWinMax = -2,
    ClickWinClose = -3,
    ClickWinCaption = -4,
};

struct App;
struct Window;

// gpui::Tiling: the client-frame sides a window manager has placed flush
// against another surface. UI's WindowBorder consumes it, while the platform
// owns the state because decorations are a property of the native window.
struct Tiling {
    bool top = false;
    bool bottom = false;
    bool left = false;
    bool right = false;

    bool IsTiled() const { return top || bottom || left || right; }
    bool AllTiled() const { return top && bottom && left && right; }
};

struct WinOpts {
    bool borderless = false;
    // The view draws the title bar. Cocoa keeps its traffic-light controls
    // above a transparent full-size content view; Windows drops the caption
    // but keeps the rest of the frame, and X11 drops the frame outright. On
    // all three the view supplies the title-bar background, its drag region
    // and — off macOS — the minimize / maximize / close controls, which is
    // what component::TitleBar builds.
    bool clientTitleBar = false;
    bool anim = false;
    int timerMs = 500;
};

// gpui::FrameTiming. One drawn frame, as measured by the window itself, so the
// FPS HUD reports what the runtime actually spent rather than an approximation
// taken from the outside. GPUI gates recording behind
// `set_frame_trace_enabled`; here it is two QPC reads per frame and always on.
struct FrameTiming {
    float drawSecs = 0;
    // How many invalidations were coalesced into this frame: the AppInvalidate
    // calls since the previous frame was recorded, which is what GPUI's
    // `invalidations` counts. An animation frame is one of them, the way
    // `request_animation_frame` is a `refresh()` there.
    uint64_t invalidations = 0;
    // When the frame reached the screen, on TimeNow()'s clock, or negative for
    // a frame that was drawn but not presented — the scene found it identical
    // to the last one. GPUI stamps `present_end` from inside its renderer;
    // this tree has no seam under the swap chain, so the stamp is taken as
    // PaintTargetEnd returns, which is after paint_win.cpp's Present, the X11
    // flush, the CGContext flush, or the canvas draw of the
    // requestAnimationFrame callback on wasm.
    double presentAt = -1;
};

enum : uint16_t {
    kFrameTraceCap = 256
};

// Process-wide state: the Direct2D / DirectWrite factories, the shared font
// cache, the entity store and the open windows. GPUI's `App`.
// One live cx.subscribe. GPUI keys its subscriber lists by the emitter and
// hands back a Subscription that unsubscribes when it drops; a handle here is
// an id, and a subscription whose emitter or subscriber has gone stale is
// swept the next time the emitter emits.
struct EntitySub {
    int id = 0;
    EntityId emitter = {};
    const void* eventType = nullptr;
    Listener handler = {};
};

// App::global<T>: application-owned services and configuration shared by
// views. Rust keys these slots by TypeId; an inline template key gives C++ the
// same stable identity without RTTI. Values have explicit ownership and are
// released by AppFree, so two Apps never share mutable component state.
using AppGlobalFreeFn = void (*)(void* value);

struct AppGlobalSlot {
    const void* key = nullptr;
    void* value = nullptr;
    AppGlobalFreeFn freeValue = nullptr;
};

void* AppGlobalGetRaw(const App* app, const void* key);
void AppGlobalSetRaw(App* app, const void* key, void* value,
                     AppGlobalFreeFn freeValue);
bool AppGlobalRemoveRaw(App* app, const void* key);
void AppGlobalClear(App* app);

template <typename T>
const void* AppGlobalKey() {
    static const uint8_t key = 0;
    return &key;
}

template <typename T>
T* AppGlobalGet(const App* app) {
    return (T*)AppGlobalGetRaw(app, AppGlobalKey<T>());
}

template <typename T>
void AppGlobalDelete(void* value) {
    delete (T*)value;
}

template <typename T>
T* AppGlobalEnsure(App* app) {
    T* value = AppGlobalGet<T>(app);
    if (value || !app) {
        return value;
    }
    value = new T();
    AppGlobalSetRaw(app, AppGlobalKey<T>(), value, &AppGlobalDelete<T>);
    return value;
}

template <typename T>
bool AppGlobalRemove(App* app) {
    return AppGlobalRemoveRaw(app, AppGlobalKey<T>());
}

struct App {
    PaintApp* paint = nullptr;
    Vec<Window*> windows;
    // Entity store; see Entity.h. Slots are recycled, so a handle carries a
    // generation and goes stale instead of dangling.
    Vec<EntitySlot> entities;
    Vec<int32_t> freeSlots;
    // cx.subscribe: every live subscription, in the order they were made.
    Vec<EntitySub> subs;
    // cx.observe: the same for the untyped channel, where `emitter` is the
    // entity being watched.
    Vec<EntitySub> observers;
    // Theme, registries and behavior defaults owned by this application.
    Vec<AppGlobalSlot> globals;
    int nextSubId = 1;
    int exitCode = 0;

    App() = default;
};

// One platform window: its render target, frame arena, hover / focus state
// and the view it renders. GPUI's `Window`.
// The OS window: an HWND wrapper on Windows, an X11 Window on Linux.
struct PlatWindow;

// crates/base/src/text_selection.rs WindowSelectionState, one per window.
struct WindowSelection;

struct Window {
    App* app = nullptr;
    PlatWindow* plat = nullptr;
    PaintCtx paint = {};
    Arena* frameArena = nullptr;
    // The taffy tree this window lays out in, kept between frames. Made on
    // the first frame and freed with the window.
    LayoutCache* layout = nullptr;
    // The selection over this window's selectable runs. Made on the first
    // press that lands on text and dropped with the window.
    WindowSelection* sel = nullptr;
    // The view this window renders. GPUI's Window holds a root view too.
    EntityId root = {};
    // Every entity `EntityRender` was asked for while the last frame was
    // built — GPUI's `Window::dirty_views`, and what makes `Notify` name a
    // window rather than every window. Rebuilt each frame.
    Vec<EntityId> rendered;
    // The scroll boxes the frame before this one painted, swapped out of
    // `paint.scrolls` as the frame starts. Rust's `ScrollHandle::bounds()`
    // and `ListState::viewport_bounds()` answer with the box the last layout
    // gave them; a view here has no handle to keep it on, so this is where a
    // lazy list finds how tall it was before it decides how many rows to
    // build. `WindowLastScrollRect` is the lookup.
    Vec<ScrollRect> prevScrolls;
    // Rebuilt after layout from the frame's element tree. Platform adapters
    // read this; it owns no strings or callbacks beyond the frame arena.
    Vec<AccessibilityNode> accessibility;
    // Semantic-content hash from the last frame. Native adapters receive one
    // children-invalidated event only when it changes, not on every animation
    // repaint.
    uint64_t accessibilityHash = 0;
    int hoverId = 0;
    int focusId = 0;
    // window.focus_generation: bumped every time the focus moves, so a
    // keystroke can tell that it stayed put without holding onto the element.
    int focusGen = 0;
    float mouseX = 0;
    float mouseY = 0;
    // The modifiers the pointer last moved or pressed under. Rust reads them
    // off the MouseMoveEvent inside its handler; a hover here is worked out
    // by the frame builder, which has no event to read, so the window keeps
    // the last ones. What wants them: a secondary-hover over a symbol.
    Modifiers mouseModifiers = {};
    // What the pointer looks like right now; the OS is only told on a change.
    CursorKind cursor = CursorKind::Arrow;
    bool maximized = false;
    // Window::client_inset and window_decorations(). WindowBorder writes the
    // stable inset while it renders; fixed UI overlays read it while their
    // own trees are built, which is before their enclosing Root is wrapped.
    // Negative means no component has supplied one yet.
    float clientInset = -1;
    Tiling tiling = {};
    // WindowBorder::resize_hit_size. Kept here because Linux starts native
    // resizing in its event adapter rather than from a retained element.
    float resizeHitSize = 4;
    // is_window_active: whether this window has the focus. A client-decorated
    // frame dims its border when it does not.
    bool active = true;
    bool running = true;
    bool anim = false;
    // window.request_animation_frame(): one more frame after this one, asked
    // for while the frame is being built and cleared as the next one starts,
    // so something that has finished moving stops asking. `anim` is the other
    // thing — a window that draws back to back until it is turned off.
    bool animFrame = false;
    // The instant this frame started. Every transition in it samples the same
    // `now`, which is what Rust gets from reading the executor's clock.
    double frameNow = 0;
    // Time the last frame was presented / drawn. Used for inactive window
    // throttling (GPUI's inactive_frame_interval).
    double lastDrawTime = 0;
    bool mouseDown = false;
    // cx.stop_propagation(): set by a handler, read by the chain it is in.
    bool stopPropagation = false;
    // OngoingScroll is keyed by (mask axis, scroll id) upstream. Only one
    // pointer gesture can be active per axis in a window, so two slots retain
    // the same state without a map.
    int scrollLockHorizontalId = 0;
    int scrollLockVerticalId = 0;
    OngoingScroll scrollLockHorizontal = {};
    OngoingScroll scrollLockVertical = {};
    // The multi-click run in progress: when the last press landed, where, and
    // with which button, so WindowClickCount can tell the next press apart
    // from a second click. GPUI keeps the same three in its platform layer.
    double lastDownAt = 0;
    float lastDownX = 0;
    float lastDownY = 0;
    MouseButton lastDownButton = MouseButton::Left;
    int clickRun = 0;
    // The element that took the press, until the button comes back up: what
    // GPUI's drag gives an element for free. 0 when nothing is held.
    int pressedId = 0;
    // What the press that is still down looked like: GPUI keeps the whole
    // MouseDownEvent as `pending_mouse_down` and hands it to the click it
    // makes on release, which is where the count and the modifiers come from.
    int pressedCount = 1;
    // pending_mouse_down being Some: a press is waiting to become a click.
    bool pressPending = false;
    // Where the press landed, and whether the pointer has since travelled far
    // enough to call it a drag. GPUI starts a drag from the move rather than
    // the press, and a drag takes the release the click would have had.
    float pressedX = 0;
    float pressedY = 0;
    bool pressedMoved = false;
    MouseButton pressedButton = MouseButton::Left;
    Modifiers pressedModifiers = {};
    // The drag in flight: what the press picked up, and which drop target the
    // pointer is over right now. GPUI keeps the same pair — `active_drag` and
    // the hitbox its drop handlers consult — on its Window.
    DragPayload activeDrag = {};
    int dragOverId = 0;
    // AnyDrag::cursor_offset: where inside the dragged element the press
    // landed, so whatever is drawn under the pointer sits where the element
    // was rather than jumping its corner to the cursor.
    float dragOffX = 0;
    float dragOffY = 0;
    bool eatReturn = false;
    // The keystroke the keymap took also arrives as a character, and a
    // character the keymap took is not text. Set on the key, read and cleared
    // on the character that follows it.
    bool eatChar = false;
    // pending_keyboard_down: an Enter or Space is down on the focused
    // element, and the generation the focus was at when it went down.
    bool keyPressPending = false;
    int keyPressGen = 0;
    // TranslateMessage has already queued WM_SYSCHAR by the time WndProc
    // learns that GPUI handled its WM_SYSKEYDOWN. Suppress that paired
    // character so an Alt+letter binding does not also activate a menu/beep.
    bool eatSysChar = false;
    // The scrollbar being dragged, and how far into its thumb the press
    // landed. GPUI keeps the same pair in ScrollbarState::drag_pos.
    int scrollDragId = 0;
    float scrollDragGrab = 0;
    // Which of the box's two bars is being dragged.
    bool scrollDragHorizontal = false;
    // The field a thumb drag writes to, used when scrollId is 0 so the
    // next frame can still find the box. BindInput is enough; the id is
    // not.
    InputState* scrollDragInput = nullptr;
    InputState* input = nullptr;
    // This window's one TooltipOverlay. Created on first use, the way a
    // field's blink cursor is.
    EntityId tooltip = {};
    Overlay overlay = {};
    InspectorState inspector = {};
    MenuState menu = {};
    Vec<FocusRect> focusEls;
    // The focus trap a container asked to hold focus this frame, settled
    // after the focusables are collected. 0 when nothing asked.
    int pendingTrap = 0;
    // Which trap was present in the preceding rendered frame. A trap takes
    // focus when it first appears, but an already-open one does not reclaim
    // focus that code deliberately moved elsewhere.
    int previousTrap = 0;
    // The trap container's own focus id, for a trap that holds no tab stop.
    int pendingTrapHost = 0;
    Vec<KeyedSlot> keyed;
    // The frame's key contexts and action handlers, in tree order.
    Vec<DispatchNode> dispatch;
    Vec<MotionSlotRec> motionSlots;
    WinOpts opts = {};
    // Window-level subscriptions bound to view entities.
    Listener onKey = {};
    Listener onClick = {};
    Listener onMouseDown = {};
    Listener onMouseUp = {};
    Listener onMouseMove = {};
    Listener onMouseExit = {};
    Listener onScrollWheel = {};
    // Armed timers, any number of them.
    Vec<TimerSub> timers;
    int nextTimerId = 1;
    // Which InputState had focus last frame, so the runtime can start and stop
    // its caret without every app wiring that up.
    InputState* prevInput = nullptr;
    // Ring of the last kFrameTraceCap draw times; frameSeq counts every frame
    // ever drawn and is what a collector cursors on.
    FrameTiming frameTrace[kFrameTraceCap] = {};
    uint64_t frameSeq = 0;
    // AppInvalidate calls since the last recorded frame; the next frame takes
    // them as its `invalidations` and zeroes it.
    uint64_t invalidations = 0;

    Window() = default;
    // Drops the focus registration a field is still holding on this window.
    // The mirror of ~InputState, which drops the one this window is holding
    // on the field; see window_common.cpp.
    ~Window();
};

// ─── context ──────────────────────────────────────────────────────────────

// GPUI's Context<T>. `win` is null outside a window callback, `a` is the frame
// arena during render, `self` is the entity currently rendering or updating.
struct Ctx {
    App* app = nullptr;
    Window* win = nullptr;
    Arena* a = nullptr;
    EntityId self = {};
    // The ids of the widgets this one is being built inside, folded together
    // — window.element_id_stack, which GPUI has already pushed by the time a
    // child's render runs. The port builds its tree before IdsCollect folds
    // anything, so a widget that names itself pushes it here as well, and a
    // `use_keyed_state` asked for underneath is keyed by the whole stack.
    // See IdScope.
    uint32_t path = 0;
};

// The same fold IdsCollect uses on the element tree, over one more name.
uint32_t IdFoldName(uint32_t parent, Str name);

// with_element_id: the widget's name is on the stack while its parts are
// built, and off it again afterwards. A widget that owns a name pushes one of
// these at the top of the function that builds its children.
struct IdScope {
    Ctx* cx = nullptr;
    uint32_t prev = 0;

    IdScope(Ctx* c, Str name) : cx(c), prev(c ? c->path : 0) {
        if (cx) {
            cx->path = IdFoldName(prev, name);
        }
    }
    ~IdScope() {
        if (cx) {
            cx->path = prev;
        }
    }
    IdScope(const IdScope&) = delete;
    IdScope& operator=(const IdScope&) = delete;
};

EntityId EntityNewRaw(App* app, void* ptr, RenderFn render, DropFn drop);
void* EntityGet(App* app, EntityId id);
void EntityDrop(App* app, EntityId id);
void EntityDropAll(App* app);

// A typed handle. Stale handles read back as null instead of dangling.
template <typename T>
struct Entity {
    EntityId id = {};

    bool IsValid() const { return id.IsValid(); }
    T* Get(App* app) const { return (T*)EntityGet(app, id); }
    T* Get(Ctx* cx) const { return (T*)EntityGet(cx->app, id); }
};

template <typename T>
void EntityDropT(void* p) {
    delete (T*)p;
}

// cx.new(|cx| T::default()). T must expose `static El* Render(T*, Ctx*)`.
template <typename T>
Entity<T> EntityNew(App* app) {
    Entity<T> e;
    e.id = EntityNewRaw(app, new T(), (RenderFn)&T::Render, &EntityDropT<T>);
    return e;
}

template <typename T>
Entity<T> EntityNew(Ctx* cx) {
    return EntityNew<T>(cx->app);
}

// State with no Render, e.g. a model the views read.
template <typename T>
Entity<T> EntityNewState(App* app) {
    Entity<T> e;
    e.id = EntityNewRaw(app, new T(), nullptr, &EntityDropT<T>);
    return e;
}

// cx.listener(|this, ev, window, cx| ...). The cast mirrors MkFunc0/MkFunc1.
// E is whichever event struct the handler takes: ClickEvent, KeyEvent, ...
template <typename T, typename E>
Listener Listen(Ctx* cx, void (*fn)(T*, Ctx*, const E*)) {
    Listener l;
    l.SetFn(fn);
    l.view = cx->self;
    return l;
}

// cx.listener(move |this, ...| ... ix ...): same, carrying a captured value.
template <typename T, typename E>
Listener Listen(Ctx* cx, void (*fn)(T*, Ctx*, const E*, intptr_t),
                intptr_t arg) {
    Listener l;
    l.SetFn(fn);
    l.view = cx->self;
    l.arg = arg;
    l.SetArgBound();
    return l;
}

// A handler that takes a value the component supplies: which day of the
// calendar, which combobox row. The component fills it with ListenerArg.
template <typename T, typename E>
Listener Listen(Ctx* cx, void (*fn)(T*, Ctx*, const E*, intptr_t)) {
    Listener l;
    l.SetFn(fn);
    l.view = cx->self;
    l.SetHasArg();
    return l;
}

// Bind the value a component hands its caller. This is what a Rust closure
// gets as its event payload: `.on_click(cx.listener(|this, day, _, cx| ...))`.
inline Listener ListenerArg(Listener l, intptr_t arg) {
    if (l.IsValid()) {
        l.arg = arg;
        l.SetArgBound();
    }
    return l;
}

// The same, for the value a widget produces rather than one its caller chose:
// which day of the calendar, the state a checkbox activation lands on. Rust
// passes that beside whatever the closure captured, so a caller that already
// bound its own — which of ten toggles this is — keeps it.
inline Listener ListenerFill(Listener l, intptr_t v) {
    if (l.IsValid() && !l.ArgBound()) {
        l.arg = v;
        l.SetHasArg();
    }
    return l;
}

// Same, but bound to another entity instead of the one that is rendering.
template <typename T, typename E>
Listener ListenTo(Entity<T> e, void (*fn)(T*, Ctx*, const E*)) {
    Listener l;
    l.SetFn(fn);
    l.view = e.id;
    return l;
}

// The same fill-me-in handler as the third Listen above, bound to another
// entity: the component supplies the value — which menu row was taken —
// rather than the caller having captured one.
template <typename T, typename E>
Listener ListenTo(Entity<T> e, void (*fn)(T*, Ctx*, const E*, intptr_t)) {
    Listener l;
    l.SetFn(fn);
    l.view = e.id;
    l.SetHasArg();
    return l;
}

template <typename T, typename E>
Listener ListenTo(Entity<T> e, void (*fn)(T*, Ctx*, const E*, intptr_t),
                  intptr_t arg) {
    Listener l;
    l.SetFn(fn);
    l.view = e.id;
    l.arg = arg;
    l.SetArgBound();
    return l;
}

// ─── EventEmitter (crates/gpui/src/app/entity_map.rs, subscriber lists) ───
//
// Rust marks what an entity emits with `impl EventEmitter<E> for T`, sends one
// with `cx.emit(e)`, and hears it with `cx.subscribe(&entity, ..)`. An empty
// specialization is the C++ declaration of the same pairing:
//
//     template <> struct EventEmitter<MyState, MyEvent> {};
//
// A state may have more than one specialization. Subscribe and EntityEmit are
// constrained by that declaration, and the erased runtime key keeps those
// event channels separate.
template <typename T, typename E>
struct EventEmitter;

template <typename T, typename E>
concept EmitsEvent = requires {
    sizeof(EventEmitter<T, E>);
};

template <typename E>
const void* EntityEventType() {
    static const uint8_t key = 0;
    return &key;
}
//
// A Subscription is a handle rather than a guard: nothing is destroyed on
// scope exit in a tree of POD state, so it is dropped by asking. It rarely
// has to be: a subscription whose subscriber has gone away is swept on the
// next emit, which is the lifetime that mattered.
struct Subscription {
    int id = 0;

    bool IsValid() const { return id != 0; }
};

Subscription EntitySubscribeRaw(App* app, EntityId emitter,
                                const void* eventType, Listener handler);
void EntityUnsubscribe(App* app, Subscription sub);

// ─── observers (crates/gpui/src/app.rs, `observers`) ─────────────────────
//
// `cx.observe(&entity, |this, observed, cx| ..)`: the untyped half of the
// pair above. An emitter sends an event and says what it is; an entity that
// notifies says only that it changed, and whoever is watching hears about it.
// The handler is called with the entity that notified, so one observer can
// watch several.
Subscription EntityObserveRaw(App* app, EntityId observed, Listener handler);
void EntityUnobserve(App* app, Subscription sub);
int EntityObserverCount(App* app, EntityId observed);

// `cx.notify()` for an entity that is not the one in hand, and the whole of
// what `Notify(cx)` does: run the observers, then invalidate the windows that
// rendered that entity last frame. A window that has never rendered it — a
// state entity that is not a view, a view whose first frame has not been
// built — falls back to `from`, and to every window when there is none, which
// is what this did for everything before there was anything to be precise
// with.
void NotifyEntity(App* app, EntityId id, Window* from);
// The erased runtime half used by the typed EntityEmit wrapper and by dynamic
// language bridges. `ev` does not outlive the call.
void EntityEmitRaw(App* app, Window* win, EntityId emitter,
                   const void* eventType, const void* ev);
// How many subscriptions the emitter has, stale ones swept first. For a
// caller that has to know whether anybody is listening at all.
int EntitySubscriberCount(App* app, EntityId emitter);

// cx.subscribe(&emitter, cx.listener(..)): the handler belongs to whatever is
// rendering, the way Listen's does, and runs whenever `emitter` emits an E.
template <typename T, typename S, typename E>
requires EmitsEvent<T, E> Subscription
Subscribe(Ctx* cx, Entity<T> emitter, void (*fn)(S*, Ctx*, const E*)) {
    Listener l;
    l.SetFn(fn);
    l.view = cx->self;
    return EntitySubscribeRaw(cx->app, emitter.id, EntityEventType<E>(), l);
}

// cx.observe(&entity, ..): `handler` runs on `cx->self` whenever `observed`
// notifies. The event pointer a listener takes is the notifying EntityId,
// since an observer that watches more than one has to tell them apart.
template <typename T, typename S>
Subscription Observe(Ctx* cx, Entity<T> observed,
                     void (*handler)(S*, Ctx*, const EntityId*)) {
    Listener l = Listen(cx, handler);
    return EntityObserveRaw(cx->app, observed.id, l);
}

// The same for a caller that has the observer's handle rather than a Ctx.
template <typename T, typename S>
Subscription ObserveTo(App* app, Entity<T> observed, Entity<S> observer,
                       void (*handler)(S*, Ctx*, const EntityId*)) {
    Listener l = ListenTo(observer, handler);
    return EntityObserveRaw(app, observed.id, l);
}

// The same, for a subscriber that is not the one rendering.
template <typename T, typename S, typename E>
requires EmitsEvent<T, E> Subscription SubscribeTo(App* app, Entity<T> emitter,
                                                   Entity<S> subscriber,
                                                   void (*fn)(S*, Ctx*,
                                                              const E*)) {
    Listener l;
    l.SetFn(fn);
    l.view = subscriber.id;
    return EntitySubscribeRaw(app, emitter.id, EntityEventType<E>(), l);
}

// The same, with the value a Rust closure would capture.
template <typename T, typename S, typename E>
requires EmitsEvent<T, E> Subscription
SubscribeTo(App* app, Entity<T> emitter, Entity<S> subscriber,
            void (*fn)(S*, Ctx*, const E*, intptr_t), intptr_t arg) {
    Listener l = ListenTo(subscriber, fn, arg);
    return EntitySubscribeRaw(app, emitter.id, EntityEventType<E>(), l);
}

// cx.emit(ev): every live subscriber to this event type hears it, oldest
// first. The event pointer does not outlive the call.
template <typename T, typename E>
requires EmitsEvent<T, E> void EntityEmit(App* app, Window* win,
                                          Entity<T> emitter, const E* ev) {
    EntityEmitRaw(app, win, emitter.id, EntityEventType<E>(), ev);
}

template <typename T, typename E>
requires EmitsEvent<T, E> void Emit(Ctx* cx, Entity<T> emitter, const E* ev) {
    EntityEmit(cx->app, cx->win, emitter, ev);
}

// cx.notify(): the frame tree is rebuilt from scratch, so this just schedules
// a repaint of every window. GPUI tracks which views observe the entity.
void Notify(Ctx* cx);
void NotifyApp(App* app);
void ListenerCall(App* app, Window* win, const Listener& l, const void* ev);

// Render an entity into `a`, building the Ctx for it.
El* EntityRender(App* app, Window* win, Arena* a, EntityId id);

// window.use_keyed_state(key, cx, init)
// Pointer-valued use_keyed_state. `fresh` is a fully constructed object;
// the first call adopts it, later calls delete the unused candidate and
// return the object already stored under the key. WindowKeyedFree runs every
// adopted object's drop function.
void* WindowKeyedState(Window* win, uint32_t key, void* fresh, DropFn drop);
void WindowKeyedFree(Window* win);

// The transition state behind one id, created zeroed on first ask and marked
// as wanted by this frame. Everything about it is in base/motion.h; this is
// the store, which has to live with the window.
void* WindowMotionState(Window* win, uint32_t key, int size);
// Drop what this frame did not ask for, which is what GPUI does with the
// state of an element it no longer renders.
void WindowMotionSweep(Window* win);
void WindowMotionFree(Window* win);

template <typename T>
T* KeyedState(Ctx* cx, uint32_t key) {
    void* p = WindowKeyedState(cx->win, key, new T(), &EntityDropT<T>);
    return (T*)p;
}

// The same window-keyed state, as an entity. `use_keyed_state` in Rust hands
// back an `Entity<T>`, which is what lets the state own timers and listeners
// of its own; KeyedState above is only a pointer, so nothing can be bound to
// it. A widget whose behavior outlives one frame — a hover card counting down
// to open — needs this one.
EntityId WindowKeyedEntity(Window* win, App* app, uint32_t key, void* fresh,
                           DropFn drop);

// A keyed slot is remembered by its key alone, so two different states under
// one name are one slot — and the second would read the first's memory as its
// own. `kind` is a constant per state type (the hash of its name is what the
// callers pass), which is what keeps, say, a popover's own state and the
// listeners its escape runs apart.
inline uint32_t KeyedKey(uint32_t name, uint32_t kind) {
    uint32_t h = name * 2654435761u;
    h ^= kind + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h ? h : 1u;
}

// use_keyed_state's key: the name folded into the stack of ids above it, so
// the same local name under two different widgets is two states. A caller
// that has already qualified its name by hand gets the same answer it did
// before, since nothing above it has pushed a scope.
inline uint32_t KeyedName(Ctx* cx, Str name) {
    return IdFoldName(cx ? cx->path : 0, name);
}

template <typename T>
Entity<T> KeyedEntity(Ctx* cx, uint32_t key) {
    Entity<T> e;
    e.id = WindowKeyedEntity(cx->win, cx->app, key, new T(), &EntityDropT<T>);
    return e;
}

// window.with_element_state(global_id, ..): state that belongs to one element
// rather than to the view that built it. The key is the element's name folded
// into the stack of ids above it — which is what a GlobalElementId is — so
// the same local name under two widgets is two states. `kind` is the state's
// own name, since a key remembers a slot and not what was put in it.
template <typename T>
T* ElementState(Ctx* cx, Str name, Str kind) {
    return KeyedState<T>(cx, KeyedKey(KeyedName(cx, name), HashClickId(kind)));
}

// The same, as an entity — for state a listener has to be bound to, which is
// what an element that answers its own presses needs.
template <typename T>
Entity<T> ElementStateEntity(Ctx* cx, Str name, Str kind) {
    return KeyedEntity<T>(cx, KeyedKey(KeyedName(cx, name), HashClickId(kind)));
}

// Window-level subscriptions. GPUI spells these window.on_key_down and
// cx.spawn + Timer::after; here each one is a Listener bound to a view.
void WindowOnKey(Window* win, Listener l);
// Fires for a click no element handled — the outside click that dismisses an
// overlay. Elements carry their own listener; this is not a dispatch table.
void WindowOnUnhandledClick(Window* win, Listener l);

// window.toggle_inspector, and the picking mode its magnifier button starts.
// Ctrl+Shift+I toggles it, as it does in Rust on everything but macOS.
void WindowToggleInspector(Window* win);
void WindowInspectorPick(Window* win, bool picking);
const InspectorState* WindowInspector(Ctx* cx);

// window.active_drag: what a press picked up, or an invalid payload when
// nothing is being dragged. A drop target reads it to decide whether to show
// itself at all, the way Rust's `drag_over::<T>` only exists for a drag of
// that type.
const DragPayload* WindowActiveDrag(Ctx* cx);
// window.is_window_active().
bool WindowIsActive(Ctx* cx);
// What the platform calls when the window takes or loses the focus.
void WindowSetActive(Window* win, bool active);
// Which element the drag is over, of those that take its kind — 0 for none.
// `El::Click(id)` names an element, so this answers with that id.
int WindowDragOverId(Ctx* cx);
// cx.stop_propagation(): the rest of the chain does not hear the event the
// handler is in. Only an element's mouse handler has a chain to stop.
void WindowStopPropagation(Ctx* cx);
// The same cursor offset, for whoever draws the thing being dragged.
Point WindowDragOffset(Ctx* cx);
// One subscription per event type, which is what window.on_mouse_event::<T>
// asks for in Rust. Each handler takes the matching event:
// `void OnDown(T* self, Ctx* cx, const MouseDownEvent* ev)`.
void WindowOnMouseDown(Window* win, Listener l);
void WindowOnMouseUp(Window* win, Listener l);
void WindowOnMouseMove(Window* win, Listener l);
void WindowOnMouseExit(Window* win, Listener l);
void WindowOnScrollWheel(Window* win, Listener l);
// cx.spawn(async move |this, cx| ...) with nothing to await: run `l` against
// its entity on the main thread, once this pass of the event loop is over.
// Safe to call from a worker thread, which is the point — a background job
// finishes with one of these rather than touching an entity itself.
//
// `ev` is what the listener receives, and must outlive the post; it is
// usually the job struct the worker filled in, and freeing it is the last
// thing the listener does. The post is dropped if the window has closed or
// the entity has gone by the time it runs, which is what Rust's
// `this.update(cx, ..).ok()` swallows.
//
// See sys/executor.h for the half of this that has no window: ExecPost.
void WindowPost(Window* win, Listener l, const void* ev = nullptr);

// Repeating timer; GPUI's system_monitor does the same with a spawned task
// that sleeps and calls cx.notify(). Returns a handle, or 0. Any number may
// be armed at once.
int WindowSetInterval(Window* win, int ms, Listener l);
// Fires once, then forgets itself. GPUI's Timer::after.
int WindowSetTimeout(Window* win, int ms, Listener l);
void WindowCancelTimer(Window* win, int id);

// ─── caret ────────────────────────────────────────────────────────────────
//
// Port of crates/base/src/input/base/blink_cursor.rs. A blinking caret is
// state, not a function of the clock: something flips it on a 500 ms timer
// and every repaint in between shows what the last flip decided. Sampling the
// clock at paint time instead makes the caret invisible whenever nothing
// happens to repaint during the lit half.
//
// One per text field, the way Rust gives every InputState its own
// Entity<BlinkCursor>. `handle` is an EntityId the owner keeps; the first
// Start creates the entity behind it.

struct BlinkCursor {
    bool visible = false;
    bool paused = false; // solid, because the user is typing
    // The armed timer. Cancelling it is what Rust's epoch counter does.
    int timer = 0;

    static void OnFlip(BlinkCursor* self, Ctx* cx, const TickEvent* ev);
    static void OnResume(BlinkCursor* self, Ctx* cx, const TickEvent* ev);
};

// Idempotent. Rust calls these from on_focus / on_blur.
void BlinkStart(App* app, Window* win, EntityId* handle);
void BlinkStop(App* app, Window* win, EntityId* handle);
// Keep it solid, then resume blinking shortly after — Rust's
// pause_blink_cursor, called from every edit and cursor movement.
void BlinkPause(App* app, Window* win, EntityId* handle);
// What a text widget asks before drawing its caret. Rust:
// blink_cursor.read(cx).visible().
bool BlinkVisible(App* app, EntityId handle);

// The same, when a Ctx is already in hand — which it is inside any Render.
inline void BlinkStart(Ctx* cx, EntityId* handle) {
    BlinkStart(cx->app, cx->win, handle);
}
inline void BlinkStop(Ctx* cx, EntityId* handle) {
    BlinkStop(cx->app, cx->win, handle);
}
inline void BlinkPause(Ctx* cx, EntityId* handle) {
    BlinkPause(cx->app, cx->win, handle);
}
inline bool BlinkVisible(Ctx* cx, EntityId handle) {
    return BlinkVisible(cx->app, handle);
}

// The input engine, when a Ctx is already in hand — which it is inside any
// Render and any listener.
inline void InputFocus(InputState* s, Ctx* cx) {
    InputFocus(s, cx->app, cx->win);
}
inline void InputBlur(InputState* s, Ctx* cx) {
    InputBlur(s, cx->app, cx->win);
}
inline void InputMoveTo(InputState* s, Ctx* cx, int offset) {
    InputMoveTo(s, cx->app, cx->win, offset);
}
inline void InputSelectAll(InputState* s, Ctx* cx) {
    InputSelectAll(s, cx->app, cx->win);
}
inline void InputClean(InputState* s, Ctx* cx) {
    InputClean(s, cx->app, cx->win);
}
inline void InputReplaceAll(InputState* s, Ctx* cx, Str value) {
    InputReplaceAll(s, cx->app, cx->win, value);
}
inline void InputInsert(InputState* s, Ctx* cx, Str value) {
    InputInsert(s, cx->app, cx->win, value);
}
inline bool InputPerform(InputState* s, Ctx* cx, InputAction action,
                         bool shift = false) {
    return InputPerform(s, cx->app, cx->win, action, shift);
}

// Open a window whose root is a view entity, the WindowOpen + cx.new pair.
Window* WindowOpenView(App* app, Str title, int dipW, int dipH, EntityId root,
                       WinOpts opts);
int AppRunView(Str title, int dipW, int dipH, EntityId root, App* app,
               WinOpts opts);

// The view a window renders, typed.
template <typename T>
T* WindowRoot(Window* win) {
    return win ? (T*)EntityGet(win->app, win->root) : nullptr;
}

// Client size in DIPs; what onRender used to receive as WinSize.
WinSize WindowSize(Window* win);

// FrameTimingCollector::collect_unseen: copy the frames drawn since *cursor
// into `out` and advance the cursor. Frames dropped from the ring while the
// caller was away are skipped. Returns how many were written.
int WindowCollectFrames(Window* win, uint64_t* cursor, FrameTiming* out,
                        int max);

// Monotonic seconds since the first call. GPUI's `Instant`, which the FPS
// readouts need at a finer resolution than GetTickCount64's ~16 ms.
double TimeNow();

App* AppNew();
void AppFree(App* app);

// Put UTF-8 text on the system clipboard.
void ClipboardSetText(Window* win, Str text);
// Take it back off. The result is arena-allocated and empty when the
// clipboard holds no text.
Str ClipboardGetText(Arena* a, Window* win);

// macOS NSTextContent autofill metadata. The other platforms intentionally
// accept and ignore it, matching gpui-kit's cfg-gated implementation.
// An empty value clears the metadata.
void WindowSetTextContentType(Window* win, Str value);

// cx.open_url: hand a link to whatever the desktop opens links with. Rust's
// takes an `&str` and answers nothing, and so does this — a browser that
// refuses to start is not something a caller can do anything about.
void OpenUrl(Str url);

// PathPromptOptions: what the desktop's own open dialog may be pointed at.
struct PathPrompt {
    // Whether a file, a directory, or either may be chosen. Both false is a
    // dialog that can choose nothing, so it is read as `files`.
    bool files = true;
    bool directories = false;
    // The dialog's title. Rust calls it `prompt`.
    Str title = {};
};

// cx.prompt_for_paths, with one path and no task: the chosen path is returned
// in the temp arena, or empty when the user cancelled, when the platform has
// no dialog of its own (wasm), or when the desktop has none to offer (a Linux
// session with neither zenity nor kdialog). Rust answers a
// `Task<Result<Option<Vec<PathBuf>>>>` and can be asked for several paths at
// once; every caller here wants one and wants it where it asked, which is what
// the platform dialogs do anyway — they run their own loop until the user is
// done.
TempStr PromptForPathTemp(Window* win, const PathPrompt& opts);

int AppRun(App* app);
Window* WindowOpen(App* app, Str title, int dipW, int dipH, WinOpts opts);
void AppSetTitle(Window* win, Str title);
void AppRequestAnim(Window* win, bool on);
// One more frame, rather than every frame. Safe to call from inside a render.
void WindowRequestAnimationFrame(Window* win);

// Collect focusable click targets from last paint for Tab cycling.
void FocusCollect(Window* win, El* root);
void IdsCollect(El* root);
void AccessibilityCollect(El* root, Vec<AccessibilityNode>* out);
const AccessibilityNode* WindowAccessibilityNode(const Window* win,
                                                 uint32_t nodeId);
bool WindowAccessibilityPerform(Window* win, uint32_t nodeId,
                                AccessibilityAction action, Str value = {});
// RangeValue::SetValue needs an absolute numeric setter rather than a run of
// Increment actions. Only a live, enabled slider node accepts it.
bool WindowAccessibilitySetNumericValue(Window* win, uint32_t nodeId,
                                        float value);
int FocusNext(Window* win, int trapId, bool backward);
// Move the focus. Everything that focuses goes through here, so the
// generation a keystroke is stamped with counts every move.
// FocusHandle — crates/gpui. In GPUI a focus handle is a refcounted key into
// the window's slotmap, made with `cx.focus_handle()`, owned by whatever holds
// the state, and attached to a box with `div().track_focus(&handle)`. It has
// nothing to do with the element's name: a state that wants focus asks for a
// handle and keeps it, and the element tree picks it up again each frame.
//
// The port used to derive a focus id from an element's name — sometimes with
// an arithmetic twist to keep it clear of that name's *click* id, which is
// what `HashClickId(id) * 31 + 1` in the popover was. A handle is that done
// properly. Handles are allocated below -1000 and hashed element ids are
// positive, so the two spaces cannot meet by construction; the window chrome
// keeps -1..-4.
//
// There is no refcount and nothing is given back: an int is cheap, and the
// state that owns the handle is what keeps it meaningful.

// cx.focus_handle().
FocusHandle FocusHandleNew(App* app);
FocusHandle FocusHandleNew(Ctx* cx);
// handle.is_focused(window) / handle.focus(window) / window.focused(cx).
bool FocusHandleIsFocused(const Window* win, FocusHandle h);
// contains_focused: the handle, or anything inside the box tracking it.
bool FocusHandleContainsFocused(const Window* win, FocusHandle h);
void FocusHandleFocus(Window* win, FocusHandle h);
FocusHandle WindowFocused(const Window* win);
// The restore half of `previous_focus_handle.take()`: focus it again if the
// frame still has somewhere to put it.
bool FocusHandleRestore(Window* win, FocusHandle h);

void WindowSetFocusId(Window* win, int id);
// window.focused(cx): which element has focus, or 0. What a widget stashes
// before it takes focus for itself.
int WindowFocusedId(const Window* win);
// FocusHandle::contains_focused: focus is on this element, or inside the trap
// it hosts — which is how a container that is not itself focusable asks
// whether what it opened still holds the focus.
bool WindowFocusWithin(const Window* win, int id);
// `previous.focus(window, cx)` on a handle a widget stashed. An element that
// is no longer on screen is a handle whose view has gone, which Rust treats as
// nothing to do; answers whether focus moved.
bool WindowRestoreFocus(Window* win, int id);
// The action a keystroke resolves to for whatever has focus, and the handlers
// it is then offered to. Answers true when one of them kept it — Rust's
// `dispatch_action` plus the `cx.propagate()` that decides how far it goes.
bool WindowDispatchKeyAction(Window* win, int vk, bool shift, bool ctrl,
                             bool alt, bool platform = false,
                             bool function = false);
// The action half on its own: the chord resolved against the contexts over
// the focused element, with no handler run. The matcher is stateful — a
// sequence half-finished is held on it — so a keystroke may only be resolved
// once, which is why the window does it here and hands the answer on rather
// than asking twice. `pending` comes back true when the chord began a
// sequence and belongs to nobody else.
uint32_t WindowResolveKeyAction(Window* win, int vk, bool shift, bool ctrl,
                                bool alt, bool platform, bool function,
                                intptr_t* arg, bool* pending);
// Whether the shortcut modifier is down — `secondary-` in a binding spec:
// Command on macOS, Control everywhere else. The two are separate modifiers
// now, so the code that means "the copy chord" has to say which.
constexpr bool KeySecondary(bool ctrl, bool platform) {
#if GPUI_OS_MAC
    (void)ctrl;
    return platform;
#else
    (void)platform;
    return ctrl;
#endif
}
// The same, for an action already in hand rather than one a keystroke
// resolved to. `arg` is what the action carries.
bool WindowDispatchAction(Window* win, uint32_t action, intptr_t arg = 0);
// The `El::OnKeyDown` handlers over the focused element, innermost first.
// Answers true when one of them stopped propagating.
bool WindowDispatchKeyEvent(Window* win, KeyEvent* ev);
// The `El::OnKeyUp` half of the same chain, run when the key comes back up.
bool WindowDispatchKeyUpEvent(Window* win, KeyEvent* ev);
// cx.on_action: a handler that belongs to the application rather than to any
// element. Tried after the focused element's chain has passed on the action,
// which is where Rust's App-level handlers sit too. A plain function pointer,
// since these are the framework's own and have no view to update.
using ActionFn = void (*)(Window* win, ActionEvent* ev);
void AppOnAction(uint32_t action, ActionFn fn);
void AppQuit(Window* win);
// cx.quit(): the application ends, not just this window. AppQuit closes the
// window it names and the loop ends when the last one has gone, which is the
// same thing while there is only one — a Quit row with two windows open is
// where the two part company.
void AppQuitAll(App* app);
void AppInvalidate(Window* win);
// cx.refresh_windows(): every window this app owns repaints. What a change
// with no one view behind it — the theme, the font size — asks for.
void AppRefreshWindows(App* app);
// A teardown belonging to a layer above this one, run by AppFree once the
// windows are gone. The theme registry's arena is what asked for it: it lives
// in src/ui, which gpui cannot name, and a process-wide table has to be given
// back somewhere or every ASan run reports it. Registering the same function
// twice registers it once.
void AppOnShutdown(void (*fn)());
// window.window_decorations(): whether the frame around this window is ours
// to draw. Windows and the browser answer what the window was opened with;
// macOS keeps its own controls either way. X11 only *asks* for client-side
// decorations, and a window manager that keeps its own frame anyway is what
// makes this a question — a title bar that drew its own controls under one
// would stack a second close button on top of the manager's.
bool WindowClientDecorated(Window* win);

// ─── the application menu bar ────────────────────────────────────────────
//
// cx.set_menus(app_menus()). The menus of the application itself, as opposed
// to the ones an element opens: on macOS they are the bar at the top of the
// screen, which belongs to the front application and not to any of its
// windows. A row carries an action and nothing else, the way Rust's
// `MenuItem::action` does, so choosing it runs the same handler the chord
// bound to it reaches — and the shortcut the OS shows beside the label is
// looked up in the keymap rather than spelled out here.
//
// Nothing else has a menu bar of its own to install into. The call is not
// conditional for that: an application says what its menus are once, and the
// platforms without one ignore it, which is where component::AppMenuBar
// comes in — the same menus drawn into the window.

// gpui::MenuItem. A row with no label is a separator, and a row with rows
// under it opens onto them rather than doing anything itself.
struct MenuRow {
    Str label = {};
    // The action dispatched when the row is chosen, and what it carries.
    // Zero is a row that does nothing, which is what a separator, a submenu
    // and a placeholder row all are.
    uint32_t action = 0;
    intptr_t arg = 0;
    bool separator = false;
    bool disabled = false;
    bool checked = false;
    const MenuRow* submenu = nullptr;
    int submenuN = 0;
};

// gpui::Menu: one menu of the bar, which is a name and its rows.
struct MenuDef {
    Str name = {};
    const MenuRow* items = nullptr;
    int n = 0;
};

// Whether the menus set below reach an OS menu bar. An application asks so it
// can decide whether to draw its own as well — which is what the story does,
// and what Rust decides with `cfg!(target_os = "macos")`.
bool AppHasMenuBar();
// Install these menus as the application's. Called again whenever a row's
// label or checked state changes, which is how the checked appearance and
// theme rows keep up; the platform replaces the bar wholesale.
void AppSetMenus(App* app, const MenuDef* menus, int n);
// What row `id` names, `id` being what a platform menu answers with. The
// numbering is the contract between the two halves — the selectable rows in
// preorder, from 1 — so it is worth being able to ask.
bool AppMenuRowForId(int id, uint32_t* action, intptr_t* arg);
bool AppMenuRowForId(const App* app, int id, uint32_t* action, intptr_t* arg);
// Drops the menu model owned by this App. AppFree calls it before globals are
// destroyed so the platform callback cannot retain a stale App pointer.
void AppMenuClear(App* app);

// window.activate_window() / cx.activate(true): bring this window forward
// and the application with it, restoring it if it was minimized. What a
// click on a system notification asks for.
void AppActivate(Window* win);
void AppMinimize(Window* win);
void AppToggleMaximize(Window* win);
void AppClose(Window* win);
void AppDrag(Window* win);
bool AppIsMaximized(Window* win);
} // namespace gpui

// The entry point every example implements. The platform half of the runtime
// provides wWinMain / main and calls this, so no example spells out either.
// Global scope, so an example that says `using namespace gpui;` can define it
// without qualifying the name.
int GpuiMain(int argc, char** argv);
#endif // GPUI_GPUI_GPUI_H_
