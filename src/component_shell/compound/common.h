#ifndef GPUI_COMPONENT_SHELL_COMPOUND_COMMON_H_
#define GPUI_COMPONENT_SHELL_COMPOUND_COMMON_H_

// crates/component-shell/src/shell/compound/common.rs: the argument checks
// the compound family shares. Each answers false with Rust's message in
// `*error`, a temporary string.

#include "component_shell/support.h"

namespace gpui::component_shell::compound::common {

// nonempty_id: "<component>(id) expects a nonempty string id" when `id` is
// blank. Rust trims Unicode whitespace; an id's is in practice only ever the
// ASCII kind.
bool NonemptyId(Str id, const char* component, Str* error);

// nonnegative_usize: a finite, nonnegative, whole number below 2^64 (Rust's
// `usize::MAX as f64` rounds up to 2^64, hence the exclusive bound), or
// "<label> expects an exactly representable nonnegative integer".
bool NonnegativeUsize(double value, Str label, uint64_t* out, Str* error);

// finite_f32: a finite number inside f32's range, or "<label> expects a
// finite number representable as f32".
bool FiniteF32(double value, Str label, float* out, Str* error);

// A usize as the int the components count in.
int UsizeInt(uint64_t value);
// A usize for a component that counts in 64 bits: Pagination's pages.
int64_t UsizeInt64(uint64_t value);

} // namespace gpui::component_shell::compound::common
#endif // GPUI_COMPONENT_SHELL_COMPOUND_COMMON_H_
