// crates/component-shell/src/shell/structured/common.rs

#include "component_shell/structured/mod.h"

#include <float.h>
#include <math.h>

namespace gpui::component_shell::structured {

bool PositiveUsize(double value, Str label, uint64_t* out, Str* error) {
    // `value >= usize::MAX as f64`: the cast rounds up to 2^64.
    if (!isfinite(value) || value < 1.0 || value != floor(value) ||
        value >= 18446744073709551616.0) {
        *error =
            fmt("%s expects an exactly representable positive integer", label);
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

bool PositiveU16(double value, Str label, uint16_t* out, Str* error) {
    uint64_t wide = 0;
    if (!PositiveUsize(value, label, &wide, error)) return false;
    if (wide > 65535) {
        *error = fmt("%s expects an integer no greater than 65535", label);
        return false;
    }
    *out = (uint16_t)wide;
    return true;
}

bool NonnegativeF32(double value, Str label, float* out, Str* error) {
    if (!isfinite(value) || value < 0.0 || value > (double)FLT_MAX) {
        *error =
            fmt("%s expects a nonnegative finite number representable as f32",
                label);
        return false;
    }
    *out = (float)value;
    return true;
}

} // namespace gpui::component_shell::structured
