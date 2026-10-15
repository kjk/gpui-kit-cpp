#ifndef GPUI_SHELL_FAILURE_H_
#define GPUI_SHELL_FAILURE_H_
/* A script failure, as one line plus the stack behind Details.
   crates/shell/src/runtime.rs error_banner / error_overlay. */

#include "gpui/gpui.h"
#include "shell/scope.h"

namespace gpui {

// What a view's failure surface is reporting. A render that threw and a
// reload that failed share one surface; they differ in what is kept.
enum class ViewFailure : uint8_t {
    Render,
    Reload,
};

// The disclosure and the copy payload for one surface. Frame-scoped keyed
// state: it lives while the surface is built and goes with it.
struct FailureChrome {
    bool copied = false;
    bool expanded = false;
    Str payload;

    ~FailureChrome() { StrFree(payload); }
};

// A failure message split into what fits in one line and what does not.
// The strings are borrowed from `arena`.
struct FailureText {
    Str summary;
    Str detail;
    bool expandable = false;
};

// "100 ms" or "2 s", the way the interrupt report says a budget.
void SpokenDuration(double seconds, char* out, int cap);

// Rewrites QuickJS's bare "interrupted" once. Any other message, and a
// second "interrupted" after this one was consumed, leaves `slot` and
// returns empty.
struct ScriptInterruption {
    bool set = false;
    bool scoped = false;
    ScopePhase phase = ScopePhase::Render;
    double budget = 0;
};
Str ExplainInterruption(Arena* arena, Str message, ScriptInterruption* slot);

// Drops `?v=<generation>` loader tags, drops the runtime's own
// `(eval_script:)` frames, and folds a run of identical frames.
Str TidyStack(Arena* arena, Str stack);

// `js` is a JSContext*. This header does not include QuickJS. Empty when
// `message` is not "out of memory" and a thrown null is not sitting on a
// heap that has reached 90% of a ceiling the host set.
Str ExplainOutOfMemory(Arena* arena, void* js, Str message, bool thrownNull);

FailureText FailureTextParse(Arena* arena, Str message);

const char* ViewFailureLabel(ViewFailure failure);
const char* ViewFailureHeading(ViewFailure failure);
const char* ViewFailureKeptNote(ViewFailure failure);
const char* ViewFailureOverlayHeading(ViewFailure failure);
const char* ViewFailureRecovery(ViewFailure failure);

El* ShellErrorBanner(Ctx* cx, ViewFailure failure, Str message);
El* ShellErrorOverlay(Ctx* cx, ViewFailure failure, Str message);

} // namespace gpui
#endif // GPUI_SHELL_FAILURE_H_
