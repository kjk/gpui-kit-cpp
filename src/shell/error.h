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

} // namespace gpui
#endif // GPUI_SHELL_ERROR_H_
