#include "shell/standard.h"

#if !GPUI_OS_WINDOWS && !GPUI_OS_WASM

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#if GPUI_OS_LINUX
#include <sys/random.h>
#endif

namespace gpui::shell {

bool SecureRandom(uint8_t* bytes, int count) {
    if (count < 0) return false;
#if GPUI_OS_MAC || GPUI_OS_IOS
    if (count > 0) arc4random_buf(bytes, (size_t)count);
    return true;
#else
    int offset = 0;
#if GPUI_OS_LINUX
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
#endif
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
#endif
}

} // namespace gpui::shell
#endif
