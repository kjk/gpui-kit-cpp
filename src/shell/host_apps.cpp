// crates/shell/src/host/{installed,scaffold,agents}.rs: where an application
// comes from when the command line does not give a path, `new`, and `agents`.

#include "shell/host.h"

#include "shell/dependencies.h"
#include "shell/plugin.h"
#include "shell/runtime.h"
#include "shell/typings.h"

#include <stdio.h>
#include <string.h>

namespace gpui::shell {

extern const char kShellAgentsTemplate[];

static bool AbsolutePath(Str path) {
    if (!path || len(path) == 0) return false;
    if (path.s[0] == '/' || path.s[0] == '\\') return true;
    return len(path) >= 3 && path.s[1] == ':' &&
           (path.s[2] == '/' || path.s[2] == '\\');
}

static Str JoinOwned(Str left, Str right) {
    if (!left || len(left) == 0) return StrDup(right);
    if (!right || len(right) == 0) return StrDup(left);
    char last = left.s[len(left) - 1];
    bool separator = last != '/' && last != '\\';
    if (!separator) return StrDup(fmt("%s%s", left, right));
    return StrDup(fmt("%s%c%s", left, base::PlatPathSep(), right));
}

static void SetOwned(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

static bool WriteWhole(Str path, Str contents, Str* error) {
    if (!path || len(path) >= kMaxPath) {
        SetOwned(error, StrL("cannot write: the path is empty or too long"));
        return false;
    }
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "wb");
    if (!file) {
        SetOwned(error, fmt("cannot write %s", path));
        return false;
    }
    bool ok = len(contents) == 0 || fwrite(contents.s, 1, (size_t)len(contents),
                                           file) == (size_t)len(contents);
    if (fclose(file) != 0) ok = false;
    if (!ok) SetOwned(error, fmt("cannot write %s", path));
    return ok;
}

static Str ReadWhole(Str path) {
    if (!path || len(path) >= kMaxPath) return {};
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "rb");
    if (!file) return {};
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return {};
    }
    long size = ftell(file);
    if (size < 0 || size > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return {};
    }
    StrBuilder out;
    if (size > 0) {
        char* buffer = new char[(size_t)size];
        size_t got = fread(buffer, 1, (size_t)size, file);
        out.Append(Str(buffer, (int)got));
        delete[] buffer;
    }
    fclose(file);
    return out.TakeStr();
}

Str ShellHomeFrom(Str shellHome, Str home, Str userProfile) {
    if (shellHome && len(shellHome) > 0) {
        if (AbsolutePath(shellHome)) return StrDup(shellHome);
        char cwd[kMaxPath];
        cwd[0] = 0;
        PlatGetCwd(cwd, kMaxPath);
        return JoinOwned(Str(cwd), shellHome);
    }
    Str base = home && len(home) > 0 ? home : userProfile;
    Str ownedBase = {};
    if (!base || len(base) == 0) {
        const char* temp = getenv("TEMP");
        if (!temp || !temp[0]) temp = getenv("TMP");
        if (!temp || !temp[0]) temp = "/tmp";
        ownedBase = StrDup(Str(temp));
        base = ownedBase;
    }
    Str homeDir = JoinOwned(base, StrL(".gpui-shell"));
    StrFree(ownedBase);
    return homeDir;
}

Str ShellHome() {
    const char* shellHome = getenv("GPUI_SHELL_HOME");
    const char* home = getenv("HOME");
    const char* userProfile = getenv("USERPROFILE");
    return ShellHomeFrom(Str(shellHome ? shellHome : ""), Str(home ? home : ""),
                         Str(userProfile ? userProfile : ""));
}

Str ShellApplicationsDirectory() {
    Str home = ShellHome();
    Str apps = JoinOwned(home, StrL("apps"));
    StrFree(home);
    return apps;
}

static bool IsApplication(Str directory) {
    Str manifest = JoinOwned(directory, Str(kShellManifestFile));
    Str entry = JoinOwned(directory, StrL("main.js"));
    bool found = (manifest && PlatFileExists(manifest.s)) ||
                 (entry && PlatFileExists(entry.s));
    StrFree(manifest);
    StrFree(entry);
    return found;
}

