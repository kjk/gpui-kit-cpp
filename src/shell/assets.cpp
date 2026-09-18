#include "shell/assets.h"

#include <stdlib.h>

namespace gpui {

static bool AppAssetLoad(void* user, Str path, Vec<uint8_t>* out) {
    return user && ((AppAssets*)user)->Load(path, out);
}

static bool AppAssetExists(void* user, Str path) {
    return user && ((AppAssets*)user)->Exists(path);
}

AppAssets::AppAssets(Str value) : root(StrDup(value)) {}

AppAssets::~AppAssets() {
    Uninstall();
    for (int i = 0; i < missing.len; i++) StrFree(missing[i]);
    VecReset(missing);
    StrFree(root);
}

bool AppAssets::Install() {
    if (!source) source = AssetsAddSource(this, AppAssetLoad, AppAssetExists);
    return source != 0;
}

void AppAssets::Uninstall() {
    if (source) AssetsRemoveSource(source);
    source = 0;
}

bool AppAssets::Resolve(Str path, Str* relative, Str* error) const {
    if (error) {
        StrFree(*error);
        *error = {};
    }
    if (!path || len(path) >= kMaxPath || StrStartsWithAny(path, "/\\") ||
        (len(path) >= 2 && path.s[1] == ':')) {
        if (error)
            *error = StrDup(
                fmt("asset `%s` is outside the application directory", path));
        return false;
    }
    int segment = 0;
    for (int i = 0; i <= len(path); i++) {
        bool separator =
            i == len(path) || path.s[i] == '/' || path.s[i] == '\\';
        if (!separator) continue;
        int n = i - segment;
        if (n == 2 && path.s[segment] == '.' && path.s[segment + 1] == '.') {
            if (error)
                *error = StrDup(fmt(
                    "asset `%s` is outside the application directory", path));
            return false;
        }
        segment = i + 1;
    }
    *relative = path;
    return true;
}

static int CompareAssetNames(const void* a, const void* b) {
    const Str* left = (const Str*)a;
    const Str* right = (const Str*)b;
    return StrCmp(*left, *right);
}

bool AppAssets::Load(Str path, Vec<uint8_t>* out, Str* error) {
    if (!out) return false;
    VecReset(*out);
    Str relative;
    if (!Resolve(path, &relative, error)) return false;
    shell::FsResult result;
    Str failure;
    if (!shell::FsRun(shell::FsOperation::Read, root, relative, {}, false,
                      &result, &failure)) {
        bool seen = false;
        for (int i = 0; i < missing.len; i++)
            if (StrEq(missing[i], path)) seen = true;
        if (!seen) {
            if (missing.len == kShellMaxReportedMissingAssets) {
                StrFree(missing[0]);
                for (int i = 1; i < missing.len; i++)
                    missing[i - 1] = missing[i];
                missing.len--;
            }
            VecAppend(missing, StrDup(path));
            log(
                fmt("asset `%s` was not found under `%s`; asset paths resolve "
                    "against the application directory",
                    path, root));
        }
        if (error)
            *error = failure;
        else
            StrFree(failure);
        result.Free();
        return false;
    }
    if (result.bytes.len > kShellMaxAssetBytes) {
        if (error)
            *error =
                StrDup(fmt("asset `%s` is %d bytes, over the %d-byte limit",
                           path, result.bytes.len, kShellMaxAssetBytes));
        result.Free();
        return false;
    }
    if (result.bytes.len > 0) {
        uint8_t* bytes = VecAppendBlanks(*out, result.bytes.len);
        if (!bytes) {
            if (error) *error = StrDup(StrL("asset allocation failed"));
            result.Free();
            return false;
        }
        memcpy(bytes, result.bytes.s, (size_t)result.bytes.len);
    }
    result.Free();
    return true;
}

bool AppAssets::Exists(Str path) {
    Str relative;
    if (!Resolve(path, &relative)) return false;
    shell::FsResult result;
    bool ok = shell::FsRun(shell::FsOperation::Exists, root, relative, {},
                           false, &result, nullptr) &&
              result.exists;
    result.Free();
    return ok;
}

bool AppAssets::List(Str path, Vec<Str>* out, Str* error) {
    if (!out) return false;
    for (int i = 0; i < out->len; i++) StrFree((*out)[i]);
    VecReset(*out);
    Str relative;
    if (!Resolve(path, &relative, error)) return false;
    shell::FsResult result;
    if (!shell::FsRun(shell::FsOperation::ReadDirectory, root, relative, {},
                      false, &result, error)) {
        result.Free();
        return false;
    }
    for (int i = 0; i < result.entries.len; i++)
        VecAppend(*out, StrDup(result.entries[i].name));
    if (out->len > 1)
        qsort(out->els, (size_t)out->len, sizeof(Str), CompareAssetNames);
    result.Free();
    return true;
}

} // namespace gpui
