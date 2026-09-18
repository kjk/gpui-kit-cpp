#include "shell/dependencies.h"

#include "base/json.h"
#include "shell/process.h"
#include "shell/standard.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gpui::shell {

static const char kSeparator = GPUI_OS_WINDOWS ? '\\' : '/';

static void DepError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

static bool IsSeparator(char c) {
    return c == '/' || c == '\\';
}

static Str JoinPath(Str left, Str right) {
    if (!left) return StrDup(right);
    if (!right) return StrDup(left);
    bool separated = IsSeparator(left.s[len(left) - 1]);
    return StrDup(separated ? fmt("%s%s", left, right)
                            : fmt("%s%c%s", left, kSeparator, right));
}

static bool PathEq(Str a, Str b) {
    if (len(a) != len(b)) return false;
#if GPUI_OS_WINDOWS
    for (int i = 0; i < len(a); i++) {
        char ca = a.s[i];
        char cb = b.s[i];
        if (IsSeparator(ca) && IsSeparator(cb)) continue;
        if (tolower((unsigned char)ca) != tolower((unsigned char)cb))
            return false;
    }
    return true;
#else
    return StrEq(a, b);
#endif
}

// A path prefix test that also refuses `<root>x`.
static bool WithinPath(Str root, Str path) {
    if (!root || !path || len(path) < len(root)) return false;
#if GPUI_OS_WINDOWS
    for (int i = 0; i < len(root); i++) {
        char a = root.s[i];
        char b = path.s[i];
        if (IsSeparator(a) && IsSeparator(b)) continue;
        if (tolower((unsigned char)a) != tolower((unsigned char)b))
            return false;
    }
#else
    if (!StrEq(root, Str(path.s, len(root)))) return false;
#endif
    return len(path) == len(root) || IsSeparator(path.s[len(root)]);
}

static Str Canonical(Str path) {
    if (!path || len(path) >= kMaxPath) return {};
    TempStr input = StrDupTemp(path);
    TempStr output = AllocStrTemp(kMaxPath - 1);
    if (!PlatCanonicalPath(input.s, output.s, len(output) + 1)) return {};
    return StrDup(Str(output.s));
}

static Str TrimAscii(Str value) {
    int start = 0;
    int end = len(value);
    while (start < end && (uint8_t)value.s[start] <= ' ') start++;
    while (end > start && (uint8_t)value.s[end - 1] <= ' ') end--;
    return Str(value.s + start, end - start);
}

static bool WriteWhole(Str path, Str contents) {
    if (!path || len(path) >= kMaxPath) return false;
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "wb");
    if (!file) return false;
    bool ok = len(contents) == 0 || fwrite(contents.s, 1, (size_t)len(contents),
                                           file) == (size_t)len(contents);
    if (fclose(file) != 0) ok = false;
    return ok;
}

// --- the cache root ----------------------------------------------------

Str GitDependencyCacheRoot(Str home) {
    Str shell = JoinPath(home, StrL(".gpui-shell"));
    Str cache = JoinPath(shell, StrL("cache"));
    Str result = JoinPath(cache, StrL("dependencies"));
    StrFree(shell);
    StrFree(cache);
    return result;
}

static bool AbsolutePath(Str path) {
    if (!path) return false;
    if (IsSeparator(path.s[0])) return true;
    return len(path) >= 3 && path.s[1] == ':' && IsSeparator(path.s[2]);
}

bool GitDependencyUserCacheRoot(Str home, Str userProfile, Str* out,
                                Str* error) {
    if (out) *out = {};
    Str selected = {};
    Str variable = {};
    if (home && len(home) > 0) {
        selected = home;
        variable = StrL("HOME");
    } else if (userProfile && len(userProfile) > 0) {
        selected = userProfile;
        variable = StrL("USERPROFILE");
    } else {
        DepError(error,
                 StrL("cannot locate the Git dependency cache: HOME or "
                      "USERPROFILE must name an absolute user directory"));
        return false;
    }
    if (!AbsolutePath(selected)) {
        DepError(error, fmt("cannot locate the Git dependency cache: %s must "
                            "be an absolute path, got `%s`",
                            variable, selected));
        return false;
    }
    if (out) *out = GitDependencyCacheRoot(selected);
    return true;
}