bool ShellBundledApplication(Str* directory) {
    if (directory) *directory = {};
    char executable[kMaxPath];
    executable[0] = 0;
    PlatGetExeDir(executable, kMaxPath);
    if (!executable[0]) return false;
    Str root(executable);
    Str resources = JoinOwned(root, StrL(".."));
    Str resourcesApp = JoinOwned(resources, StrL("Resources"));
    Str bundled = JoinOwned(resourcesApp, StrL("app"));
    Str beside = JoinOwned(root, StrL("app"));
    StrFree(resources);
    StrFree(resourcesApp);
    bool found = false;
    if (IsApplication(bundled)) {
        if (directory)
            *directory = bundled;
        else
            StrFree(bundled);
        found = true;
        StrFree(beside);
    } else if (IsApplication(beside)) {
        if (directory)
            *directory = beside;
        else
            StrFree(beside);
        found = true;
        StrFree(bundled);
    } else {
        StrFree(bundled);
        StrFree(beside);
    }
    return found;
}

bool ShellResolveApplication(const Invocation& invocation, Str* directory,
                             Str* error) {
    if (directory) *directory = {};
    if (invocation.application == ApplicationKind::Installed) {
        Str apps = ShellApplicationsDirectory();
        Str candidate = JoinOwned(apps, invocation.directory);
        if (IsApplication(candidate)) {
            if (directory)
                *directory = candidate;
            else
                StrFree(candidate);
            StrFree(apps);
            return true;
        }
        SetOwned(error,
                 fmt("no application `%s` is installed: %s has no main.js or "
                     "gpui-shell.json. `gpui-shell list` shows what is "
                     "installed; to run a directory, use `gpui-shell run -d "
                     "%s`.",
                     invocation.directory, candidate, invocation.directory));
        StrFree(candidate);
        StrFree(apps);
        return false;
    }
    if (invocation.application == ApplicationKind::Path) {
        if (directory) *directory = StrDup(invocation.directory);
        return directory == nullptr || directory->s != nullptr;
    }
    if (invocation.application == ApplicationKind::Current) {
        char cwd[kMaxPath];
        cwd[0] = 0;
        PlatGetCwd(cwd, kMaxPath);
        if (!cwd[0]) {
            SetOwned(error, StrL("cannot read the current directory"));
            return false;
        }
        if (directory) *directory = StrDup(Str(cwd));
        return true;
    }
    if (invocation.application == ApplicationKind::Bundled) {
        if (ShellBundledApplication(directory)) return true;
        Str apps = ShellApplicationsDirectory();
        SetOwned(error,
                 fmt("nothing to run: no application is packaged beside this "
                     "executable. Run one with `gpui-shell run <name>` or "
                     "`gpui-shell run -d <directory>`; `gpui-shell list` "
                     "shows what is installed in %s.",
                     apps));
        StrFree(apps);
        return false;
    }
    SetOwned(error, StrL("no application was named"));
    return false;
}

static void PrintStr(FILE* file, Str value) {
    if (value) fwrite(value.s, 1, (size_t)len(value), file);
}

