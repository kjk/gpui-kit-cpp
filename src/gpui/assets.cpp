#include "gpui/assets.h"

#include "base.h"

#include <stdio.h>

namespace gpui {

#if GPUI_OS_WINDOWS
static const char kSep = '\\';
#else
static const char kSep = '/';
#endif

static const int kMaxRoots = 12;
static char gRoots[kMaxRoots][kMaxPath];
static int gRootN = 0;

struct RegisteredAssetSource {
    int id = 0;
    void* user = nullptr;
    AssetLoadFn load = nullptr;
    AssetExistsFn exists = nullptr;
};

static RegisteredAssetSource gSources[kMaxRoots];
static int gSourceN = 0;
static int gNextSource = 1;

void AssetsClear() {
    gRootN = 0;
    gSourceN = 0;
}

static void AddRootRaw(const char* dir) {
    if (!dir || !dir[0] || gRootN >= kMaxRoots) {
        return;
    }
    for (int i = 0; i < gRootN; i++) {
        if (base::StrEqI(Str(gRoots[i]), dir)) {
            return;
        }
    }
    if (!PlatDirExists(dir)) {
        return;
    }
    StrCopyZ(gRoots[gRootN], kMaxPath, dir);
    gRootN++;
}

// How many roots are registered, which is what AppNew asks before it
// supplies a default.
int AssetsRootCount() {
    return gRootN + gSourceN;
}

int AssetsAddSource(void* user, AssetLoadFn load, AssetExistsFn exists) {
    if (!user || !load || gSourceN >= kMaxRoots) return 0;
    int id = gNextSource++;
    if (id <= 0) {
        gNextSource = 2;
        id = 1;
    }
    gSources[gSourceN++] = {id, user, load, exists};
    return id;
}

void AssetsRemoveSource(int id) {
    for (int i = 0; i < gSourceN; i++) {
        if (gSources[i].id != id) continue;
        for (int j = i + 1; j < gSourceN; j++) gSources[j - 1] = gSources[j];
        gSourceN--;
        return;
    }
}

void AssetsAddRoot(Str dir) {
    if (!dir.s || len(dir) <= 0) {
        return;
    }
    int n = len(dir) < kMaxPath - 1 ? len(dir) : kMaxPath - 1;
    TempStr path = StrDupTemp(Str(dir.s, n));
    AddRootRaw(path.s);
}

static TempStr JoinPathTemp(Str a, Str b) {
    if (!a) return StrDupTemp(b);
    if (!b) return StrDupTemp(a);
    return fmt("%s%c%s", a, kSep, b);
}

static void ParentDir(Str* path) {
    while (path->len > 0 &&
           (path->s[path->len - 1] == '\\' || path->s[path->len - 1] == '/')) {
        path->s[--path->len] = 0;
    }
    while (path->len > 0 && path->s[path->len - 1] != '\\' &&
           path->s[path->len - 1] != '/') {
        path->s[--path->len] = 0;
    }
    while (path->len > 0 &&
           (path->s[path->len - 1] == '\\' || path->s[path->len - 1] == '/')) {
        path->s[--path->len] = 0;
    }
}

// Asset paths are written with forward slashes; rewrite them to whatever the
// OS wants.
static void ToNativeSep(Str s) {
    for (int i = 0; i < len(s); i++) {
        if (s.s[i] == '/' || s.s[i] == '\\') {
            s.s[i] = kSep;
        }
    }
}

void AssetsAddDefaultRoots(Str exampleName) {
    TempStr cwd = AllocStrTemp(kMaxPath - 1);
    cwd.s[0] = 0;
    PlatGetCwd(cwd.s, len(cwd) + 1);
    cwd.len = (int)strlen(cwd.s);

    TempStr exe = AllocStrTemp(kMaxPath - 1);
    exe.s[0] = 0;
    PlatGetExeDir(exe.s, len(exe) + 1);
    exe.len = (int)strlen(exe.s);

    TempStr sub = exampleName ? fmt("assets%c%s", kSep, exampleName)
                              : StrDupTemp(StrL("assets"));

    TempStr path = JoinPathTemp(cwd, sub);
    AddRootRaw(path.s);
    path = JoinPathTemp(exe, sub);
    AddRootRaw(path.s);

    // Walk parents of cwd and exe looking for assets/<name>
    for (int src = 0; src < 2; src++) {
        TempStr walk = StrDupTemp(src == 0 ? cwd : exe);
        for (int up = 0; up < 6; up++) {
            path = JoinPathTemp(walk, sub);
            AddRootRaw(path.s);
            if (exampleName.s) {
                // rust layout: examples/app_assets/assets
                TempStr rust =
                    fmt("examples%c%s%cassets", kSep, exampleName, kSep);
                path = JoinPathTemp(walk, rust);
                AddRootRaw(path.s);
                rust = fmt(".work%cgpui-component%cexamples%c%s%cassets", kSep,
                           kSep, kSep, exampleName, kSep);
                path = JoinPathTemp(walk, rust);
                AddRootRaw(path.s);
            }
            // The pinned Rust clone itself, which is where a folder that
            // belongs to no one example lives — `themes/`, the theme files
            // the registry reads.
            TempStr work = fmt(".work%cgpui-component", kSep);
            path = JoinPathTemp(walk, work);
            AddRootRaw(path.s);
            TempStr prev = StrDupTemp(walk);
            ParentDir(&walk);
            if (!walk || base::StrEqI(prev, walk)) {
                break;
            }
        }
    }
}

static bool ReadFileAll(const char* path, Vec<uint8_t>* out) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size < 0 || size > 8 * 1024 * 1024) {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    VecReset(*out);
    int n = (int)size;
    if (n == 0) {
        fclose(f);
        return true;
    }
    uint8_t* buf = VecAppendBlanks(*out, n);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if ((int)got != n) {
        VecReset(*out);
        return false;
    }
    return true;
}

