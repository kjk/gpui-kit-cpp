#ifndef GPUI_EXAMPLES_SHOWCASE_PALETTE_H_
#define GPUI_EXAMPLES_SHOWCASE_PALETTE_H_
/* The example palette — crates/base/examples/shared/palette.rs.

   crates/base is the *unstyled* layer, so its examples supply their own
   colors: the Rust pages write `rgb(0x171717)` and friends inline. This is
   the table those literals are resolved through, so one set of hard-coded
   light values reads correctly on a dark desktop too — `example_rgb(0xd4d4d4)`
   is the border in either appearance.

   Rust keeps the active palette in a `thread_local`, because Components and
   Motion are two standalone apps that the wasm host embeds together. There is
   one of each here and no embedding host, so the active palette is a file
   static with the same lifetime.

   Two things upstream has that this does not, and both are the same gap:
   `Window::appearance()` and `cx.observe_window_appearance(..)`. This tree
   has no portable seam for the desktop's light/dark setting — Windows reads
   it in `window_win.cpp` for the DWM frame and nowhere else — so the palette
   is activated from the theme mode the example itself sets, which is what
   chooses light or dark for everything else in the window anyway. A page
   never asks which appearance is in force; it asks for a colour by its light
   value, exactly as the Rust pages do. */

#include "gpui.h"

using namespace gpui;

struct ExamplePalette {
    uint32_t canvas;
    uint32_t surface;
    uint32_t elevated;
    uint32_t foreground;
    uint32_t mutedForeground;
    uint32_t subtleForeground;
    uint32_t border;
    uint32_t strongBorder;
    uint32_t hover;
    uint32_t selected;
    uint32_t accent;
    uint32_t accentForeground;

    static ExamplePalette ForDark(bool dark);
    // The light literal a page wrote, resolved into this palette. A colour
    // the table does not name is its own answer, which is what keeps a page's
    // brand colours (the color picker's swatches) where they were.
    uint32_t Resolve(uint32_t lightColor) const;
};

// The two palettes, value for value from
// crates/base/examples/shared/palette.rs.
inline ExamplePalette ExamplePalette::ForDark(bool dark) {
    ExamplePalette p;
    if (dark) {
        p.canvas = 0x0e0e0e;
        p.surface = 0x171717;
        p.elevated = 0x262626;
        p.foreground = 0xffffff;
        p.mutedForeground = 0xa3a3a3;
        p.subtleForeground = 0x737373;
        p.border = 0x404040;
        p.strongBorder = 0xd4d4d4;
        p.hover = 0x262626;
        p.selected = 0x303030;
        p.accent = 0xffffff;
        p.accentForeground = 0x171717;
        return p;
    }
    p.canvas = 0xffffff;
    p.surface = 0xffffff;
    p.elevated = 0xf5f5f5;
    p.foreground = 0x171717;
    p.mutedForeground = 0x525252;
    p.subtleForeground = 0x737373;
    p.border = 0xd4d4d4;
    p.strongBorder = 0x171717;
    p.hover = 0xf5f5f5;
    p.selected = 0xf0f0f0;
    p.accent = 0x171717;
    p.accentForeground = 0xffffff;
    return p;
}

inline uint32_t ExamplePalette::Resolve(uint32_t lightColor) const {
    switch (lightColor) {
        case 0xffffff:
            return surface;
        case 0x171717:
        case 0x262626:
            return foreground;
        case 0x404040:
            return strongBorder;
        case 0x525252:
            return mutedForeground;
        case 0x737373:
        case 0xa3a3a3:
            return subtleForeground;
        case 0x71717a:
            return mutedForeground;
        case 0xd4d4d4:
        case 0xd4d4d8:
        case 0xe5e5e5:
        case 0xe5e7eb:
            return border;
        case 0xf0f0f0:
            return selected;
        case 0xf4f4f5:
        case 0xf5f5f5:
            return hover;
        default:
            break;
    }
    // The syntax-highlighting colours, which are only remapped on the dark
    // canvas: a light theme's token colours already read against white.
    if (canvas == 0x0e0e0e) {
        switch (lightColor) {
            case 0x007fff:
            case 0x0433ff:
                return 0x79c0ff;
            case 0x036a07:
                return 0x7ee787;
            case 0xc5060b:
                return 0xff7b72;
            case 0x0000a2:
            case 0x6f42c1:
                return 0xd2a8ff;
            case 0x333333:
                return 0xc9d1d9;
            default:
                break;
        }
    }
    return lightColor;
}

// One active palette per binary. Rust keeps it in a thread_local because
// its wasm host embeds Components and Motion in one module; here the two are
// separate binaries that each compile this header.
inline ExamplePalette gActive = ExamplePalette::ForDark(false);

inline void PaletteActivate(bool dark) {
    gActive = ExamplePalette::ForDark(dark);
}

inline const ExamplePalette& PaletteActive() {
    return gActive;
}

inline Rgba RgbU32(uint32_t v) {
    return Rgb((uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 8) & 0xff),
               (uint8_t)(v & 0xff));
}

inline Rgba ExampleRgb(uint32_t lightColor) {
    return RgbU32(gActive.Resolve(lightColor));
}

inline Rgba ExampleCanvas() {
    return RgbU32(gActive.canvas);
}

#endif // GPUI_EXAMPLES_SHOWCASE_PALETTE_H_
