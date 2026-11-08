#ifndef GPUI_BASE_STYLED_H_
#define GPUI_BASE_STYLED_H_
/* StyledExt — crates/base/src/styled.rs

   Fluent style methods are El methods here. These two constructors are the
   public h_flex/v_flex helpers Rust re-exports. */

#include "base/theme_tokens.h"

namespace gpui {

// A row that centers its children on the cross axis.
//
// See StyledExt::HFlex for the cross-axis rule, which is not symmetric with
// VFlex.
inline El* HFlex(Arena* a) {
    return Div(a)->FlexRow()->ItemsCenter();
}

// A column whose children stretch across the cross axis.
//
// See StyledExt::VFlex for the cross-axis rule, which is not symmetric with
// HFlex.
inline El* VFlex(Arena* a) {
    return Div(a)->FlexCol();
}

// div().absolute().inset_0().flex().items_center().justify_center(): a
// layer over the whole of its parent with its content centered.
inline El* CenteredOverlay(Arena* a) {
    return Div(a)->Absolute()->Inset0()->Flex()->ItemsCenter()->JustifyCenter();
}

inline BoxShadow box_shadow(float x, float y, float blur, float spread,
                            Hsla color) {
    BoxShadow out;
    out.x = x;
    out.y = y;
    out.blur = blur;
    out.spread = spread;
    out.color = HslaToRgba(color);
    out.inset = false;
    return out;
}

enum class RoleOverrideKind : uint8_t {
    Implicit,
    Presentational,
    Role
};

// RoleOverride::resolve: false means no role should be written at all.
struct RoleOverride {
    RoleOverrideKind kind = RoleOverrideKind::Implicit;
    AccessibilityRole role = AccessibilityRole::None;

