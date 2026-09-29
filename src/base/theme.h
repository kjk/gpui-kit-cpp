#ifndef GPUI_BASE_THEME_H_
#define GPUI_BASE_THEME_H_
/* Base Theme — crates/base/src/theme.rs

   This is the application-owned Base global Rust exposes: semantic tokens
   plus behavior defaults projected by the UI layer. It has no dependency on
   the component palette. */

#include "base/theme_tokens.h"
#include "base/scrollbar.h"
#include "base/plot.h"

namespace gpui {

namespace base_theme {

enum class ThemeAppearance : uint8_t {
    Light,
    Dark,
};

struct ScrollbarTheme {
    ScrollbarMode mode = ScrollbarMode::Scrolling;
    ScrollbarMotion motion = {};
    ScrollbarStyles styles = {};

    static ScrollbarTheme New() { return {}; }
    ScrollbarTheme WithMode(ScrollbarMode value) const {
        ScrollbarTheme copy = *this;
        copy.mode = value;
        return copy;
    }
    ScrollbarTheme WithMotion(ScrollbarMotion value) const {
        ScrollbarTheme copy = *this;
        copy.motion = value;
        return copy;
    }
    ScrollbarTheme WithStyles(const ScrollbarStyles& value) const {
        ScrollbarTheme copy = *this;
        copy.styles = value;
        return copy;
    }
    ScrollbarMode Mode() const { return mode; }
    ScrollbarMotion Motion() const { return motion; }
    const ScrollbarStyles& Styles() const { return styles; }
};

struct ResizableTheme {
    Rgba handle = {0, 0, 0, 0};
    Rgba activeHandle = {0, 0, 0, 0};
    bool hasHandle = false;
    bool hasActiveHandle = false;
};

// PlotTheme: global defaults used by base/plot. `motion` defaults to
// motionless; styled layers project their own timing, and Base never installs
// a fade or glide of its own.
struct PlotTheme {
    plot::PlotMotion motion = {};

    static PlotTheme New() { return {}; }
    PlotTheme WithMotion(const plot::PlotMotion& value) const {
        PlotTheme copy = *this;
        copy.motion = value;
        return copy;
    }
    const plot::PlotMotion& Motion() const { return motion; }
};

struct Theme {
    ThemeAppearance appearance = ThemeAppearance::Light;
    SemanticThemeTokens tokens;
    ScrollbarTheme scrollbar = {};
    ResizableTheme resizable = {};
    PlotTheme plot = {};

    // Theme::global clones the installed value or returns Default; global_mut
    // installs Default on first access and returns the application-owned one.
    static Theme Global(const App* app);
    static Theme* GlobalMut(App* app);
};

} // namespace base_theme

// Compatibility names from before the two Rust crates' Theme types were
// separated into module namespaces. These are aliases, so all callers and
// globals share one type and one state.
using BaseScrollbarTheme = base_theme::ScrollbarTheme;
using BaseResizableTheme = base_theme::ResizableTheme;
using BaseThemeAppearance = base_theme::ThemeAppearance;
using BaseTheme = base_theme::Theme;

BaseTheme* BaseThemeGlobal(App* app);
const BaseTheme* BaseThemeGlobal(const App* app);
void BaseThemeSet(App* app, const BaseTheme& theme);

} // namespace gpui
#endif // GPUI_BASE_THEME_H_