// `digest(&[("git", url)])`.
static void AppendLengthLe(Vec<uint8_t>* out, int value) {
    uint64_t length = (uint64_t)value;
    for (int i = 0; i < 8; i++) {
        VecAppend(*out, (uint8_t)((length >> (i * 8)) & 0xff));
    }
}

Str GitDependencyRemoteKey(Str git) {
    Vec<uint8_t> input;
    Str kind = StrL("git");
    AppendLengthLe(&input, len(kind));
    memcpy(VecAppendBlanks(input, len(kind)), kind.s, (size_t)len(kind));
    AppendLengthLe(&input, len(git));
    if (len(git) > 0)
        memcpy(VecAppendBlanks(input, len(git)), git.s, (size_t)len(git));
    uint8_t digest[32];
    Sha256(Str((const char*)input.els, len(input)), digest);
    VecReset(input);
    TempStr hex = AllocStrTemp(64);
    static const char* digits = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        hex.s[i * 2] = digits[digest[i] >> 4];
        hex.s[i * 2 + 1] = digits[digest[i] & 15];
    }
    return StrDup(hex);
}

static Mutex gTemporaryMutex;
static uint64_t gTemporaryNext = 1;

// `temporary_path`: unique per process and per call, so two publishers never
// build in the same directory.
static Str TemporaryPath(Str parent, Str label) {
    gTemporaryMutex.Lock();
    uint64_t next = gTemporaryNext++;
    gTemporaryMutex.Unlock();
    Str name =
        StrDup(fmt(".%s.tmp-%u-%llu", label, (unsigned)DependencyProcessId(),
                   (unsigned long long)next));
    Str result = JoinPath(parent, name);
    StrFree(name);
    return result;
}

// --- running git -------------------------------------------------------

// `git_command` plus `run_command`. Non-interactive, bounded by the process
// runner's own 30 second ceiling, and reaching nothing but the executable the
// host already has.
static bool RunGit(Str name, Str operation, const Str* args, int count,
                   Str workingDirectory, ProcessOutput* output, Str* error) {
    Str environment[2] = {StrL("GIT_TERMINAL_PROMPT=0"),
                          StrL("GCM_INTERACTIVE=Never")};
    ProcessOptions options;
    options.workingDirectory = workingDirectory;
    options.environment = environment;
    options.environmentCount = 2;
    options.inheritEnvironment = true;
    Str failure = {};
    ProcessOutput result;
    bool ok = ProcessRunBounded(StrL("git"), args, count, nullptr, &result,
                                &failure, &options);
    if (!ok) {
        DepError(error, fmt("could not %s Git dependency `%s`: %s", operation,
                            name, failure));
        StrFree(failure);
        result.Free();
        return false;
    }
    StrFree(failure);
    if (result.code != 0) {
        DepError(error, fmt("could not %s Git dependency `%s`: %s", operation,
                            name, TrimAscii(result.err)));
        result.Free();
        return false;
    }
    if (output)
        *output = result;
    else
        result.Free();
    return true;
}

// `configured_origin`: `--null` so exactly one URL has to come back.
static Str ConfiguredOrigin(Str name, Str mirror, Str* error) {
    Str args[4] = {StrL("config"), StrL("--null"), StrL("--get-all"),
                   StrL("remote.origin.url")};
    ProcessOutput output;
    if (!RunGit(name, StrL("inspect cached origin"), args, 4, mirror, &output,
                error))
        return {};
    Str text = output.out;
    if (len(text) == 0 || text.s[len(text) - 1] != 0) {
        DepError(error, fmt("Git dependency `%s` cache origin config is "
                            "malformed; remove %s and retry",
                            name, mirror));
        output.Free();
        return {};
    }
    Str origin(text.s, len(text) - 1);
    bool embedded = false;
    for (int i = 0; i < len(origin); i++)
        if (origin.s[i] == 0) embedded = true;
    if (len(origin) == 0 || embedded) {
        DepError(error, fmt("Git dependency `%s` cache origin config must "
                            "contain exactly one non-empty URL; remove %s and "
                            "retry",
                            name, mirror));
        output.Free();
        return {};
    }
    Str result = StrDup(origin);
    output.Free();
    return result;
}

