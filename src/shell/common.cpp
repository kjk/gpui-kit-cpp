#include "shell/common.h"

#include "shell/error.h"

#include <stdio.h>
#include <stdlib.h>

namespace gpui::shell {

static bool IsSlash(char c) {
    return c == '/' || c == '\\';
}

bool PathIsAbsolute(Str path) {
    if (!path || len(path) == 0) return false;
    if (IsSlash(path.s[0])) return true;
    char drive = path.s[0];
    return len(path) >= 3 &&
           ((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')) &&
           path.s[1] == ':' && IsSlash(path.s[2]);
}

Str PathJoin(Str left, Str right) {
    if (!left || len(left) == 0) return StrDup(right);
    if (!right || len(right) == 0) return StrDup(left);
    if (IsSlash(left.s[len(left) - 1])) return StrDup(fmt("%s%s", left, right));
    return StrDup(fmt("%s%c%s", left, base::PlatPathSep(), right));
}

Str TrimAscii(Str value) {
    int start = 0;
    int end = len(value);
    while (start < end && (uint8_t)value.s[start] <= ' ') start++;
    while (end > start && (uint8_t)value.s[end - 1] <= ' ') end--;
    return Str(value.s + start, end - start);
}

void AppendJsonString(StrBuilder* out, Str value) {
    out->AppendChar('"');
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (c == '"' || c == '\\') {
            out->AppendChar('\\');
            out->AppendChar(c);
        } else if ((uint8_t)c < 0x20) {
            static const char* digits = "0123456789abcdef";
            char escaped[] = {
                '\\', 'u', '0', '0', digits[((uint8_t)c) >> 4], digits[c & 15]};
            out->Append(Str(escaped, 6));
        } else {
            out->AppendChar(c);
        }
    }
    out->AppendChar('"');
}

bool WriteWholeFile(Str path, Str contents, Str* error) {
    if (!path || len(path) <= 0 || len(path) >= kMaxPath) {
        ErrorOutSet(error, StrL("cannot write: the path is empty or too long"));
        return false;
    }
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "wb");
    if (!file) {
        ErrorOutSet(error, fmt("cannot write %s", path));
        return false;
    }
    bool ok = len(contents) == 0 || fwrite(contents.s, 1, (size_t)len(contents),
                                           file) == (size_t)len(contents);
    if (fclose(file) != 0) ok = false;
    if (!ok) ErrorOutSet(error, fmt("cannot write %s", path));
    return ok;
}

bool ReadWholeFile(Str path, int limit, Str* out, Str* error) {
    if (out) *out = {};
    if (!path || len(path) <= 0 || len(path) >= kMaxPath) {
        ErrorOutSet(error, StrL("cannot read: the path is empty or too long"));
        return false;
    }
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "rb");
    if (!file) {
        ErrorOutSet(error, fmt("cannot read %s", path));
        return false;
    }
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    if (size < 0 || size > limit || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        ErrorOutSet(error, fmt("cannot read %s", path));
        return false;
    }
    char* buffer = (char*)Alloc(nullptr, (size_t)size + 1);
    if (!buffer) {
        fclose(file);
        ErrorOutSet(error, StrL("allocating a file failed"));
        return false;
    }
    size_t got = size ? fread(buffer, 1, (size_t)size, file) : 0;
    fclose(file);
    if (got != (size_t)size) {
        free(buffer);
        ErrorOutSet(error, fmt("cannot read %s", path));
        return false;
    }
    buffer[size] = 0;
    if (out)
        *out = Str(buffer, (int)size);
    else
        free(buffer);
    return true;
}

bool SourceTreeSkipsDirectory(Str name) {
    return !name || name.s[0] == '.' || StrEq(name, StrL("node_modules")) ||
           StrEq(name, StrL("target"));
}

} // namespace gpui::shell
