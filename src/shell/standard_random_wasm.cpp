#include "shell/standard.h"

#if GPUI_OS_WASM

#include <emscripten.h>

EM_JS(int, ShellCryptoRandom, (uint8_t* bytes, int count), {
    if (count < 0 || !globalThis.crypto || !globalThis.crypto.getRandomValues)
        return 0;
    const chunk = 65536;
    for (let at = 0; at < count; at += chunk) {
        globalThis.crypto.getRandomValues(
            HEAPU8.subarray(bytes + at, bytes + Math.min(count, at + chunk)));
    }
    return 1;
});

namespace gpui::shell {
bool SecureRandom(uint8_t* bytes, int count) {
    return ShellCryptoRandom(bytes, count) != 0;
}
} // namespace gpui::shell
#endif
