#include "shell/standard.h"

#include <stdlib.h>

namespace gpui::shell {

bool SecureRandom(uint8_t* bytes, int count) {
    if (count < 0) return false;
    if (count > 0) arc4random_buf(bytes, (size_t)count);
    return true;
}

} // namespace gpui::shell
