#ifndef GPUI_COMPONENT_SHELL_NAVIGATION_MOD_H_
#define GPUI_COMPONENT_SHELL_NAVIGATION_MOD_H_

// crates/component-shell/src/shell/navigation/mod.rs: Icon and the Sidebar
// parts. What is declared here beyond the registrations is what the Rust
// modules' own tests reach, exposed for tests/ComponentShellTests.cpp.

#include "component_shell/support.h"

namespace gpui::component_shell::navigation {

// ─── icon.rs ──────────────────────────────────────────────────────────────

// size: one of the four size literals; "unsupported Icon size `<value>`"
// otherwise.
bool IconSize(Str value, UiSize* out, Str* error);

// rotation: finite radians inside f32's range; "Icon.rotate expects finite
// radians representable as f32" otherwise.
bool IconRotation(double value, float* out, Str* error);

// icon_path: a nonblank path with no root, drive prefix or `..` component;
// "Icon path must not be empty" or "Icon path must stay inside the
// application asset root" otherwise.
bool IconPath(Str path, Str* error);

// ─── sidebar.rs ───────────────────────────────────────────────────────────

// require_registered_child: "<parent> accepts only <expected> children;
// received <actual or `an ordinary element`>" unless `actual` is `expected`.
bool RequireRegisteredChild(const char* parent, const char* expected,
                            const char* actual, Str* error);

// require_default_item_style: SidebarMenuItem takes no shell style.
bool RequireDefaultItemStyle(bool styled, Str* error);

} // namespace gpui::component_shell::navigation
#endif // GPUI_COMPONENT_SHELL_NAVIGATION_MOD_H_
