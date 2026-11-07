#ifndef GPUI_SHELL_WIDE_H_
#define GPUI_SHELL_WIDE_H_

// UTF-8 <-> UTF-16 copies the shell's Windows files hand to wide OS calls.
// Defined in wide_win.cpp; nothing else compiles a caller.

#include "base.h"

namespace gpui::shell {

// A heap, NUL-terminated UTF-16 copy of value, freed with Free(nullptr, p).
// Null when value is null, holds a NUL (the OS would read a shorter path than
// the policy checked) or is not valid UTF-8.
wchar_t* WideDup(Str value);

// A heap UTF-8 copy of count UTF-16 units, freed with StrFree. Empty when the
// conversion or the allocation fails.
Str Utf8Dup(const wchar_t* value, int count);

} // namespace gpui::shell

#endif // GPUI_SHELL_WIDE_H_