// --- the package entry -------------------------------------------------

// `dependency_entry_name`: the manifest names it, or `package.json`'s `main`
// does, or it is `index.js`.
static Str DependencyEntryName(Str name, const GitDependency& dependency,
                               Str root, Str* error) {
    if (!dependency.packageEntry) return StrDup(dependency.entry);
    Str manifestPath = JoinPath(root, StrL("package.json"));
    if (!PlatFileExists(manifestPath.s) && !PlatDirExists(manifestPath.s)) {
        StrFree(manifestPath);
        return StrDup(Str(kGitDependencyDefaultEntry));
    }
    Str canonical = Canonical(manifestPath);
    if (!canonical || !WithinPath(root, canonical) ||
        !PlatFileExists(canonical.s)) {
        DepError(error, fmt("Git dependency `%s` package.json is not a file "
                            "inside its checkout",
                            name));
        StrFree(manifestPath);
        StrFree(canonical);
        return {};
    }
    StrFree(manifestPath);
    TempStr source = ReadBoundedFileTemp(canonical, kShellMaxManifestBytes);
    if (!source.s) {
        DepError(
            error,
            fmt("reading package.json for Git dependency `%s` failed", name));
        StrFree(canonical);
        return {};
    }
    StrFree(canonical);
    Arena* arena = ArenaNew();
    JsonValue* value = JsonParse(arena, source);
    if (!value) {
        DepError(error, fmt("Git dependency `%s` package.json must contain "
                            "valid JSON",
                            name));
        ArenaDelete(arena);
        return {};
    }
    if (value->kind != JsonKind::Object) {
        DepError(error, fmt("Git dependency `%s` package.json must contain a "
                            "JSON object",
                            name));
        ArenaDelete(arena);
        return {};
    }
    const JsonValue* main = JsonGet(value, "main");
    if (!main) {
        ArenaDelete(arena);
        return StrDup(Str(kGitDependencyDefaultEntry));
    }
    if (main->kind != JsonKind::String) {
        DepError(error, fmt("Git dependency `%s` package.json `main` must be a "
                            "string",
                            name));
        ArenaDelete(arena);
        return {};
    }
    Str entry = StrDup(main->str);
    ArenaDelete(arena);
    bool escapes = len(entry) == 0 || AbsolutePath(entry) ||
                   StrContains(entry, StrL("\\")) ||
                   StrContains(entry, StrL(":"));
    int start = 0;
    for (int i = 0; !escapes && i <= len(entry); i++) {
        if (i < len(entry) && entry.s[i] != '/') continue;
        if (i - start == 2 && entry.s[start] == '.' &&
            entry.s[start + 1] == '.')
            escapes = true;
        start = i + 1;
    }
    if (escapes) {
        DepError(error, fmt("Git dependency `%s` package.json `main` `%s` must "
                            "be a path inside its checkout",
                            name, entry));
        StrFree(entry);
        return {};
    }
    return entry;
}

// --- the store ---------------------------------------------------------

void MaterializedDependency::Free() {
    StrFree(name);
    StrFree(root);
    StrFree(entry);
    *this = {};
}

const MaterializedDependency* MaterializedDependencies::Find(Str name) const {
    for (int i = 0; i < items.len; i++) {
        if (StrEq(items[i].name, name)) return &items[i];
    }
    return nullptr;
}