    static RoleOverride Implicit();
    static RoleOverride Presentational();
    static RoleOverride Explicit(AccessibilityRole role);
    bool Resolve(AccessibilityRole defaultRole, AccessibilityRole* out) const;
};

// C++ elements already own the fluent Styled surface, so the Rust extension
// trait is a stateless set of operations over El*. It keeps source names and
// semantics without wrapping or owning an element.
struct StyledExt {
    static El* RefineStyle(El* element, const Style& style, uint32_t fields);
    // Lays children out in a row, centered on the cross axis.
    //
    // The centering is the desktop default for a row of controls — an icon
    // beside its label lines up without either side asking for it — but it
    // is *not* the mirror image of VFlex, which leaves the cross axis
    // stretching. A column placed in a row therefore does not take the row's
    // height: it takes its content's height and is centered inside the row.
    // When its content is taller than the row, it overflows equally above
    // and below, so the column's header is pushed off the top edge and
    // clipped.
    //
    // Give a full-height column `H(kFill)` (or the row `ItemsStart()` /
    // `ItemsStretch()`) whenever the child owns a header, a footer, or a
    // scroll region that has to resolve against the row's height:
    //
    //     // A sidebar beside a detail pane, both spanning the full height.
    //     HFlex(a)->SizeFull()->Child(Div(a)->W(256)->H(kFill));
    static El* HFlex(El* element);
    // Lays children out in a column, stretching them across the cross axis.
    //
    // Unlike HFlex this installs no cross-axis alignment, so a child without
    // a width fills the column. See HFlex for the asymmetry.
    static El* VFlex(El* element);
    static El* Paddings(El* element, Edges paddings);
    static El* Margins(El* element, Edges margins);
    static El* DebugRed(El* element);
    static El* DebugBlue(El* element);
    static El* DebugYellow(El* element);
    static El* DebugGreen(El* element);
    static El* DebugPink(El* element);
    static El* DebugFocused(El* element, FocusHandle focus,
                            const Window* window);
    static El* FontThin(El* element);
    static El* FontExtraLight(El* element);
    static El* FontLight(El* element);
    static El* FontNormal(El* element);
    static El* FontMedium(El* element);
    static El* FontSemibold(El* element);
    static El* FontBold(El* element);
    static El* FontExtraBold(El* element);
    static El* FontBlack(El* element);
    static El* CornerRadii(El* element, Corners radius);
};

// The Rust inspector derives this table. The port's inspector is manual, so
// it consumes the method names rather than generic FunctionReflection values.
const char* const* StyledExtReflectionMethods(int* count);

inline RoleOverride RoleOverride::Implicit() {
    return {};
}

inline RoleOverride RoleOverride::Presentational() {
    RoleOverride out;
    out.kind = RoleOverrideKind::Presentational;
    return out;
}

inline RoleOverride RoleOverride::Explicit(AccessibilityRole value) {
    RoleOverride out;
    out.kind = RoleOverrideKind::Role;
    out.role = value;
    return out;
}

inline bool RoleOverride::Resolve(AccessibilityRole defaultRole,
                                  AccessibilityRole* out) const {
    if (kind == RoleOverrideKind::Presentational) {
        return false;
    }
    if (out) {
        *out = kind == RoleOverrideKind::Role ? role : defaultRole;
    }
    return true;
}

inline El* StyledExt::RefineStyle(El* element, const Style& style,
                                  uint32_t fields) {
    return element ? element->Refine(style, fields) : nullptr;
}

inline El* StyledExt::HFlex(El* element) {
    return element ? element->FlexRow()->ItemsCenter() : nullptr;
}

inline El* StyledExt::VFlex(El* element) {
    return element ? element->FlexCol() : nullptr;
}

inline El* StyledExt::Paddings(El* element, Edges value) {
    return element ? element->PadL(value.left)
                         ->PadR(value.right)
                         ->PadT(value.top)
                         ->PadB(value.bottom)
                   : nullptr;
}

inline El* StyledExt::Margins(El* element, Edges value) {
    return element ? element->MarginL(value.left)
                         ->MarginR(value.right)
                         ->MarginT(value.top)
                         ->MarginB(value.bottom)
                   : nullptr;
}

inline El* StyledDebug(El* element, float h, float s, float l) {
#if defined(DEBUG) || !defined(NDEBUG)
    return element ? element->Border(1, HslaToRgba(HslaNew(h / 360.f, s / 100.f,
                                                           l / 100.f, 1.f)))
                   : nullptr;
#else
    (void)h;
    (void)s;
    (void)l;
    return element;
#endif
}

inline El* StyledExt::DebugRed(El* element) {
    return StyledDebug(element, 0.f, 72.2f, 50.6f);
}
inline El* StyledExt::DebugBlue(El* element) {
    return StyledDebug(element, 217.2f, 91.2f, 59.8f);
}
inline El* StyledExt::DebugYellow(El* element) {
    return StyledDebug(element, 47.9f, 95.8f, 53.1f);
}
inline El* StyledExt::DebugGreen(El* element) {
    return StyledDebug(element, 142.1f, 70.6f, 45.3f);
}
inline El* StyledExt::DebugPink(El* element) {
    return StyledDebug(element, 330.4f, 81.2f, 60.4f);
}

inline El* StyledExt::DebugFocused(El* element, FocusHandle focus,
                                   const Window* window) {
#if defined(DEBUG) || !defined(NDEBUG)
    return FocusHandleContainsFocused(window, focus) ? DebugBlue(element)
                                                     : element;
#else
    (void)focus;
    (void)window;
    return element;
#endif
}

inline El* StyledExt::FontThin(El* element) {
    return element ? element->Weight(FontWeight::Thin) : nullptr;
}
inline El* StyledExt::FontExtraLight(El* element) {
    return element ? element->Weight(FontWeight::ExtraLight) : nullptr;
}
inline El* StyledExt::FontLight(El* element) {
    return element ? element->Weight(FontWeight::Light) : nullptr;
}
inline El* StyledExt::FontNormal(El* element) {
    return element ? element->Weight(FontWeight::Normal) : nullptr;
}
inline El* StyledExt::FontMedium(El* element) {
    return element ? element->Weight(FontWeight::Medium) : nullptr;
}
inline El* StyledExt::FontSemibold(El* element) {
    return element ? element->Weight(FontWeight::Semibold) : nullptr;
}
inline El* StyledExt::FontBold(El* element) {
    return element ? element->Weight(FontWeight::Bold) : nullptr;
}
inline El* StyledExt::FontExtraBold(El* element) {
    return element ? element->Weight(FontWeight::ExtraBold) : nullptr;
}
inline El* StyledExt::FontBlack(El* element) {
    return element ? element->Weight(FontWeight::Black) : nullptr;
}

inline El* StyledExt::CornerRadii(El* element, Corners radius) {
    return element
               ? element->Corners(radius.tl, radius.tr, radius.br, radius.bl)
               : nullptr;
}

inline const char* const* StyledExtReflectionMethods(int* count) {
    static const char* methods[] = {
        "refine_style",    "h_flex",     "v_flex",         "paddings",
        "margins",         "debug_red",  "debug_blue",     "debug_yellow",
        "debug_green",     "debug_pink", "debug_focused",  "font_thin",
        "font_extralight", "font_light", "font_normal",    "font_medium",
        "font_semibold",   "font_bold",  "font_extrabold", "font_black",
        "corner_radii",
    };
    if (count) {
        *count = (int)(sizeof(methods) / sizeof(methods[0]));
    }
    return methods;
}

} // namespace gpui
#endif // GPUI_BASE_STYLED_H_
