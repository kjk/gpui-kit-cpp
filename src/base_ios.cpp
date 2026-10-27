/* iOS answers for the Plat* facts that are not shared with every POSIX
   target. Strings, directories, threads and the clock stay in base_posix.cpp.
   Stat timestamps match macOS (st_mtimespec); the shortcut modifier, fonts
   and window chrome do not — iOS is not GPUI_OS_MAC. */

#include "base.h"

#include <sys/stat.h>

namespace base {

bool PlatGetExePath(char* out, int cap) {
    if (out && cap > 0) out[0] = 0;
    return false;
}

uint64_t PlatStatModifiedNs(const struct stat* st) {
    return (uint64_t)st->st_mtimespec.tv_sec * 1000000000ull +
           (uint64_t)st->st_mtimespec.tv_nsec;
}

bool PlatSecondaryIsCommand() {
    return false;
}
bool PlatTitleBarCloseIsElement() {
    return false;
}
bool PlatShowsWindowControls() {
    return true;
}
float PlatCaretWidth() {
    return 1.f;
}
bool PlatScrollBounce() {
    return true;
}
bool PlatCleanOnEscape() {
    return true;
}
bool PlatScrollBounceTouch() {
    return true;
}
bool PlatWebViewPartialClipHides() {
    return false;
}
const char* PlatMonoFontName() {
    return "DejaVu Sans Mono";
}
const char* PlatShellDataDir() {
    return "/.local/share";
}
const char* PlatShellPlatformName() {
    return "linux";
}
bool PlatBlockSelectUsesControl() {
    return false;
}
bool PlatScrollGestureLocks() {
    return true;
}
bool PlatAsyncIo() {
    return false;
}
float PlatWindowShadowSize() {
    return 0.f;
}

} // namespace base
