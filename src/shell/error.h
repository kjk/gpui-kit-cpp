#ifndef GPUI_SHELL_ERROR_H_
#define GPUI_SHELL_ERROR_H_

#include "base.h"

namespace gpui {

struct ShellError {
    Str message;

    bool IsSet() const { return len(message) > 0; }
};

void ShellErrorClear(ShellError* error);
void ShellErrorSet(ShellError* error, Str message);

// Replaces an owned `Str* error` out-parameter with a copy of message. A null
// out-parameter is a caller that did not ask for the reason.
void ErrorOutSet(Str* error, Str message);
// Frees and empties an owned `Str* error` out-parameter, if there is one.
void ErrorOutClear(Str* error);

} // namespace gpui
#endif // GPUI_SHELL_ERROR_H_