bool MaterializedDependencies::CopyFrom(const MaterializedDependencies& other) {
    Free();
    for (int i = 0; i < other.items.len; i++) {
        MaterializedDependency copy;
        copy.name = StrDup(other.items[i].name);
        copy.root = StrDup(other.items[i].root);
        copy.entry = StrDup(other.items[i].entry);
        if (!copy.name.s || !copy.root.s || !copy.entry.s) {
            copy.Free();
            Free();
            return false;
        }
        if (!VecAppend(items, copy)) {
            copy.Free();
            Free();
            return false;
        }
    }
    return true;
}

void MaterializedDependencies::Free() {
    for (int i = 0; i < items.len; i++) items[i].Free();
    VecReset(items);
}

GitDependencyStore::GitDependencyStore() : GitDependencyStore(Str{}) {}

// An empty root is `for_user`: the private cache the environment names.
GitDependencyStore::GitDependencyStore(Str value) {
    if (value) {
        root = StrDup(value);
        return;
    }
    const char* home = getenv("HOME");
    const char* userProfile = getenv("USERPROFILE");
    GitDependencyUserCacheRoot(Str(home ? home : ""),
                               Str(userProfile ? userProfile : ""), &root,
                               &initError);
}

GitDependencyStore::~GitDependencyStore() {
    StrFree(root);
    StrFree(initError);
}

