#include "shell/typings.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace gpui::shell {

constexpr int kTypesMaxEntries = kShellTypesMaxFiles + 1;
constexpr int kTypesMaxSourceBytes = 8 * 1024 * 1024;
constexpr int kTypesMaxDeclarationBytes = 2 * 1024 * 1024;

struct TypesDirectory {
    char path[kMaxPath] = {};
    int depth = 0;
};

static TempStr JoinPathTemp(Str directory, Str name) {
    if (!directory || !name) return {};
    bool separator = directory.s[directory.len - 1] != '/' &&
                     directory.s[directory.len - 1] != '\\';
    int len = directory.len + (separator ? 1 : 0) + name.len;
    if (len >= kMaxPath) return {};
    if (!separator) return fmt("%s%s", directory, name);
    return fmt("%s%c%s", directory, GPUI_OS_WINDOWS ? '\\' : '/', name);
}

static bool SourceImportsBuiltins(Str source) {
    static const char* specifiers[] = {"gpui-kit", "gpui", "gpui-base",
                                       "gpui-shell", "gpui-fps"};
    for (int i = 0; i < (int)(sizeof(specifiers) / sizeof(specifiers[0]));
         i++) {
        TempStr quoted = fmt("\"%s\"", Str(specifiers[i]));
        if (StrContains(source, quoted)) return true;
        quoted = fmt("'%s'", Str(specifiers[i]));
        if (StrContains(source, quoted)) return true;
    }
    return false;
}

static bool IsScript(Str name) {
    return StrEndsWith(name, ".js") || StrEndsWith(name, ".mjs");
}

static bool SkipDirectory(Str name) {
    return !name || name.s[0] == '.' || StrEq(name, StrL("node_modules")) ||
           StrEq(name, StrL("target"));
}

static bool AppendDirectory(Vec<TypesDirectory>* directories, Str path,
                            int depth) {
    if (!path || path.len >= kMaxPath) return false;
    TypesDirectory directory;
    memcpy(directory.path, path.s, (size_t)path.len);
    directory.path[path.len] = 0;
    directory.depth = depth;
    return VecAppend(*directories, directory);
}

static void AppendQuoted(StrBuilder* out, Str value) {
    for (int i = 0; i < value.len; i++) {
        char ch = value.s[i];
        if (ch == '\\' || ch == '"') out->AppendChar('\\');
        out->AppendChar(ch);
    }
}

static void AppendReindented(StrBuilder* out, Str declarations) {
    int common = INT_MAX;
    int at = 0;
    while (at < declarations.len) {
        int end = at;
        while (end < declarations.len && declarations.s[end] != '\n') end++;
        int first = at;
        while (first < end &&
               (declarations.s[first] == ' ' || declarations.s[first] == '\t'))
            first++;
        if (first < end && first - at < common) common = first - at;
        at = end + 1;
    }
    if (common == INT_MAX) common = 0;
    at = 0;
    while (at < declarations.len) {
        int lineEnd = at;
        while (lineEnd < declarations.len && declarations.s[lineEnd] != '\n')
            lineEnd++;
        int end = lineEnd;
        while (end > at && (declarations.s[end - 1] == ' ' ||
                            declarations.s[end - 1] == '\t'))
            end--;
        if (end == at) {
            out->AppendChar('\n');
        } else {
            int first = at;
            int remove = common;
            while (first < end && remove > 0 &&
                   (declarations.s[first] == ' ' ||
                    declarations.s[first] == '\t')) {
                first++;
                remove--;
            }
            out->Append(StrL("  "));
            out->Append(Str(declarations.s + first, end - first));
            out->AppendChar('\n');
        }
        at = lineEnd + 1;
    }
}

void ShellTypeDeclarations(StrBuilder* out, const HostModules* modules) {
    if (!out) return;
    AppendBuiltinTypeDeclarations(out);
    // The embedded declarations predate the Kit package rename. Both names
    // expose the same types, just as the runtime exposes the same values.
    out->Append(StrL(
        "\ndeclare module \"gpui-kit\" {\n  export * from \"gpui\";\n}\n"));
    for (int i = 0; i < HostModulesCount(modules); i++) {
        HostModule* module = HostModulesAt(modules, i);
        if (!module) continue;
        out->Append(StrL("\ndeclare module \""));
        AppendQuoted(out, module->Name());
        out->Append(StrL("\" {\n"));
        if (module->Declared()) {
            AppendReindented(out, module->Declared());
        } else {
            out->Append(StrL("  import { HostValue } from \"gpui\";\n\n"));
            for (int function = 0; function < module->FunctionCount();
                 function++) {
                Str name = module->FunctionName(function);
                out->Append(StrL("  export function "));
                out->Append(name);
                out->Append(StrL("(...args: HostValue[]): "));
                if (module->IsAsync(name))
                    out->Append(StrL("Promise<HostValue>"));
                else
                    out->Append(StrL("HostValue"));
                out->Append(StrL(";\n"));
            }
        }
        out->Append(StrL("}\n"));
    }
}

