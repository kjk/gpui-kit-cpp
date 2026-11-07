// The _inotify suffix is the one Linux and Android share: one kernel, one
// /dev/urandom. Linux falls back to it when getrandom comes up short; Android
// at API 24 has no getrandom and reads it from the start.
#include "shell/standard.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

namespace gpui::shell {

bool SecureRandomFromUrandom(uint8_t* bytes, int offset, int count) {
    int file = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (file < 0) return false;
    while (offset < count) {
        ssize_t got = read(file, bytes + offset, (size_t)(count - offset));
        if (got > 0)
            offset += (int)got;
        else if (got < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    close(file);
    return offset == count;
}

} // namespace gpui::shell