bool GitDependencyStore::Materialize(Str name, const GitDependency& dependency,
                                     MaterializedDependency* out, Str* error) {
    if (out) *out = {};
    if (!IsValid()) {
        DepError(error, initError ? initError
                                  : StrL("the Git dependency cache is "
                                         "unavailable"));
        return false;
    }
    if (!DependencyMakeDirectories(root, error)) return false;
    Str remoteKey = GitDependencyRemoteKey(dependency.git);
    Str locks = JoinPath(root, StrL("locks"));
    Str mirrors = JoinPath(root, StrL("mirrors"));
    Str checkoutRoot = JoinPath(root, StrL("checkouts"));
    Str checkouts = JoinPath(checkoutRoot, remoteKey);
    Str lockPath = {};
    Str mirror = {};
    Str configured = {};
    Str reference = {};
    Str commit = {};
    Str checkout = {};
    Str checkoutCanonical = {};
    Str entryName = {};
    Str entryPath = {};
    Str entryCanonical = {};
    DependencyLock lock;
    bool ok = DependencyMakeDirectories(locks, error) &&
              DependencyMakeDirectories(mirrors, error) &&
              DependencyMakeDirectories(checkouts, error);
    if (ok) {
        lockPath = JoinPath(locks, fmt("%s.lock", remoteKey));
        ok = DependencyLockAcquire(lockPath, name, &lock, error);
    }
    if (ok) {
        mirror = JoinPath(mirrors, fmt("%s.git", remoteKey));
        if (!PlatDirExists(mirror.s)) {
            Str temporary = TemporaryPath(mirrors, remoteKey);
            Str args[5] = {StrL("clone"), StrL("--mirror"), StrL("--"),
                           dependency.git, temporary};
            ok = RunGit(name, StrL("clone"), args, 5, {}, nullptr, error);
            if (!ok) {
                DependencyRemoveTree(temporary);
            } else if (!DependencyRenameDirectory(temporary, mirror)) {
                if (PlatDirExists(mirror.s)) {
                    // Another process published it first.
                    DependencyRemoveTree(temporary);
                } else {
                    DependencyRemoveTree(temporary);
                    DepError(error, StrL("publishing Git dependency mirror "
                                         "failed"));
                    ok = false;
                }
            }
            StrFree(temporary);
        }
    }
    if (ok) {
        configured = ConfiguredOrigin(name, mirror, error);
        ok = configured.s != nullptr;
    }
    if (ok && !StrEq(configured, dependency.git)) {
        DepError(error, fmt("Git dependency `%s` cache origin is `%s`, "
                            "expected `%s`; remove %s and retry",
                            name, configured, dependency.git, mirror));
        ok = false;
    }
    if (ok) {
        if (dependency.packageEntry) {
            reference = StrDup(dependency.reference ? dependency.reference
                                                    : StrL("HEAD"));
        } else if (dependency.branch) {
            reference = StrDup(fmt("refs/heads/%s", dependency.branch));
        } else {
            reference = StrDup(fmt("refs/tags/%s", dependency.tag));
        }
        Str args[6] = {StrL("fetch"), StrL("--force"), StrL("--depth"),
                       StrL("1"),     StrL("origin"),  reference};
        ok = RunGit(name, StrL("fetch"), args, 6, mirror, nullptr, error);
    }
    if (ok) {
        Str args[2] = {StrL("rev-parse"), StrL("FETCH_HEAD")};
        ProcessOutput output;
        ok = RunGit(name, StrL("resolve fetched commit"), args, 2, mirror,
                    &output, error);
        if (ok) {
            Str trimmed = TrimAscii(output.out);
            bool valid = len(trimmed) == 40 || len(trimmed) == 64;
            for (int i = 0; valid && i < len(trimmed); i++) {
                char c = trimmed.s[i];
                valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                        (c >= 'A' && c <= 'F');
            }
            if (!valid) {
                DepError(error, fmt("Git dependency `%s` resolved an invalid "
                                    "commit id `%s`",
                                    name, trimmed));
                ok = false;
            } else {
                commit = StrDup(trimmed);
            }
            output.Free();
        }
    }
    if (ok) {
        checkout = JoinPath(checkouts, commit);
        Str marker = JoinPath(checkout, StrL(".git"));
        bool present = PlatDirExists(marker.s);
        StrFree(marker);
        if (!present) {
            Str temporary = TemporaryPath(checkouts, commit);
            Str clone[5] = {StrL("clone"), StrL("--no-checkout"), StrL("--"),
                            mirror, temporary};
            ok = RunGit(name, StrL("create immutable checkout"), clone, 5, {},
                        nullptr, error);
            if (ok) {
                Str detach[4] = {StrL("checkout"), StrL("--force"),
                                 StrL("--detach"), commit};
                ok = RunGit(name, StrL("checkout fetched commit"), detach, 4,
                            temporary, nullptr, error);
            }
            if (!ok) {
                DependencyRemoveTree(temporary);
            } else if (!DependencyRenameDirectory(temporary, checkout)) {
                Str published = JoinPath(checkout, StrL(".git"));
                bool other = PlatDirExists(published.s);
                StrFree(published);
                DependencyRemoveTree(temporary);
                if (!other) {
                    DepError(error, StrL("publishing Git dependency checkout "
                                         "failed"));
                    ok = false;
                }
            }
            StrFree(temporary);
        }
    }
    if (ok) {
        checkoutCanonical = Canonical(checkout);
        if (!checkoutCanonical) {
            DepError(error,
                     fmt("resolving dependency checkout %s failed", checkout));
            ok = false;
        }
    }
    if (ok) {
        entryName =
            DependencyEntryName(name, dependency, checkoutCanonical, error);
        ok = entryName.s != nullptr;
    }
    if (ok) {
        entryPath = JoinPath(checkoutCanonical, entryName);
        entryCanonical = Canonical(entryPath);
        if (!entryCanonical) {
            DepError(error, fmt("Git dependency `%s` has no entry `%s`", name,
                                entryName));
            ok = false;
        } else if (!WithinPath(checkoutCanonical, entryCanonical) ||
                   !PlatFileExists(entryCanonical.s)) {
            DepError(error, fmt("Git dependency `%s` entry `%s` is not a file "
                                "inside its checkout",
                                name, entryName));
            ok = false;
        }
    }
    if (ok && out) {
        out->name = StrDup(name);
        out->root = checkoutCanonical;
        out->entry = entryCanonical;
        checkoutCanonical = {};
        entryCanonical = {};
    }
    DependencyLockRelease(&lock);
    StrFree(remoteKey);
    StrFree(locks);
    StrFree(mirrors);
    StrFree(checkoutRoot);
    StrFree(checkouts);
    StrFree(lockPath);
    StrFree(mirror);
    StrFree(configured);
    StrFree(reference);
    StrFree(commit);
    StrFree(checkout);
    StrFree(checkoutCanonical);
    StrFree(entryName);
    StrFree(entryPath);
    StrFree(entryCanonical);
    return ok;
}