int ShellListApplications() {
    Str directory = ShellApplicationsDirectory();
    DirEntry* entries = new DirEntry[512];
    int count = directory ? PlatListDir(directory.s, entries, 512) : 0;
    Vec<Str> names;
    for (int i = 0; i < count; i++) {
        if (!entries[i].isDir) continue;
        Str path = JoinOwned(directory, Str(entries[i].name));
        if (IsApplication(path))
            VecAppend(names, path);
        else
            StrFree(path);
    }
    delete[] entries;
    for (int i = 1; i < names.len; i++) {
        Str item = names[i];
        int at = i;
        while (at > 0 && StrCmp(names[at - 1], item) > 0) {
            names[at] = names[at - 1];
            at--;
        }
        names[at] = item;
    }
    if (names.len == 0) {
        fprintf(stderr,
                "no application is installed in %s. Each application is a "
                "directory there holding main.js and, usually, "
                "gpui-shell.json.\n",
                directory ? directory.s : "");
        StrFree(directory);
        return 1;
    }
    for (int i = 0; i < names.len; i++) {
        Str root = names[i];
        int start = len(root);
        while (start > 0 && root.s[start - 1] != '/' &&
               root.s[start - 1] != '\\')
            start--;
        Str name = Str(root.s + start, len(root) - start);
        Str manifestPath = JoinOwned(root, Str(kShellManifestFile));
        bool hasManifest = manifestPath && PlatFileExists(manifestPath.s);
        StrFree(manifestPath);
        if (!hasManifest) {
            PrintStr(stdout, name);
            printf("\n    no %s: local defaults (storage only)\n",
                   kShellManifestFile);
            continue;
        }
        PluginManifest manifest;
        ShellError error = {};
        if (!PluginManifestRead(root, &manifest, &error)) {
            PrintStr(stdout, name);
            printf("\n    invalid: ");
            PrintStr(stdout, error.message);
            fputc('\n', stdout);
            ShellErrorClear(&error);
            continue;
        }
        PrintStr(stdout, name);
        printf("  ");
        PrintStr(stdout, manifest.name);
        fputc(' ', stdout);
        PrintStr(stdout, manifest.version);
        printf(" (");
        PrintStr(stdout, manifest.id);
        printf(")\n");
        Arena* scratch = ArenaNew();
        Str data = ShellAppDataDirectory(manifest.id, scratch, nullptr);
        Capabilities capabilities = manifest.Grant(root, data ? data : Str{});
        StrBuilder summary;
        capabilities.AppendSummary(&summary);
        Str lines = summary.TakeStr();
        if (!lines || len(lines) == 0) {
            printf("    no capabilities\n");
        } else {
            int at = 0;
            while (at < len(lines)) {
                int end = at;
                while (end < len(lines) && lines.s[end] != '\n') end++;
                printf("    ");
                fwrite(lines.s + at, 1, (size_t)(end - at), stdout);
                fputc('\n', stdout);
                at = end < len(lines) ? end + 1 : end;
            }
        }
        StrFree(lines);
        ArenaDelete(scratch);
        ShellErrorClear(&error);
    }
    for (int i = 0; i < names.len; i++) StrFree(names[i]);
    VecReset(names);
    StrFree(directory);
    return 0;
}

static void AppendSlug(StrBuilder* out, Str folder) {
    bool dash = false;
    bool any = false;
    for (int i = 0; i < len(folder); i++) {
        unsigned char c = (unsigned char)folder.s[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out->AppendChar((char)c);
            dash = false;
            any = true;
        } else if (any && !dash) {
            out->AppendChar('-');
            dash = true;
        }
    }
}

static Str ApplicationId(Str folder) {
    StrBuilder slug;
    AppendSlug(&slug, folder);
    Str text = slug.TakeStr();
    int end = len(text);
    while (end > 0 && text.s[end - 1] == '-') end--;
    StrBuilder id;
    id.Append(StrL("local."));
    if (end == 0)
        id.Append(StrL("app"));
    else
        id.Append(Str(text.s, end));
    StrFree(text);
    return id.TakeStr();
}

static bool SeparatorWord(char c) {
    return c == '-' || c == '_' || c == ' ' || c == '.';
}

static Str DisplayName(Str folder) {
    StrBuilder out;
    bool word = false;
    bool any = false;
    for (int i = 0; i < len(folder); i++) {
        char c = folder.s[i];
        if (SeparatorWord(c)) {
            word = false;
            continue;
        }
        if (!word && any) out.AppendChar(' ');
        if (!word && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out.AppendChar(c);
        word = true;
        any = true;
    }
    if (!any) return StrDup(StrL("App"));
    return out.TakeStr();
}

static void AppendJsonString(StrBuilder* out, Str text) {
    out->AppendChar('"');
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c == '"' || c == '\\') out->AppendChar('\\');
        out->AppendChar(c);
    }
    out->AppendChar('"');
}

static Str FolderName(Str directory) {
    int start = len(directory);
    while (start > 0 && directory.s[start - 1] != '/' &&
           directory.s[start - 1] != '\\')
        start--;
    return Str(directory.s + start, len(directory) - start);
}

static Str ManifestText(Str folder) {
    Str id = ApplicationId(folder);
    Str name = DisplayName(folder);
    StrBuilder out;
    out.Append(StrL("{\n  \"id\": "));
    AppendJsonString(&out, id);
    out.Append(StrL(",\n  \"name\": "));
    AppendJsonString(&out, name);
    out.Append(StrL(",\n  \"version\": \"0.1.0\",\n  \"shell-version\": \""));
    out.Append(Str(kShellVersion));
    out
        .Append(StrL("\",\n  \"entry\": \"main.js\",\n  \"capabilities\": {\n"
                     "    \"storage\": true\n  }\n}\n"));
    StrFree(id);
    StrFree(name);
    return out.TakeStr();
}

