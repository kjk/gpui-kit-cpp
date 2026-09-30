// crates/component-shell/src/shell/compound/common.rs

#include "component_shell/compound/common.h"

#include <float.h>
#include <limits.h>
#include <math.h>

namespace gpui::component_shell::compound::common {

bool NonemptyId(Str id, const char* component, Str* error) {
    if (len(StrTrimAscii(id)) > 0) return true;
    *error = fmt("%s(id) expects a nonempty string id", Str(component));
    return false;
}

bool NonnegativeUsize(double value, Str label, uint64_t* out, Str* error) {
    // 2^64: `usize::MAX as f64` on the 64-bit targets Rust builds for.
    if (!isfinite(value) || value < 0 || value != floor(value) ||
        value >= 18446744073709551616.0) {
        *error = fmt("%s expects an exactly representable nonnegative integer",
                     label);
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

bool FiniteF32(double value, Str label, float* out, Str* error) {
    if (!isfinite(value) || value < -(double)FLT_MAX ||
        value > (double)FLT_MAX) {
        *error = fmt("%s expects a finite number representable as f32", label);
        return false;
    }
    *out = (float)value;
    return true;
}

int UsizeInt(uint64_t value) {
    return value > (uint64_t)INT_MAX ? INT_MAX : (int)value;
}

} // namespace gpui::component_shell::compound::common