// What an editor has to be told before `gpui-kit.d.ts` and the linked packages
// mean anything. `EDITOR_CONFIG` in crates/shell/src/typings.rs, byte for
// byte, and the same settings examples/js_todolist/jsconfig.json was written
// against.
static const char* const kEditorConfig =
    R"JSON({
  "// why": [
    "Written once by gpui-shell, then yours: an existing jsconfig.json or",
    "tsconfig.json is never replaced, and this file is not rewritten.",
    "",
    "`moduleResolution` is how a bare specifier is answered. Left to be",
    "inferred it can still land on the resolution that never looks in",
    "node_modules, and a Git dependency the runtime resolves fine is",
    "underlined as a module the editor cannot find.",
    "",
    "`lib` decides which globals exist. The default hands a script the",
    "browser's — a `console`, a `localStorage`, a `Window` this runtime does",
    "not have — and their declarations collide with the ones gpui-kit.d.ts makes,",
    "so the file describing the API is itself reported as the error.",
    "",
    "`strictNullChecks` is off, and this one is the runtime's shape rather than",
    "a preference. A view assigns its state in `init`, which TypeScript cannot",
    "see as definite assignment the way it sees a constructor, so every field",
    "would read as possibly-undefined and every use would want a `?.` that",
    "means nothing at run time. Turning it on would buy noise, not safety."
  ],
  "compilerOptions": {
    "target": "ES2022",
    "module": "ES2022",
    "moduleResolution": "bundler",
    "lib": ["ES2022"],
    "checkJs": true,
    "strict": true,
    "strictNullChecks": false,
    "noEmit": true
  }
}
)JSON";

// `write_editor_config`: only when the directory has neither configuration
// file, so the first launch scaffolds one and everything after leaves the
// author's own settings alone.
static bool WriteEditorConfig(Str directory, bool* wrote, ShellError* error) {
    if (wrote) *wrote = false;
    TempStr path = JoinPathTemp(directory, Str(kShellConfigFile));
    TempStr existing = JoinPathTemp(directory, Str(kShellTypeScriptConfigFile));
    if (!path || !existing) {
        ShellErrorSet(error, StrL("editor configuration path is too long"));
        return false;
    }
    if (PlatFileExists(path.s) || PlatDirExists(path.s) ||
        PlatFileExists(existing.s) || PlatDirExists(existing.s))
        return true;
    Str contents(kEditorConfig);
    FILE* file = fopen(path.s, "wb");
    if (!file) {
        ShellErrorSet(error, fmt("cannot write `%s`", path));
        return false;
    }
    size_t count = fwrite(contents.s, 1, (size_t)contents.len, file);
    if (count != (size_t)contents.len || fclose(file) != 0) {
        ShellErrorSet(error, fmt("cannot write `%s`", path));
        return false;
    }
    if (wrote) *wrote = true;
    return true;
}

static bool HasSymlinkDeclaration(Str directory, DirEntry* entries,
                                  ShellError* error) {
    int count = PlatListDir(directory.s, entries, kTypesMaxEntries);
    if (count >= kTypesMaxEntries) {
        ShellErrorSet(error,
                      fmt("cannot safely inspect `%s` for an existing %s",
                          directory, Str(kShellTypesFile)));
        return true;
    }
    for (int i = 0; i < count; i++) {
        if (StrEq(Str(entries[i].name), kShellTypesFile) && entries[i]
                                                                .isSymlink) {
            ShellErrorSet(error, fmt("refusing to replace symlink `%s/%s`",
                                     directory, Str(kShellTypesFile)));
            return true;
        }
    }
    return false;
}

