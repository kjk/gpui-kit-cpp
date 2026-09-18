#include "base/measure.h"

#include <stdio.h>
#include <stdlib.h>

namespace gpui {

bool MeasurementEnabled() {
    return getenv("ZED_MEASUREMENTS") != nullptr ||
           getenv("GPUI_MEASUREMENTS") != nullptr;
}

Measure MeasureBegin(Str name) {
    Measure m;
    m.name = name;
    m.started = TimeNow();
    m.active = MeasurementEnabled();
    return m;
}

void MeasureEnd(Measure* measure) {
    if (!measure || !measure->active) {
        return;
    }
    double elapsedMs = (TimeNow() - measure->started) * 1000.0;
    printf("%.*s in %.3f ms\n", len(measure->name),
           measure->name.s ? measure->name.s : "", elapsedMs);
    measure->active = false;
}

void MeasureRunIf(Str name, bool enabled, MeasureFn fn) {
    if (!fn.IsValid()) {
        return;
    }
    if (!enabled || !MeasurementEnabled()) {
        fn.Call();
        return;
    }
    Measure m = MeasureBegin(name);
    fn.Call();
    MeasureEnd(&m);
}

void MeasureRun(Str name, MeasureFn fn) {
    MeasureRunIf(name, true, fn);
}

} // namespace gpui
