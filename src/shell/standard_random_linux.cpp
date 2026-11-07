#include "shell/standard.h"

#include <errno.h>
#include <sys/random.h>

namespace gpui::shell {

bool SecureRandom(uint8_t* bytes, int count) {
    if (count < 0) return false;
    int offset = 0;
    while (offset < count) {
        ssize_t got = getrandom(bytes + offset, (size_t)(count - offset), 0);
        if (got > 0)
            offset += (int)got;
        else if (got < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    if (offset == count) return true;
    return SecureRandomFromUrandom(bytes, offset, count);
}

} // namespace gpui::shell