bool AssetsFindDir(Str relDir, char* out, int cap) {
    if (!relDir.s || len(relDir) <= 0 || !out || cap <= 0) {
        return false;
    }
    int n = len(relDir) < kMaxPath - 1 ? len(relDir) : kMaxPath - 1;
    TempStr rel = StrDupTemp(Str(relDir.s, n));
    ToNativeSep(rel);

    for (int i = 0; i < gRootN; i++) {
        TempStr full = JoinPathTemp(Str(gRoots[i]), rel);
        if (PlatDirExists(full.s)) {
            StrCopyZ(out, cap, full.s);
            return true;
        }
    }
    return false;
}

bool AssetsLoad(Str relPath, Vec<uint8_t>* out) {
    if (!relPath.s || len(relPath) <= 0 || !out) {
        return false;
    }
    for (int i = gSourceN - 1; i >= 0; i--)
        if (gSources[i].load(gSources[i].user, relPath, out)) return true;

    int n = len(relPath) < kMaxPath - 1 ? len(relPath) : kMaxPath - 1;
    TempStr rel = StrDupTemp(Str(relPath.s, n));
    ToNativeSep(rel);

    for (int i = 0; i < gRootN; i++) {
        TempStr full = JoinPathTemp(Str(gRoots[i]), rel);
        if (ReadFileAll(full.s, out)) {
            return true;
        }
    }
    return false;
}

TempStr AssetsLoadTextTemp(Str relPath) {
    Vec<uint8_t> buf;
    if (!AssetsLoad(relPath, &buf) || len(buf) <= 0) {
        return {};
    }
    Str s = AllocStrTemp(len(buf));
    if (!s.s) {
        return {};
    }
    memcpy(s.s, buf.els, (size_t)len(buf));
    s.s[len(buf)] = 0;
    return s;
}

// Whether a root has the file, asked without reading it. This used to be
// AssetsLoad into a throwaway buffer, which meant an fopen and a full read
// per candidate path; the image layout asks it several times per picture per
// measure pass, so it was the single biggest thing in the story's layout.
bool AssetsExists(Str relPath) {
    if (!relPath.s || len(relPath) <= 0) {
        return false;
    }
    for (int i = gSourceN - 1; i >= 0; i--)
        if (gSources[i].exists && gSources[i].exists(gSources[i].user, relPath))
            return true;

    int n = len(relPath) < kMaxPath - 1 ? len(relPath) : kMaxPath - 1;
    TempStr rel = StrDupTemp(Str(relPath.s, n));
    ToNativeSep(rel);

    for (int i = 0; i < gRootN; i++) {
        TempStr full = JoinPathTemp(Str(gRoots[i]), rel);
        if (PlatFileExists(full.s)) {
            return true;
        }
    }
    return false;
}
} // namespace gpui
