/* Android answers for the Plat* facts that are not shared with every POSIX
   target. Strings, directories, threads and the clock stay in base_posix.cpp.
   Stat timestamps match Linux (st_mtim). */

#include "base.h"

#include <sys/stat.h>

namespace base {

uint64_t PlatStatModifiedNs(const struct stat* st) {
    return (uint64_t)st->st_mtim.tv_sec * 1000000000ull + (uint64_t)st->st_mtim
                                                              .tv_nsec;
}

bool PlatSecondaryIsCommand() {
    return false;
}
bool PlatShowsWindowControls() {
    return true;
}
float PlatCaretWidth() {
    return 2.f;
}
bool PlatScrollBounce() {
    return true;
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
