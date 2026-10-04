#include "shell/standard.h"

#include <windows.h>

extern "C" BOOLEAN NTAPI SystemFunction036(PVOID, ULONG);

namespace gpui::shell {

bool SecureRandom(uint8_t* bytes, int count) {
    return count >= 0 && (count == 0 || SystemFunction036(bytes, (ULONG)count));
}

} // namespace gpui::shell