static Str EntryText(Str name, bool styled) {
    StrBuilder title;
    AppendJsonString(&title, name);
    Str quoted = title.TakeStr();
    StrBuilder out;
    if (styled) {
        out.Append(
            StrL("import { View, div } from \"gpui-kit\";\n"
                 "import { v_flex } from \"gpui-base\";\n"
                 "import { Button, TitleBar } from \"gpui-component\";\n"
                 "\n"
                 "export default class App extends View {\n"
                 "  init() {\n"
                 "    this.count = 0;\n"
                 "  }\n"
                 "\n"
                 "  render(cx) {\n"
                 "    return v_flex()\n"
                 "      .size_full()\n"
                 "      .bg(cx.theme().colors.background)\n"
                 "      .child(\n"
                 "        new TitleBar().child(\n"
                 "          "
                 "div().text_size(13).font_medium().text_color(cx.theme()."
                 "colors.foreground).child("));
        out.Append(quoted);
        out.Append(StrL(
            "),\n"
            "        ),\n"
            "      )\n"
            "      .child(\n"
            "        v_flex()\n"
            "          .flex_1()\n"
            "          .items_center()\n"
            "          .justify_center()\n"
            "          .gap(12)\n"
            "          "
            ".child(div().text_color(cx.theme().colors.foreground).child(`"
            "Clicked ${this.count} times`))\n"
            "          .child(\n"
            "            new Button(\"increment\").primary().label(\"Click "
            "me\").on_click((_event, cx) => {\n"
            "              this.count += 1;\n"
            "              cx.notify();\n"
            "            }),\n"
            "          ),\n"
            "      );\n"
            "  }\n"
            "}\n"));
    } else {
        out
            .Append(StrL("import { View, div } from \"gpui-kit\";\n"
                         "import { v_flex, Button } from \"gpui-base\";\n"
                         "\n"
                         "// "));
        out.Append(quoted);
        out.Append(
            StrL("\n"
                 "export default class App extends View {\n"
                 "  init() {\n"
                 "    this.count = 0;\n"
                 "  }\n"
                 "\n"
                 "  render(cx) {\n"
                 "    return v_flex()\n"
                 "      .size_full()\n"
                 "      .items_center()\n"
                 "      .justify_center()\n"
                 "      .gap(12)\n"
                 "      .bg(cx.theme().colors.background)\n"
                 "      "
                 ".child(div().text_color(cx.theme().colors.foreground).child(`"
                 "Clicked ${this.count} times`))\n"
                 "      .child(\n"
                 "        Button.new(\"increment\")\n"
                 "          .h(28)\n"
                 "          .px(12)\n"
                 "          .items_center()\n"
                 "          .justify_center()\n"
                 "          .rounded(6)\n"
                 "          .bg(cx.theme().colors.primary)\n"
                 "          .text_color(cx.theme().colors.primary_foreground)\n"
                 "          .on_click((_event, cx) => {\n"
                 "            this.count += 1;\n"
                 "            cx.notify();\n"
                 "          })\n"
                 "          .child(\"Click me\"),\n"
                 "      );\n"
                 "  }\n"
                 "}\n"));
    }
    StrFree(quoted);
    return out.TakeStr();
}

static bool DirectoryOccupied(Str directory) {
    if (!directory || !PlatDirExists(directory.s)) return false;
    DirEntry* entries = new DirEntry[64];
    int count = PlatListDir(directory.s, entries, 64);
    delete[] entries;
    return count > 0;
}

