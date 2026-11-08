#ifndef GPUI_SHELL_COMMON_H_
#define GPUI_SHELL_COMMON_H_

// Small path, text and file helpers several shell files had each written
// for themselves.

#include "base.h"

namespace gpui::shell {

// `/x`, `\x`, or a drive-letter `C:\x` / `C:/x`, on every host: a path
// read from a manifest or a variable means the same wherever it is read.
bool PathIsAbsolute(Str path);

// `left` and `right` joined by the host's separator, unless `left` already
// ends in one. Either side empty is the other. Heap-owned.
Str PathJoin(Str left, Str right);

// `value` without leading and trailing ASCII whitespace and control bytes.
// A view into value.
Str TrimAscii(Str value);

// A JSON string literal: quotes and backslashes escaped, control bytes as
// \u00XX.
void AppendJsonString(StrBuilder* out, Str value);

// Writes `contents` to `path`, creating or truncating it. False with "cannot
// write <path>", or the path being empty or too long, as the reason.
bool WriteWholeFile(Str path, Str contents, Str* error);
// Reads all of `path`, refusing a file over `limit` bytes. `out` is owned and
// NUL-terminated. False with "cannot read <path>" or the like.
bool ReadWholeFile(Str path, int limit, Str* out, Str* error);

// The directories a source-tree walk does not enter: hidden ones,
// node_modules and target.
bool SourceTreeSkipsDirectory(Str name);

} // namespace gpui::shell

#endif // GPUI_SHELL_COMMON_H_
