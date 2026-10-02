#ifndef GPUI_SRC_UI_SIZING_H_
#define GPUI_SRC_UI_SIZING_H_
/* Shared types for the themed gpui-kit façade
   crates/ui/src/sizing.rs */

#include "gpui/gpui.h"
#include "base/lib.h"
#include "ui/theme.h"

namespace gpui {

// crates/ui's payload enum Size. `Kind` is deliberately unscoped inside the
// struct so existing C++ reads and switches keep the concise UiSize::Small
// spelling while Custom(px) retains Rust's Size::Size(Pixels) payload.
struct UiSize {
    enum class Kind : uint8_t {
        Size,
        XSmall,
        Small,
        Medium,
        Large
    };

    struct Constant {
        Kind kind;
        constexpr operator Kind() const { return kind; }
    };

    static constexpr Constant Size{Kind::Size};
    static constexpr Constant XSmall{Kind::XSmall};
    static constexpr Constant Small{Kind::Small};
    static constexpr Constant Medium{Kind::Medium};
    static constexpr Constant Large{Kind::Large};

    Kind kind = Kind::Medium;
    float pixels = 0;

    constexpr UiSize() = default;
    constexpr UiSize(Kind value) : kind(value) {}
    constexpr UiSize(Constant value) : kind(value.kind) {}
    static constexpr UiSize Custom(float value) {
        UiSize out(Kind::Size);
        out.pixels = value;
        return out;
    }
    constexpr operator Kind() const { return kind; }
    UiSize& operator=(Kind value) {
        kind = value;
        pixels = 0;
        return *this;
    }
    UiSize& operator=(Constant value) { return *this = value.kind; }
};

constexpr bool operator==(UiSize a, UiSize b) {
    return a.kind == b.kind &&
           (a.kind != UiSize::Kind::Size || a.pixels == b.pixels);
}
constexpr bool operator!=(UiSize a, UiSize b) {
    return !(a == b);
}
constexpr bool operator==(UiSize a, UiSize::Constant b) {
    return a.kind == b.kind;
}
constexpr bool operator==(UiSize::Constant a, UiSize b) {
    return b == a;
}
constexpr bool operator!=(UiSize a, UiSize::Constant b) {
    return !(a == b);
}
constexpr bool operator!=(UiSize::Constant a, UiSize b) {
    return !(a == b);
}
constexpr bool operator<(UiSize a, UiSize::Constant b) {
    return (uint8_t)a.kind < (uint8_t)b.kind;
}
constexpr bool operator<(UiSize::Constant a, UiSize b) {
    return (uint8_t)a.kind < (uint8_t)b.kind;
}
constexpr bool operator>(UiSize a, UiSize::Constant b) {
    return (uint8_t)a.kind > (uint8_t)b.kind;
}
constexpr bool operator>(UiSize::Constant a, UiSize b) {
    return (uint8_t)a.kind > (uint8_t)b.kind;
}

inline float UiSizeAsF32(UiSize s) {
    switch (s) {
        case UiSize::Size:
            return s.pixels;
        case UiSize::XSmall:
            return 0;
        case UiSize::Small:
            return 1;
        case UiSize::Large:
            return 3;
        default:
            return 2;
    }
}

inline Str UiSizeAsStr(UiSize s) {
    switch (s) {
        case UiSize::Size:
            return StrL("custom");
        case UiSize::XSmall:
            return StrL("xs");
        case UiSize::Small:
            return StrL("sm");
        case UiSize::Large:
            return StrL("lg");
        default:
            return StrL("md");
    }
}

inline UiSize UiSizeFromStr(Str text) {
    if (StrEqI(text, "xs") || StrEqI(text, "xsmall")) {
        return UiSize::XSmall;
    }
    if (StrEqI(text, "sm") || StrEqI(text, "small")) {
        return UiSize::Small;
    }
    if (StrEqI(text, "lg") || StrEqI(text, "large")) {
        return UiSize::Large;
    }
    return UiSize::Medium;
}

inline UiSize UiSizeSmaller(UiSize s) {
    switch (s) {
        case UiSize::Size:
            return UiSize::Custom(s.pixels * 0.2f);
        case UiSize::Small:
            return UiSize::XSmall;
        case UiSize::Medium:
            return UiSize::Small;
        case UiSize::Large:
            return UiSize::Medium;
        default:
            return UiSize::XSmall;
    }
}

inline UiSize UiSizeLarger(UiSize s) {
    switch (s) {
        case UiSize::Size:
            return UiSize::Custom(s.pixels * 1.2f);
        case UiSize::XSmall:
            return UiSize::Small;
        case UiSize::Small:
            return UiSize::Medium;
        case UiSize::Medium:
            return UiSize::Large;
        default:
            return UiSize::Large;
    }
}

// These names follow the pinned implementation, whose max chooses the
// visually smaller size and min chooses the visually larger one.
inline UiSize UiSizeMax(UiSize a, UiSize b) {
    if (a.kind == UiSize::Kind::Size && b.kind == UiSize::Kind::Size) {
        return UiSize::Custom(std::min(a.pixels, b.pixels));
    }
    if (a.kind == UiSize::Kind::Size) {
        return a;
    }
    if (b.kind == UiSize::Kind::Size) {
        return b;
    }
    return UiSizeAsF32(a) < UiSizeAsF32(b) ? a : b;
}

inline UiSize UiSizeMin(UiSize a, UiSize b) {
    if (a.kind == UiSize::Kind::Size && b.kind == UiSize::Kind::Size) {
        return UiSize::Custom(std::max(a.pixels, b.pixels));
    }
    if (a.kind == UiSize::Kind::Size) {
        return a;
    }
    if (b.kind == UiSize::Kind::Size) {
        return b;
    }
    return UiSizeAsF32(a) > UiSizeAsF32(b) ? a : b;
}

inline float UiSizePx(UiSize s) {
    switch (s) {
        case UiSize::Size:
            return s.pixels;
        case UiSize::XSmall:
            return 20;
        case UiSize::Small:
            return 24;
        case UiSize::Large:
            return 36;
        default:
            return 28;
    }
}

// Icon::with_size, crates/ui/src/icon.rs: size_3 / size_3p5 / size_4 / size_6,
// rems at the window's rem size; a custom Size is pixels. Not the
// control-height scale above — an icon inside a Medium control is size_4.
inline float UiIconPx(const Ctx* cx, UiSize s) {
    switch (s) {
        case UiSize::Size:
            return s.pixels;
        case UiSize::XSmall:
            return Rems(cx, 0.75f);
        case UiSize::Small:
            return Rems(cx, 0.875f);
        case UiSize::Large:
            return Rems(cx, 1.5f);
        default:
            return Rems(cx, 1.f);
    }
}

// Size::table_cell_padding, crates/ui/src/sizing.rs. The table's own cells
// are padded by hand; this is what the loading view measures its rows with,
// which is the one place Rust reads the scale rather than a constant.
inline Edges UiTableCellPadding(UiSize s) {
    // Edges::New is left, right, top, bottom — the shared Rect's field order,
    // which is not the one Rust's Edges<Pixels> lists. Named rather than
    // braced so a reader does not have to remember which.
    switch (s) {
        case UiSize::XSmall:
            return Edges::New(4, 4, 2, 2);
        case UiSize::Small:
            return Edges::New(6, 6, 3, 3);
        case UiSize::Large:
            return Edges::New(12, 12, 8, 8);
        default:
            return Edges::New(8, 8, 4, 4);
    }
}

// Size::table_row_height: 26 / 30 / 32 / 40.
inline float UiTableRowHeight(UiSize s) {
    switch (s) {
        case UiSize::Size:
            return s.pixels;
        case UiSize::XSmall:
            return 26;
        case UiSize::Small:
            return 30;
        case UiSize::Large:
            return 40;
        default:
            return 32;
    }
}

inline float UiFontPx(UiSize s) {
    switch (s) {
        case UiSize::Size:
            return s.pixels * 0.875f;
        case UiSize::XSmall:
            return 11;
        case UiSize::Small:
            return 12;
        case UiSize::Large:
            return 16;
        default:
            return 14;
    }
}

inline float UiInputPadX(UiSize s) {
    switch (s) {
        case UiSize::Large:
            return 12;
        case UiSize::Medium:
            return 10;
        case UiSize::Small:
            return 8;
        case UiSize::XSmall:
            return 4;
        default:
            return 8;
    }
}

inline float UiInputFontPx(UiSize s) {
    switch (s) {
        case UiSize::Size:
            return s.pixels * 0.875f;
        case UiSize::XSmall:
            return 12;
        case UiSize::Small:
        case UiSize::Medium:
            return 14;
        default:
            return 16;
    }
}

inline float UiInputPadY(UiSize s) {
    switch (s) {
        case UiSize::Large:
            return 10;
        case UiSize::Medium:
            return 8;
        case UiSize::Small:
            return 2;
        case UiSize::XSmall:
            return 0;
        default:
            return 2;
    }
}

// input_h: h_11 / h_8 / h_6 / h_5, rems at the window's rem size.
inline float UiInputHeight(const Ctx* cx, UiSize s) {
    switch (s) {
        case UiSize::Large:
            return Rems(cx, 2.75f);
        case UiSize::Medium:
            return Rems(cx, 2.f);
        case UiSize::Small:
            return Rems(cx, 1.5f);
        case UiSize::XSmall:
            return Rems(cx, 1.25f);
        default:
            return Rems(cx, 1.5f);
    }
}

// DROPDOWN_LIST_PADDING: the inset between an input's dropdown popup and its
// list rows.
constexpr float kDropdownListPadding = 4.f;

// Size::list_px: the horizontal padding of a row in an input's dropdown list.
// The trigger insets its text by a 1px border plus input_px; the popup insets
// a row by kDropdownListPadding. The row pads the difference, so the chosen
// item's text lines up with the trigger's.
inline float UiListPadX(UiSize s) {
    return UiInputPadX(s) + 1.f - kDropdownListPadding;
}

// list_py: py_2 / py_0p5 / py_1, rems at the window's rem size.
inline float UiListPadY(const Ctx* cx, UiSize s) {
    switch (s) {
        case UiSize::Large:
            return Rems(cx, 0.5f);
        case UiSize::Small:
            return Rems(cx, 0.125f);
        default:
            return Rems(cx, 0.25f);
    }
}

// size_with: size_11 / size_8 / size_5 / size_4, rems at the window's rem
// size; a custom Size is pixels.
inline float UiSizeWithPx(const Ctx* cx, UiSize s) {
    switch (s) {
        case UiSize::Size:
            return s.pixels;
        case UiSize::Large:
            return Rems(cx, 2.75f);
        case UiSize::Medium:
            return Rems(cx, 2.f);
        case UiSize::Small:
            return Rems(cx, 1.25f);
        default:
            return Rems(cx, 1.f);
    }
}

// StyleSized<T> projects to free El refinements because C++ puts the fluent
// style vocabulary on El itself rather than using a blanket extension trait.
inline El* UiInputTextSize(El* e, UiSize s) {
    return e->Font(UiInputFontPx(s));
}
inline El* UiInputPadL(El* e, UiSize s) {
    return e->PadL(UiInputPadX(s));
}
inline El* UiInputPadR(El* e, UiSize s) {
    return e->PadR(UiInputPadX(s));
}
inline El* UiInputPadX(El* e, UiSize s) {
    return e->PadX(UiInputPadX(s));
}
inline El* UiInputPadY(El* e, UiSize s) {
    return e->PadY(UiInputPadY(s));
}
inline El* UiInputH(const Ctx* cx, El* e, UiSize s) {
    return e->H(UiInputHeight(cx, s));
}
inline El* UiInputSize(const Ctx* cx, El* e, UiSize s) {
    return UiInputH(cx, UiInputPadY(UiInputPadX(e, s), s), s);
}
inline El* UiListPadX(El* e, UiSize s) {
    return e->PadX(UiListPadX(s));
}
inline El* UiListPadY(const Ctx* cx, El* e, UiSize s) {
    return e->PadY(UiListPadY(cx, s));
}
inline El* UiListSize(const Ctx* cx, El* e, UiSize s) {
    return UiInputTextSize(UiListPadY(cx, UiListPadX(e, s), s), s);
}
inline El* UiSizeWith(const Ctx* cx, El* e, UiSize s) {
    float px = UiSizeWithPx(cx, s);
    return e->W(px)->H(px);
}
// table_cell_size's text: text_xs / text_sm / text_sm / text_base, the
// Input ladder; 0 for a custom Size, which sets none.
inline float UiTableCellFontPx(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
            return 12;
        case UiSize::Small:
        case UiSize::Medium:
            return 14;
        case UiSize::Large:
            return 16;
        default:
            return 0;
    }
}
inline El* UiTableCellSize(El* e, UiSize s) {
    Edges pad = UiTableCellPadding(s);
    if (float font = UiTableCellFontPx(s)) {
        e->Font(font);
    }
    return e->PadL(pad.left)->PadR(pad.right)->PadT(pad.top)->PadB(pad.bottom);
}
// button_text_size: text_xs, text_sm for Small and Medium, text_base.
inline float UiButtonTextPx(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
            return 12;
        case UiSize::Small:
        case UiSize::Medium:
            return 14;
        default:
            return 16;
    }
}
inline El* UiButtonTextSize(El* e, UiSize s) {
    return e->Font(UiButtonTextPx(s));
}

namespace component {

// `div().id(name).track_focus(..).on_click(..)`: the name, the hit target and
// the focus, which the fold down from the root turns into one number.
inline El* BindClick(El* e, Str name, Listener onClick) {
    e->PathId(name);
    if (onClick.IsValid()) {
        e->OnClick(onClick);
    }
    return e;
}

// The same as BindClick for an element that is a hit target and nothing else.
// A name only has to be unique among its siblings, because the id is the fold
// of the path down to it, which is what a GlobalElementId is — `("col-header",
// ix)` upstream, not `format!("{id}-col-header-{ix}")`.
//
// Hit-testable and nothing else, which is what `.id()` on its own is. The
// parts a widget builds by the hundred — a table's rows, its cells, its
// heads — are all `.id()` upstream; the one focusable element is the widget
// itself, so the keyboard reaches it once rather than row by row.
inline El* BindPathClick(El* e, Str name, Listener onClick) {
    e->PathClick(name);
    if (onClick.IsValid()) {
        e->OnClick(onClick);
    }
    return e;
}
} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SIZING_H_