bool ShellCreateApplication(Str directory,
                            const FrozenComponentRegistry* components,
                            Str* created, Str* error) {
    if (created) *created = {};
    if (DirectoryOccupied(directory)) {
        SetOwned(error, fmt("`%s` already exists and is not empty; `new` only "
                            "creates an application",
                            directory));
        return false;
    }
    if (!DependencyMakeDirectories(directory, error)) {
        if (error && !*error)
            SetOwned(error, fmt("cannot create `%s`", directory));
        return false;
    }
    char canonical[kMaxPath];
    canonical[0] = 0;
    TempStr raw = StrDupTemp(directory);
    if (!raw || !PlatCanonicalPath(raw.s, canonical, kMaxPath)) {
        SetOwned(error, fmt("cannot read `%s`", directory));
        return false;
    }
    Str root = StrDup(Str(canonical));
    Str folder = FolderName(root);
    bool styled = components && components->ModuleSpecifier();
    Str manifest = ManifestText(folder);
    Str shown = DisplayName(folder);
    Str entry = EntryText(shown, styled);
    StrFree(shown);
    Str manifestPath = JoinOwned(root, Str(kShellManifestFile));
    Str entryPath = JoinOwned(root, StrL("main.js"));
    Str ignorePath = JoinOwned(root, StrL(".gitignore"));
    bool ok = WriteWhole(manifestPath, manifest, error) &&
              WriteWhole(entryPath, entry, error) &&
              WriteWhole(ignorePath,
                         StrL("gpui-kit.d.ts\nnode_modules/\ndist/\n"), error);
    StrFree(manifest);
    StrFree(entry);
    StrFree(manifestPath);
    StrFree(entryPath);
    StrFree(ignorePath);
    if (!ok) {
        StrFree(root);
        return false;
    }
    ShellError declared = {};
    if (!ShellWriteTypeDeclarations(root, nullptr, nullptr, &declared,
                                    components)) {
        SetOwned(error, fmt("cannot write type declarations: %s",
                            declared.message ? declared.message
                                             : StrL("unknown error")));
        ShellErrorClear(&declared);
        StrFree(root);
        return false;
    }
    ShellErrorClear(&declared);
    Str written = {};
    if (!ShellWriteAgents(root, components, &written, error)) {
        StrFree(written);
        StrFree(root);
        return false;
    }
    StrFree(written);
    if (created)
        *created = root;
    else
        StrFree(root);
    return true;
}

static void AppendReplaced(StrBuilder* out, Str text, Str name, Str version,
                           Str home, Str modules) {
    for (int i = 0; i < len(text);) {
        if (text.s[i] == '{') {
            struct Token {
                const char* key;
                Str value;
            };
            Token tokens[] = {{"{name}", name},
                              {"{version}", version},
                              {"{home}", home},
                              {"{modules}", modules}};
            bool replaced = false;
            for (const Token& token : tokens) {
                int n = (int)strlen(token.key);
                if (i + n <= len(text) &&
                    memcmp(text.s + i, token.key, (size_t)n) == 0) {
                    out->Append(token.value);
                    i += n;
                    replaced = true;
                    break;
                }
            }
            if (replaced) continue;
        }
        out->AppendChar(text.s[i]);
        i++;
    }
}

static Str AgentsMarker() {
    return StrDup(
        fmt("<!-- Generated by `gpui-shell agents`. Edits are "
            "replaced on the next run; delete this line to keep "
            "them. -->"));
}

Str ShellAgentsText(const FrozenComponentRegistry* components) {
    StrBuilder modules;
    gpui::ShellAppendAgentsModules(&modules, components);
    Str moduleText = modules.TakeStr();
    StrBuilder body;
    AppendReplaced(&body, Str(kShellAgentsTemplate), StrL("gpui-shell"),
                   Str(kShellVersion), StrL("~/.gpui-shell/apps"),
                   moduleText ? moduleText : Str{});
    Str marker = AgentsMarker();
    StrBuilder out;
    out.Append(marker);
    out.Append(StrL("\n\n"));
    Str bodyText = body.TakeStr();
    out.Append(bodyText);
    StrFree(bodyText);
    StrFree(marker);
    StrFree(moduleText);
    return out.TakeStr();
}

bool ShellWriteAgents(Str directory, const FrozenComponentRegistry* components,
                      Str* written, Str* error) {
    if (written) *written = {};
    if (!directory || !PlatDirExists(directory.s)) {
        SetOwned(error, fmt("`%s` is not a directory", directory));
        return false;
    }
    Str path = JoinOwned(directory, StrL("AGENTS.md"));
    Str text = ShellAgentsText(components);
    Str existing = ReadWhole(path);
    if (existing && StrEq(existing, text)) {
        StrFree(existing);
        StrFree(text);
        StrFree(path);
        return true;
    }
    Str marker = AgentsMarker();
    if (existing && !StrStartsWith(existing, marker)) {
        SetOwned(error,
                 fmt("%s exists and was not generated by gpui-shell; it is "
                     "left alone. Run `gpui-shell agents` to print the "
                     "instructions instead.",
                     path));
        StrFree(marker);
        StrFree(existing);
        StrFree(text);
        StrFree(path);
        return false;
    }
    StrFree(marker);
    StrFree(existing);
    bool ok = WriteWhole(path, text, error);
    StrFree(text);
    if (!ok) {
        StrFree(path);
        return false;
    }
    if (written)
        *written = path;
    else
        StrFree(path);
    return true;
}

} // namespace gpui::shell
