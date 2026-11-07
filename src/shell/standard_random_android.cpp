#include "shell/standard.h"

namespace gpui::shell {

bool SecureRandom(uint8_t* bytes, int count) {
    if (count < 0) return false;
    return SecureRandomFromUrandom(bytes, 0, count);
}

} // namespace gpui::shell
