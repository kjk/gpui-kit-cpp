#include "base/input_keys.h"

namespace gpui {

void InputBindKeysOther(const char* ctx);

void InputBindPlatformKeys(const char* ctx) {
    InputBindKeysOther(ctx);
}

} // namespace gpui