static bool RefreshTypes(Str directory, Str declarations, DirEntry* entries,
                         bool* changed, ShellError* error) {
    if (changed) *changed = false;
    if (HasSymlinkDeclaration(directory, entries, error)) return false;
    TempStr path = JoinPathTemp(directory, Str(kShellTypesFile));
    if (!path) {
        ShellErrorSet(error, StrL("type declaration path is too long"));
        return false;
    }
    TempStr current = ReadBoundedFileTemp(path, kTypesMaxDeclarationBytes);
    if (current.s && StrEq(current, declarations)) {
        return true;
    }
    FILE* file = fopen(path.s, "wb");
    if (!file) {
        ShellErrorSet(error, fmt("cannot write `%s`", path));
        return false;
    }
    size_t count = fwrite(declarations.s, 1, (size_t)declarations.len, file);
    bool ok = count == (size_t)declarations.len && fclose(file) == 0;
    if (!ok) {
        ShellErrorSet(error, fmt("cannot write `%s`", path));
        return false;
    }
    if (changed) *changed = true;
    return true;
}

bool ShellWriteTypeDeclarations(Str root, const HostModules* modules,
                                int* written, ShellError* error) {
    ShellErrorClear(error);
    if (written) *written = 0;
    if (!root || root.len >= kMaxPath) {
        ShellErrorSet(error,
                      StrL("application directory is empty or too long"));
        return false;
    }
    TypesDirectory rootDirectory;
    memcpy(rootDirectory.path, root.s, (size_t)root.len);
    rootDirectory.path[root.len] = 0;
    if (!PlatDirExists(rootDirectory.path)) {
        ShellErrorSet(error,
                      fmt("application directory `%s` does not exist", root));
        return false;
    }

    StrBuilder declarations;
    ShellTypeDeclarations(&declarations, modules);
    Str text = declarations.TakeStr();
    if (!text || text.len > kTypesMaxDeclarationBytes) {
        StrFree(text);
        ShellErrorSet(error, StrL("type declarations exceed the size limit"));
        return false;
    }

    // The application root only: one project, one configuration, and a nested
    // directory that happens to import `gpui` is part of it rather than a
    // second project.
    bool wroteConfig = false;
    if (!WriteEditorConfig(root, &wroteConfig, error)) {
        StrFree(text);
        return false;
    }
    if (wroteConfig && written) (*written)++;

    Vec<TypesDirectory> pending;
    Vec<TypesDirectory> targets;
    bool ok =
        VecAppend(pending, rootDirectory) && VecAppend(targets, rootDirectory);
    DirEntry* entries = ok ? AllocArray<DirEntry>(kTypesMaxEntries) : nullptr;
    if (!entries) ok = false;
    int files = 0;
    while (ok && pending.len > 0 && files <= kShellTypesMaxFiles) {
        TypesDirectory directory = pending[pending.len - 1];
        pending.len--;
        int count = PlatListDir(directory.path, entries, kTypesMaxEntries);
        if (count >= kTypesMaxEntries) break;
        bool imports = false;
        for (int i = 0; i < count && files <= kShellTypesMaxFiles; i++) {
            const DirEntry& item = entries[i];
            if (item.isSymlink || item.name[0] == '.') continue;
            if (item.isDir) {
                if (directory.depth >= kShellTypesMaxDepth ||
                    SkipDirectory(Str(item.name)))
                    continue;
                TempStr child =
                    JoinPathTemp(Str(directory.path), Str(item.name));
                if (!child ||
                    !AppendDirectory(&pending, child, directory.depth + 1)) {
                    ok = false;
                    break;
                }
            } else if (item.isFile) {
                files++;
                if (imports || !IsScript(Str(item.name))) continue;
                TempStr sourcePath =
                    JoinPathTemp(Str(directory.path), Str(item.name));
                TempStr source =
                    sourcePath
                        ? ReadBoundedFileTemp(sourcePath, kTypesMaxSourceBytes)
                        : TempStr{};
                if (source.s) {
                    imports = SourceImportsBuiltins(source);
                }
            }
        }
        if (imports && !StrEq(Str(directory.path), root))
            ok =
                AppendDirectory(&targets, Str(directory.path), directory.depth);
    }

    for (int i = 0; ok && i < targets.len; i++) {
        bool changed = false;
        ok = RefreshTypes(Str(targets[i].path), text, entries, &changed, error);
        if (ok && changed && written) (*written)++;
    }
    if (!ok && error && !error->IsSet())
        ShellErrorSet(error,
                      StrL("out of memory while writing type declarations"));
    Free(nullptr, entries);
    VecReset(targets);
    VecReset(pending);
    StrFree(text);
    return ok;
}

} // namespace gpui::shell
