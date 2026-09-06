#ifndef GPUI_UI_ICON_H_
#define GPUI_UI_ICON_H_
/* Themed icon wrapper — crates/ui/src/icon.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// Rust uses an IconNamed trait so application enums can supply paths. The
// POD port represents the trait's one return value directly; any custom icon
// set can return one of these without inheritance, RTTI or retained objects.
struct IconNamed {
    Str path = {};

    static IconNamed From(IconName name);
};

// IconSource: where the icon's picture comes from. The last `Path` or `Data`
// call selects it.
enum class IconSource : uint8_t {
    // An asset path, or the one the IconName stands for.
    Path,
    // Raw SVG source, without registering an asset path.
    Data,
};

struct Icon {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    IconName name = IconName::None;
    Str path = {};
    // `Icon::data`: the SVG source, copied into the frame arena so the input
    // need not outlive the call. Rust shares it behind an Arc when the icon
    // is cloned; an Icon here lives one frame and is not cloned.
    Str data = {};
    IconSource source = IconSource::Path;
    float size = 0;
    float rotation = 0;
    Rgba color = {};
    bool hasSize = false;
    bool hasColor = false;

    static Icon* New(Ctx* cx, IconName name);
    static Icon* New(Ctx* cx, IconNamed named);
    static Icon* Empty(Ctx* cx);
    // Replaces any previously set path or SVG data.
    Icon* Path(Str assetPath);
    // Set raw SVG bytes without registering an asset path. Replaces any
    // previously set path or data. Parsing and rendering follow the SVG
    // reader every asset goes through.
    Icon* Data(Str svg);
    Icon* Size(float v);
    Icon* Size(UiSize v);
    Icon* Color(Rgba c);
    // GPUI rotations are turns: 0.25 is ninety degrees clockwise.
    Icon* Transform(float turns);
    Icon* Rotate(float turns);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_ICON_H_