bool GitDependencyStore::MaterializeAll(const PluginManifest& manifest,
                                        MaterializedDependencies* out,
                                        Str* error) {
    if (!out) return false;
    out->Free();
    for (int i = 0; i < manifest.dependencies.len; i++) {
        const GitDependency& dependency = manifest.dependencies[i];
        MaterializedDependency materialized;
        if (!Materialize(dependency.name, dependency, &materialized, error)) {
            out->Free();
            return false;
        }
        VecAppend(out->items, materialized);
    }
    return true;
}

// --- editor links ------------------------------------------------------

static bool IsEditorLinkStub(Str link) {
    Str marker = JoinPath(link, Str(kEditorLinkMarker));
    bool result = PlatFileExists(marker.s);
    StrFree(marker);
    return result;
}

static void AppendJsonString(StrBuilder* out, Str value) {
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

// `write_editor_link_stub`: the package that stands in for a symlink the
// platform refused. A bare import types the same way; only a package-subpath
// import is left unresolved.
static bool WriteEditorLinkStub(Str link, Str name,
                                const MaterializedDependency& dependency,
                                Str* error) {
    if (!DependencyMakeDirectories(link, error)) return false;
    Str markerPath = JoinPath(link, Str(kEditorLinkMarker));
    Str manifestPath = JoinPath(link, StrL("package.json"));
    Str indexPath = JoinPath(link, StrL("index.js"));
    // A path as TypeScript wants to read it: rooted, with forward slashes.
    Str specifier = StrDup(dependency.entry);
    for (int i = 0; i < len(specifier); i++)
        if (specifier.s[i] == '\\') specifier.s[i] = '/';
    StrBuilder manifest;
    manifest.Append(StrL("{\n  \"main\": \"index.js\",\n  \"name\": "));
    AppendJsonString(&manifest, name);
    manifest
        .Append(StrL(",\n  \"private\": true,\n  \"type\": \"module\"\n}\n"));
    StrBuilder index;
    index.Append(
        fmt("// Written by gpui-shell so an editor can resolve `%s`.\n", name));
    index
        .Append(StrL("// The runtime resolves the manifest entry directly and "
                     "never reads this file.\nexport * from "));
    AppendJsonString(&index, specifier);
    index.Append(StrL(";\n"));
    Str manifestText = manifest.TakeStr();
    Str indexText = index.TakeStr();
    bool ok = WriteWhole(markerPath, fmt("%s\n", dependency.root)) &&
              WriteWhole(manifestPath, manifestText) &&
              WriteWhole(indexPath, indexText);
    if (!ok) DepError(error, fmt("writing %s failed", link));
    StrFree(manifestText);
    StrFree(indexText);
    StrFree(specifier);
    StrFree(markerPath);
    StrFree(manifestPath);
    StrFree(indexPath);
    return ok;
}

// `relink`: makes `link` point at `dependency`, and reports whether it had to.
bool GitDependencyStore::LinkForEditor(
    Str applicationRoot, const MaterializedDependencies& dependencies,
    int* linked, Str* error) {
    if (linked) *linked = 0;
    Str modules = JoinPath(applicationRoot, Str(kEditorModuleDirectory));
    if (dependencies.items.len == 0 && !PlatDirExists(modules.s)) {
        StrFree(modules);
        return true;
    }
    bool ok = DependencyMakeDirectories(modules, error);
    Vec<Str> declared;
    for (int i = 0; ok && i < dependencies.items.len; i++) {
        const MaterializedDependency& dependency = dependencies.items[i];
        Str link = JoinPath(modules, dependency.name);
        VecAppend(declared, link);
        // A scoped name is a directory and a package.
        int lastSeparator = -1;
        for (int c = 0; c < len(link); c++)
            if (IsSeparator(link.s[c])) lastSeparator = c;
        if (lastSeparator > 0) {
            Str parent(link.s, lastSeparator);
            ok = DependencyMakeDirectories(parent, error);
        }
        if (!ok) break;
        bool replaced = false;
        Str target = {};
        if (DependencyReadDirectoryLink(link, &target)) {
            if (PathEq(target, dependency.root)) {
                StrFree(target);
                continue;
            }
            if (!WithinPath(root, target)) {
                // It already points outside the dependency cache.
                StrFree(target);
                continue;
            }
            StrFree(target);
            DependencyRemoveDirectoryLink(link);
        } else if (IsEditorLinkStub(link)) {
            DependencyRemoveTree(link);
        } else if (PlatDirExists(link.s) || PlatFileExists(link.s)) {
            // An installed package already claims that name.
            continue;
        }
        if (DependencySymlinkDirectory(dependency.root, link)) {
            replaced = true;
        } else {
            ok = WriteEditorLinkStub(link, dependency.name, dependency, error);
            replaced = ok;
        }
        if (replaced && linked) (*linked)++;
    }
    if (ok) Prune(modules, declared);
    for (int i = 0; i < len(declared); i++) StrFree(declared[i]);
    VecReset(declared);
    StrFree(modules);
    return ok;
}

// `prune`: removes the links of dependencies the manifest no longer declares,
// bounded at the depth a scoped name needs and confined to entries this store
// wrote.
void GitDependencyStore::Prune(Str modules, const Vec<Str>& declared) {
    struct Pending {
        Str path;
        int depth;
    };
    Vec<Pending> pending;
    VecAppend(pending, Pending{StrDup(modules), 0});
    DirEntry* entries = AllocArray<DirEntry>(kEditorPruneMaxEntries);
    while (len(pending) > 0) {
        Pending directory = pending[len(pending) - 1];
        pending.len--;
        int count = entries ? PlatListDir(directory.path.s, entries,
                                          kEditorPruneMaxEntries)
                            : 0;
        for (int i = 0; i < count && i < kEditorPruneMaxEntries; i++) {
            const DirEntry& item = entries[i];
            Str name = Str(item.name);
            if (StrEq(name, StrL(".")) || StrEq(name, StrL(".."))) continue;
            Str path = JoinPath(directory.path, name);
            bool isDeclared = false;
            for (int d = 0; d < len(declared); d++)
                if (PathEq(declared[d], path)) isDeclared = true;
            if (isDeclared) {
                StrFree(path);
                continue;
            }
            if (item.isSymlink) {
                Str target = {};
                if (DependencyReadDirectoryLink(path, &target) &&
                    WithinPath(root, target)) {
                    DependencyRemoveDirectoryLink(path);
                }
                StrFree(target);
                StrFree(path);
                continue;
            }
            if (item.isDir) {
                if (IsEditorLinkStub(path)) {
                    DependencyRemoveTree(path);
                } else if (directory.depth < 1) {
                    VecAppend(pending, Pending{path, directory.depth + 1});
                    continue;
                }
            }
            StrFree(path);
        }
        StrFree(directory.path);
    }
    Free(nullptr, entries);
    VecReset(pending);
}

bool ShellWriteDependencyLinks(Str applicationRoot, int* linked, Str* error) {
    if (linked) *linked = 0;
    Str manifestPath = JoinPath(applicationRoot, Str(kShellManifestFile));
    bool present = PlatFileExists(manifestPath.s);
    StrFree(manifestPath);
    if (!present) return true;
    PluginManifest manifest;
    ShellError manifestError = {};
    if (!PluginManifestRead(applicationRoot, &manifest, &manifestError)) {
        DepError(error, manifestError.message);
        ShellErrorClear(&manifestError);
        return false;
    }
    ShellErrorClear(&manifestError);
    if (manifest.dependencies.len == 0) return true;
    GitDependencyStore store;
    MaterializedDependencies materialized;
    bool ok = store.MaterializeAll(manifest, &materialized, error) &&
              store.LinkForEditor(applicationRoot, materialized, linked, error);
    materialized.Free();
    return ok;
}

} // namespace gpui::shell
