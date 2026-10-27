#ifndef GPUI_SHELL_VALUE_H_
#define GPUI_SHELL_VALUE_H_

#include "shell/error.h"
#include "gpui/gpui.h"

namespace gpui::shell {

enum class BridgedKind : uint8_t {
    Nil,
    Bool,
    Number,
    String,
    // A Background from the script's builder. The spec lives in the arena
    // that recorded the call.
    Background,
    // A shadow([...]) list. `shadows` is arena-owned; count 0 clears them.
    Shadows,
};

struct BackgroundSpec;
struct ShadowSpec;

struct Bridged {
    BridgedKind kind = BridgedKind::Nil;
    bool boolean = false;
    double number = 0;
    Str string;
    const BackgroundSpec* background = nullptr;
    const ShadowSpec* shadows = nullptr;
    int shadowCount = 0;

    static Bridged Nil();
    static Bridged Bool(bool value);
    static Bridged Number(double value);
    static Bridged String(Str value);
    static Bridged Background(const BackgroundSpec* spec);
    static Bridged Shadows(const ShadowSpec* specs, int count);
};

bool BridgedAsF32(const Bridged& value, float* out,
                  ShellError* error = nullptr);
bool BridgedAsString(const Bridged& value, Str* out,
                     ShellError* error = nullptr);
bool BridgedAsPixels(const Bridged& value, float* out,
                     ShellError* error = nullptr);
bool BridgedAsColor(const Bridged& value, Hsla* out,
                    ShellError* error = nullptr);
// A colour literal, or a Background from the builder. Shared by `bg` and
// `window.paint_path`.
bool BackgroundSpecResolve(const BackgroundSpec& spec, ::gpui::Background* out,
                           ShellError* error = nullptr);
bool BridgedAsBackground(const Bridged& value, ::gpui::Background* out,
                         ShellError* error = nullptr);
// CSS order, reversed so the first record paints on top. `blur` is halved.
bool BridgedAsShadows(const Bridged& value, ::gpui::BoxShadow* out, int cap,
                      int* count, ShellError* error = nullptr);
bool BridgedIsTruthy(const Bridged& value);
Str BridgedDescribe(Arena* arena, const Bridged& value);
bool BridgedArg(const Bridged* args, int count, int index, Str method,
                Bridged* out, ShellError* error = nullptr);

} // namespace gpui::shell
#endif // GPUI_SHELL_VALUE_H_
