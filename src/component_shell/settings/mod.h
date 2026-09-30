#ifndef GPUI_COMPONENT_SHELL_SETTINGS_MOD_H_
#define GPUI_COMPONENT_SHELL_SETTINGS_MOD_H_

// crates/component-shell/src/shell/settings/mod.rs: the helper its own tests
// reach.

#include "component_shell/support.h"

namespace gpui::component_shell::settings {

// positive: "<label> expects a positive finite pixel value" unless `value` is
// a positive finite f32.
bool Positive(double value, const char* label, float* out, Str* error);

} // namespace gpui::component_shell::settings
#endif // GPUI_COMPONENT_SHELL_SETTINGS_MOD_H_
