#ifndef GPUI_SHELL_STANDARD_H_
#define GPUI_SHELL_STANDARD_H_

#include "base.h"

namespace gpui::shell {

constexpr int kStandardDataLimit = 64 * 1024 * 1024;

void Sha256(Str data, uint8_t digest[32]);
bool SecureRandom(uint8_t* bytes, int count);

bool ZlibDeflate(Str input, bool gzip, Str* output, Str* error = nullptr);
bool ZlibInflate(Str input, bool gzip, Str* output, Str* error = nullptr);
// A raw DEFLATE stream: no zlib or gzip wrapper. What a zip method-8 entry
// holds. `output` is owned.
bool ZlibInflateRaw(Str input, Str* output, Str* error = nullptr);

} // namespace gpui::shell
#endif // GPUI_SHELL_STANDARD_H_
