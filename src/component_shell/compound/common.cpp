// crates/component-shell/src/shell/compound/common.rs

#include "component_shell/compound/common.h"

#include <float.h>
#include <limits.h>
#include <math.h>

namespace gpui::component_shell::compound::common {

bool NonemptyId(Str id, const char* component, Str* error) {
    if (len(StrTrim(id)) > 0) return true;
    *error = fmt("%s(id) expects a nonempty string id", Str(component));
    return false;
}

bool NonnegativeUsize(double value, Str label, uint64_t* out, Str* error) {
    if (!IsUsizeNumber(value)) {
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

int64_t UsizeInt64(uint64_t value) {
    return value > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)value;
}

} // namespace gpui::component_shell::compound::common
