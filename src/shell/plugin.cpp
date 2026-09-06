#include "shell/plugin.h"

#include "base/json.h"
#include "shell/filesystem.h"
#include "shell/host_modules.h"
#include "shell/scope.h"

#include <stdio.h>
#include <stdlib.h>

namespace gpui::shell {

static void SetError(ShellError* error, Str message) {
    ShellErrorSet(error, message);
}

static Str Join(Arena* arena, Str left, Str right) {
    StrBuilder path(arena);
    path.Append(left);
    if (left && left.s[left.len - 1] != '/' && left.s[left.len - 1] != '\\')
        path.AppendChar(GPUI_OS_WINDOWS ? '\\' : '/');
    path.Append(right);
    return path.TakeStr();
}

static const char* JsonTypeName(const JsonValue* value) {
    if (!value) return "missing";
    static const char names[] =
        "null\0a boolean\0a number\0a string\0an array\0an object\0";
    Str name = SeqStrByIndex(names, (int)value->kind);
    return name ? name.s : "a value";
}

static bool HasOnly(const JsonValue* object, const char* const* names,
                    int count, Str where, ShellError* error) {
    if (!object || object->kind != JsonKind::Object) {
        SetError(error, fmt("%s must be an object, found %s", where,
                            Str(JsonTypeName(object))));
        return false;
    }
    for (const JsonValue* field = object->first; field; field = field->next) {
        bool known = false;
        for (int i = 0; i < count; i++)
            if (StrEq(field->key, names[i])) known = true;
        if (!known) {
            SetError(error, fmt("unknown field `%s` in %s", field->key, where));
            return false;
        }
    }
    return true;
}

static bool RequiredString(const JsonValue* object, const char* field, Str* out,
                           ShellError* error) {
    const JsonValue* value = JsonGet(object, field);
    if (!value || value->kind == JsonKind::Null) {
        SetError(error, fmt("missing field `%s`", Str(field)));
        return false;
    }
    if (value->kind != JsonKind::String) {
        SetError(error, fmt("field `%s` must be a string, found %s", Str(field),
                            Str(JsonTypeName(value))));
        return false;
    }
    if (value->str.len == 0) {
        SetError(error, fmt("field `%s` is empty", Str(field)));
        return false;
    }
    *out = value->str;
    return true;
}

static bool ParseSemver(Str value, int* major, int* minor, int* patch) {
    int parts[3] = {};
    int at = 0;
    for (int part = 0; part < 3; part++) {
        if (at >= value.len || value.s[at] < '0' || value.s[at] > '9')
            return false;
        if (value.s[at] == '0' && at + 1 < value.len &&
            value.s[at + 1] >= '0' && value.s[at + 1] <= '9')
            return false;
        int number = 0;
        while (at < value.len && value.s[at] >= '0' && value.s[at] <= '9') {
            if (number > 100000000) return false;
            number = number * 10 + value.s[at++] - '0';
        }
        parts[part] = number;
        if (part < 2) {
            if (at >= value.len || value.s[at] != '.') return false;
            at++;
        }
    }
    if (at < value.len) {
        if (value.s[at] != '-' && value.s[at] != '+') return false;
        for (; at < value.len; at++) {
            char ch = value.s[at];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' ||
                  ch == '+'))
                return false;
        }
    }
    if (major) *major = parts[0];
    if (minor) *minor = parts[1];
    if (patch) *patch = parts[2];
    return true;
}

static bool ValidId(Str id) {
    if (!id) return false;
    for (int i = 0; i < id.len; i++) {
        char ch = id.s[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              ch == '.' || ch == '-' || ch == '_'))
            return false;
    }
    char first = id.s[0];
    char last = id.s[id.len - 1];
    if (first == '.' || first == '-' || first == '_' || last == '.' ||
        last == '-' || last == '_')
        return false;
    return StrFind(id, StrL("..")) < 0;
}

static bool ValidEntry(Str entry) {
    if (!entry || StrStartsWithAny(entry, "/\\") ||
        StrFind(entry, StrL(":")) >= 0)
        return false;
    int start = 0;
    for (int i = 0; i <= entry.len; i++) {
        if (i < entry.len && entry.s[i] != '/' && entry.s[i] != '\\') continue;
        if (i - start == 2 && entry.s[start] == '.' &&
            entry.s[start + 1] == '.')
            return false;
        start = i + 1;
    }
    return true;
}

static bool ParseStringArray(Arena* arena, const JsonValue* value,
                             Vec<Str>* out, Str field, ShellError* error,
                             bool required = false) {
    if (!value) {
        if (required) SetError(error, fmt("%s must be an array", field));
        return !required;
    }
    if (value->kind != JsonKind::Array) {
        SetError(error, fmt("%s must be an array", field));
        return false;
    }
    for (const JsonValue* item = value->first; item; item = item->next) {
        if (item->kind != JsonKind::String || !item->str) {
            SetError(error, fmt("%s entries must be non-empty strings", field));
            return false;
        }
        VecAppend(*out, StrDup(arena, item->str));
    }
    return true;
}

static bool ValidatePlaceholders(const Vec<Str>& paths, Str field,
                                 ShellError* error) {
    for (int p = 0; p < paths.len; p++) {
        Str value = paths[p];
        for (int i = 0; i + 2 < value.len; i++) {
            if (value.s[i] != '$' || value.s[i + 1] != '{') continue;
            int end = i + 2;
            while (end < value.len && value.s[end] != '}') end++;
            if (end >= value.len) {
                SetError(error, fmt("unterminated placeholder in %s", field));
                return false;
            }
            Str placeholder(value.s + i, end - i + 1);
            if (!StrEq(placeholder, StrL("${pluginDir}")) &&
                !StrEq(placeholder, StrL("${dataDir}"))) {
                SetError(error, fmt("unknown placeholder `%s` in %s",
                                    placeholder, field));
                return false;
            }
            i = end;
        }
    }
    return true;
}

// --- Git-backed dependencies -------------------------------------------
//
// Ports crates/shell/src/plugin.rs's `GitDependency`, its string shorthand
// and `validate_dependencies`. The manifest is the whole description of a
// dependency, and every question a Git command would otherwise discover is
// answered here, before one runs.

static Str TrimAscii(Str value) {
    int start = 0;
    int end = value.len;
    while (start < end && (uint8_t)value.s[start] <= ' ') start++;
    while (end > start && (uint8_t)value.s[end - 1] <= ' ') end--;
    return Str(value.s + start, end - start);
}

// `valid_git_ref_name`: git-check-ref-format's rules, so a selector cannot be
// a refspec.
static bool ValidGitRefName(Str reference) {
    if (!reference || StrEq(reference, StrL("@"))) return false;
    char first = reference.s[0];
    char last = reference.s[reference.len - 1];
    if (first == '.' || first == '/' || last == '.' || last == '/')
        return false;
    if (StrContains(reference, StrL("..")) ||
        StrContains(reference, StrL("@{")) ||
        StrContains(reference, StrL("//")))
        return false;
    for (int i = 0; i < reference.len; i++) {
        uint8_t c = (uint8_t)reference.s[i];
        if (c < 0x20 || c == 0x7f || c == ' ' || c == '~' || c == '^' ||
            c == ':' || c == '?' || c == '*' || c == '[' || c == '\\')
            return false;
    }
    int start = 0;
    for (int i = 0; i <= reference.len; i++) {
        if (i < reference.len && reference.s[i] != '/') continue;
        Str component(reference.s + start, i - start);
        if (component.len == 0 || component.s[0] == '.' ||
            StrEndsWith(component, ".lock"))
            return false;
        start = i + 1;
    }
    return true;
}

static bool ValidGitHubComponent(Str component) {
    if (component.len == 0 || StrEq(component, StrL(".")) ||
        StrEq(component, StrL("..")))
        return false;
    for (int i = 0; i < component.len; i++) {
        char c = component.s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

static bool LooksLikeScpGitUrl(Str remote) {
    int colon = StrFind(remote, StrL(":"));
    if (colon < 0) return false;
    Str authority(remote.s, colon);
    Str path(remote.s + colon + 1, remote.len - colon - 1);
    return StrContains(authority, StrL("@")) &&
           !StrContains(authority, StrL("/")) && path.len > 0 &&
           StrContains(path, StrL("/"));
}

// `parse_git_dependency_string`: a Git URL or a GitHub `owner/repository`,
// each with at most one `#ref`.
static bool ParseGitDependencyString(Arena* arena, Str source, Str* git,
                                     Str* reference, Str* detail) {
    int hashes = 0;
    for (int i = 0; i < source.len; i++)
        if (source.s[i] == '#') hashes++;
    if (!StrEq(TrimAscii(source), source) || source.len == 0 || hashes > 1) {
        *detail = StrL(
            "a string dependency must be a Git URL or GitHub "
            "owner/repository with one optional #Git ref");
        return false;
    }
    int hash = StrFind(source, StrL("#"));
    Str remote = hash < 0 ? source : Str(source.s, hash);
    Str fragment = {};
    if (hash >= 0) {
        fragment = Str(source.s + hash + 1, source.len - hash - 1);
        if (fragment.len == 0) {
            *detail = StrL("a string dependency #Git ref must not be empty");
            return false;
        }
        if (!ValidGitRefName(fragment)) {
            *detail =
                StrDup(arena, fmt("string dependency selector `%s` is not a "
                                  "valid Git ref",
                                  fragment));
            return false;
        }
    }

    if (StrContains(remote, StrL("://")) || LooksLikeScpGitUrl(remote)) {
        bool whitespace = false;
        for (int i = 0; i < remote.len; i++)
            if ((uint8_t)remote.s[i] <= ' ') whitespace = true;
        if (whitespace || StrEndsWith(remote, "://") ||
            StrStartsWith(remote, "://")) {
            *detail = StrL("a string dependency must contain a valid Git URL");
            return false;
        }
        *git = StrDup(arena, remote);
        *reference = fragment ? StrDup(arena, fragment) : Str{};
        return true;
    }

    Str owner = {};
    Str repository = {};
    int components = 0;
    int start = 0;
    for (int i = 0; i <= remote.len; i++) {
        if (i < remote.len && remote.s[i] != '/') continue;
        Str component(remote.s + start, i - start);
        if (components == 0)
            owner = component;
        else if (components == 1)
            repository = component;
        components++;
        start = i + 1;
    }
    if (components != 2 || !ValidGitHubComponent(owner) ||
        !ValidGitHubComponent(repository)) {
        *detail = StrL(
            "GitHub shorthand must contain exactly owner/repository plus an "
            "optional #Git ref");
        return false;
    }
    *git = StrDup(arena, fmt("https://github.com/%s/%s", owner, repository));
    *reference = StrDup(arena, fragment ? fragment : StrL("main"));
    return true;
}

static bool ValidBareModuleName(Str name) {
    if (!name || StrStartsWithAny(name, "./")) return false;
    if (StrContains(name, StrL("\\")) || StrContains(name, StrL(":")))
        return false;
    int start = 0;
    for (int i = 0; i <= name.len; i++) {
        if (i < name.len && name.s[i] != '/') continue;
        Str part(name.s + start, i - start);
        if (part.len == 0 || StrEq(part, StrL(".."))) return false;
        start = i + 1;
    }
    return true;
}

static void SetDependencyError(ShellError* error, Str detail) {
    SetError(error, fmt("invalid `dependencies`: %s. Use a GitHub "
                        "owner/repository shorthand or full Git URL with an "
                        "optional #ref, or an object with a Git URL, exactly "
                        "one non-empty `branch` or `tag`, and an optional "
                        "repository-relative `entry`",
                        detail));
}

// `validate_dependencies`: everything answerable before a Git command runs.
static bool ValidateDependency(const GitDependency& dependency,
                               ShellError* error) {
    Str name = dependency.name;
    if (!ValidBareModuleName(name)) {
        SetDependencyError(error,
                           fmt("`%s` is not a valid bare module name", name));
        return false;
    }
    if (HostIsReservedSpecifier(name)) {
        SetDependencyError(error, fmt("`%s` is reserved by gpui-shell and "
                                      "cannot name a Git dependency",
                                      name));
        return false;
    }
    if (TrimAscii(dependency.git).len == 0) {
        SetDependencyError(error, fmt("`%s.git` must not be empty", name));
        return false;
    }
    if (dependency.packageEntry) {
        if (dependency.reference && !ValidGitRefName(dependency.reference)) {
            SetDependencyError(
                error, fmt("`%s` selector `%s` is not a valid Git ref", name,
                           dependency.reference));
            return false;
        }
        return true;
    }
    bool hasBranch = dependency.branch && TrimAscii(dependency.branch).len > 0;
    bool hasTag = dependency.tag && TrimAscii(dependency.tag).len > 0;
    if (dependency.branch && dependency.tag) {
        SetDependencyError(
            error,
            fmt("`%s` must select either `branch` or `tag`, not both", name));
        return false;
    }
    if (hasBranch == hasTag) {
        SetDependencyError(
            error,
            fmt("`%s` must select one non-empty `branch` or `tag`", name));
        return false;
    }
    Str reference = hasBranch ? dependency.branch : dependency.tag;
    if (!ValidGitRefName(reference)) {
        SetDependencyError(
            error, fmt("`%s` selector `%s` is not a valid Git ref name", name,
                       reference));
        return false;
    }
    if (!ValidEntry(dependency.entry)) {
        SetDependencyError(
            error,
            fmt("`%s.entry` must be a path inside the Git repository", name));
        return false;
    }
    return true;
}

// Rust's BTreeMap iterates by key; the store, the resolver and the editor
// links all walk dependencies in that order, so the vector is kept sorted.
static bool DependencyNameOrdered(Str left, Str right) {
    int shared = left.len < right.len ? left.len : right.len;
    for (int i = 0; i < shared; i++) {
        uint8_t a = (uint8_t)left.s[i];
        uint8_t b = (uint8_t)right.s[i];
        if (a != b) return a < b;
    }
    return left.len <= right.len;
}

static bool ParseDependencies(const JsonValue* value, PluginManifest* out,
                              ShellError* error) {
    if (!value || value->kind == JsonKind::Null) return true;
    if (value->kind != JsonKind::Object) {
        SetDependencyError(error, StrL("the block must be an object"));
        return false;
    }
    for (const JsonValue* field = value->first; field; field = field->next) {
        GitDependency dependency;
        dependency.name = StrDup(out->arena, field->key);
        if (field->kind == JsonKind::String) {
            Str detail = {};
            if (!ParseGitDependencyString(out->arena, field->str,
                                          &dependency.git,
                                          &dependency.reference, &detail)) {
                SetDependencyError(error, detail);
                return false;
            }
            dependency.entry = Str(kGitDependencyDefaultEntry);
            dependency.packageEntry = true;
        } else if (field->kind == JsonKind::Object) {
            static const char* fields[] = {"git", "branch", "tag", "entry"};
            bool known = true;
            for (const JsonValue* member = field->first; member;
                 member = member->next) {
                bool found = false;
                for (int i = 0; i < 4; i++)
                    if (StrEq(member->key, fields[i])) found = true;
                if (!found) {
                    SetDependencyError(
                        error, fmt("unknown field `%s` in `%s`", member->key,
                                   dependency.name));
                    known = false;
                    break;
                }
            }
            if (!known) return false;
            const JsonValue* git = JsonGet(field, "git");
            const JsonValue* branch = JsonGet(field, "branch");
            const JsonValue* tag = JsonGet(field, "tag");
            const JsonValue* entry = JsonGet(field, "entry");
            if (!git || git->kind != JsonKind::String ||
                (branch && branch->kind != JsonKind::Null &&
                 branch->kind != JsonKind::String) ||
                (tag && tag->kind != JsonKind::Null &&
                 tag->kind != JsonKind::String) ||
                (entry && entry->kind != JsonKind::Null &&
                 entry->kind != JsonKind::String)) {
                SetDependencyError(
                    error, fmt("`%s` must name a string `git`, and string "
                               "`branch`, `tag` and `entry` where present",
                               dependency.name));
                return false;
            }
            dependency.git = StrDup(out->arena, git->str);
            if (branch && branch->kind == JsonKind::String)
                dependency.branch = StrDup(out->arena, branch->str);
            if (tag && tag->kind == JsonKind::String)
                dependency.tag = StrDup(out->arena, tag->str);
            dependency.entry = entry && entry->kind == JsonKind::String
                                   ? StrDup(out->arena, entry->str)
                                   : Str(kGitDependencyDefaultEntry);
        } else {
            SetDependencyError(error, fmt("`%s` must be a string or an object",
                                          dependency.name));
            return false;
        }
        if (!ValidateDependency(dependency, error)) return false;
        int at = out->dependencies.len;
        VecAppend(out->dependencies, dependency);
        while (at > 0 && !DependencyNameOrdered(out->dependencies[at - 1].name,
                                                out->dependencies[at].name)) {
            GitDependency swap = out->dependencies[at - 1];
            out->dependencies[at - 1] = out->dependencies[at];
            out->dependencies[at] = swap;
            at--;
        }
    }
    return true;
}

static bool ParseCapabilities(const JsonValue* value, PluginManifest* out,
                              ShellError* error) {
    if (!value || value->kind == JsonKind::Null) return true;
    static const char* fields[] = {"fs", "network", "storage", "clipboard",
                                   "process"};
    if (!HasOnly(value, fields, 5, StrL("capabilities"), error)) return false;
    const JsonValue* storage = JsonGet(value, "storage");
    if (storage) {
        if (storage->kind != JsonKind::Bool) {
            SetError(error, StrL("capabilities.storage must be a boolean"));
            return false;
        }
        out->storage = storage->b;
    }
    const JsonValue* fs = JsonGet(value, "fs");
    if (fs && fs->kind != JsonKind::Null) {
        static const char* fsFields[] = {"read", "write", "execute"};
        if (!HasOnly(fs, fsFields, 3, StrL("capabilities.fs"), error) ||
            !ParseStringArray(out->arena, JsonGet(fs, "read"), &out->readRoots,
                              StrL("capabilities.fs.read"), error) ||
            !ParseStringArray(out->arena, JsonGet(fs, "write"),
                              &out->writeRoots, StrL("capabilities.fs.write"),
                              error))
            return false;
        if (!ValidatePlaceholders(out->readRoots, StrL("capabilities.fs.read"),
                                  error) ||
            !ValidatePlaceholders(out->writeRoots,
                                  StrL("capabilities.fs.write"), error))
            return false;
        const JsonValue* execute = JsonGet(fs, "execute");
        if (execute && execute->kind == JsonKind::String &&
            StrEq(execute->str, StrL("*"))) {
            out->executeUnrestricted = true;
        } else if (execute &&
                   !ParseStringArray(out->arena, execute, &out->execute,
                                     StrL("capabilities.fs.execute"), error)) {
            return false;
        }
    }
    const JsonValue* network = JsonGet(value, "network");
    if (network && network->kind != JsonKind::Null) {
        static const char* networkFields[] = {"hosts", "http"};
        if (!HasOnly(network, networkFields, 2, StrL("capabilities.network"),
                     error) ||
            !ParseStringArray(out->arena, JsonGet(network, "hosts"),
                              &out->networkHosts,
                              StrL("capabilities.network.hosts"), error))
            return false;
        for (int i = 0; i < out->networkHosts.len; i++) {
            Str host = out->networkHosts[i];
            if (!host || StrFind(host, StrL("://")) >= 0 ||
                StrFind(host, StrL("/")) >= 0) {
                SetError(error, fmt("network host `%s` must be a hostname "
                                    "without a scheme or path",
                                    host));
                return false;
            }
        }
        const JsonValue* http = JsonGet(network, "http");
        if (http && http->kind != JsonKind::Array) {
            SetError(error, StrL("capabilities.network.http must be an array"));
            return false;
        }
        for (const JsonValue* rule = http ? http->first : nullptr; rule;
             rule = rule->next) {
            static const char* httpFields[] = {
                "scheme", "host", "port", "methods", "paths", "path_prefixes"};
            if (!HasOnly(rule, httpFields, 6,
                         StrL("capabilities.network.http entry"), error))
                return false;
            auto* parsed = ArenaNew<PluginHttpGrant>(out->arena);
            const JsonValue* scheme = JsonGet(rule, "scheme");
            parsed->scheme =
                StrDup(out->arena, scheme && scheme->kind == JsonKind::String
                                       ? scheme->str
                                       : StrL("https"));
            Str host;
            if (!RequiredString(rule, "host", &host, error)) return false;
            parsed->host = StrDup(out->arena, host);
            if ((!StrEq(parsed->scheme, StrL("http")) &&
                 !StrEq(parsed->scheme, StrL("https"))) ||
                StrFind(host, StrL("://")) >= 0 ||
                StrFind(host, StrL("/")) >= 0) {
                SetError(
                    error,
                    StrL("invalid capabilities.network.http scheme or host"));
                return false;
            }
            const JsonValue* port = JsonGet(rule, "port");
            if (port) {
                if (port->kind != JsonKind::Number || port->num < 1 ||
                    port->num > 65535 || port->num != (int)port->num) {
                    SetError(
                        error,
                        StrL(
                            "capabilities.network.http port must be 1..65535"));
                    return false;
                }
                parsed->hasPort = true;
                parsed->port = (uint16_t)port->num;
            }
            if (!ParseStringArray(
                    out->arena, JsonGet(rule, "methods"), &parsed->methods,
                    StrL("capabilities.network.http.methods"), error, true) ||
                parsed->methods.len == 0 ||
                !ParseStringArray(
                    out->arena, JsonGet(rule, "paths"), &parsed->paths,
                    StrL("capabilities.network.http.paths"), error) ||
                !ParseStringArray(
                    out->arena, JsonGet(rule, "path_prefixes"),
                    &parsed->pathPrefixes,
                    StrL("capabilities.network.http.path_prefixes"), error))
                return false;
            for (int i = 0; i < parsed->methods.len; i++) {
                if (!StrEq(parsed->methods[i], StrL("GET")) &&
                    !StrEq(parsed->methods[i], StrL("POST"))) {
                    SetError(error, fmt("invalid HTTP method `%s`",
                                        parsed->methods[i]));
                    return false;
                }
            }
            for (int pass = 0; pass < 2; pass++) {
                Vec<Str>& paths = pass ? parsed->pathPrefixes : parsed->paths;
                for (int i = 0; i < paths.len; i++) {
                    if (!paths[i] || paths[i].s[0] != '/') {
                        SetError(error,
                                 StrL("HTTP grant paths must start with `/`"));
                        return false;
                    }
                }
            }
            VecAppend(out->http, parsed);
        }
    }
    const JsonValue* clipboard = JsonGet(value, "clipboard");
    if (clipboard && clipboard->kind != JsonKind::Null) {
        static const char* clipboardFields[] = {"read", "write"};
        if (!HasOnly(clipboard, clipboardFields, 2,
                     StrL("capabilities.clipboard"), error))
            return false;
        const JsonValue* read = JsonGet(clipboard, "read");
        const JsonValue* write = JsonGet(clipboard, "write");
        if ((read && read->kind != JsonKind::Bool) ||
            (write && write->kind != JsonKind::Bool)) {
            SetError(error, StrL("clipboard grants must be booleans"));
            return false;
        }
        out->clipboardRead = read && read->b;
        out->clipboardWrite = write && write->b;
    }
    const JsonValue* process = JsonGet(value, "process");
    if (process && process->kind != JsonKind::Null) {
        static const char* processFields[] = {"exit"};
        if (!HasOnly(process, processFields, 1, StrL("capabilities.process"),
                     error))
            return false;
        const JsonValue* exit = JsonGet(process, "exit");
        if (exit && exit->kind != JsonKind::Bool) {
            SetError(error,
                     StrL("capabilities.process.exit must be a boolean"));
            return false;
        }
        out->exit = exit && exit->b;
    }
    return true;
}

PluginManifest::PluginManifest() : arena(ArenaNew()) {}

PluginManifest::~PluginManifest() {
    VecReset(dependencies);
    VecReset(readRoots);
    VecReset(writeRoots);
    VecReset(execute);
    VecReset(networkHosts);
    for (int i = 0; i < http.len; i++) {
        VecReset(http[i]->methods);
        VecReset(http[i]->paths);
        VecReset(http[i]->pathPrefixes);
    }
    VecReset(http);
    ArenaDelete(arena);
}

bool PluginManifestParse(Str source, PluginManifest* out, ShellError* error) {
    ShellErrorClear(error);
    if (!out || !out->arena) {
        SetError(error, StrL("manifest output is not initialized"));
        return false;
    }
    JsonValue* root = JsonParse(out->arena, source);
    if (!root) {
        SetError(error, StrL("the manifest is not valid JSON"));
        return false;
    }
    static const char* fields[] = {
        "id",    "name",         "version",     "shell-version",
        "entry", "dependencies", "capabilities"};
    if (!HasOnly(root, fields, 7, StrL("the manifest"), error)) return false;
    Str id, name, entry;
    if (!RequiredString(root, "id", &id, error) ||
        !RequiredString(root, "name", &name, error) ||
        !RequiredString(root, "entry", &entry, error))
        return false;
    if (!ValidId(id)) {
        SetError(error,
                 fmt("invalid `id` `%s`: use lowercase letters, digits, `.`, "
                     "`-` and `_`, beginning and ending with a letter or digit",
                     id));
        return false;
    }
    if (!ValidEntry(entry)) {
        SetError(error, fmt("invalid `entry` `%s`: expected a path inside the "
                            "plugin directory",
                            entry));
        return false;
    }
    const JsonValue* version = JsonGet(root, "version");
    Str versionText = version && version->kind != JsonKind::Null
                          ? JsonString(version)
                          : StrL("unknown");
    if ((version && version->kind != JsonKind::Null && !versionText) ||
        (versionText && !StrEq(versionText, StrL("unknown")) &&
         !ParseSemver(versionText, nullptr, nullptr, nullptr))) {
        SetError(error,
                 fmt("invalid `version` `%s`: expected a semantic version",
                     versionText));
        return false;
    }
    const JsonValue* shellVersion = JsonGet(root, "shell-version");
    Str required = shellVersion && shellVersion->kind != JsonKind::Null
                       ? JsonString(shellVersion)
                       : Str(kShellVersion);
    int requiredMajor = 0, requiredMinor = 0, requiredPatch = 0;
    int runtimeMajor = 0, runtimeMinor = 0, runtimePatch = 0;
    if (!required || !ParseSemver(required, &requiredMajor, &requiredMinor,
                                  &requiredPatch)) {
        SetError(
            error,
            fmt("invalid `shell-version` `%s`: expected a semantic version",
                required));
        return false;
    }
    ParseSemver(Str(kShellVersion), &runtimeMajor, &runtimeMinor,
                &runtimePatch);
    bool oldEnough =
        runtimeMajor > requiredMajor ||
        (runtimeMajor == requiredMajor &&
         (runtimeMinor > requiredMinor ||
          (runtimeMinor == requiredMinor && runtimePatch >= requiredPatch)));
    if (!oldEnough) {
        SetError(error, fmt("this application requires gpui-shell %s, but this "
                            "runtime is %s and is not compatible",
                            required, Str(kShellVersion)));
        return false;
    }
    out->id = StrDup(out->arena, id);
    out->name = StrDup(out->arena, name);
    out->version = StrDup(out->arena, versionText);
    out->shellVersion = StrDup(out->arena, required);
    out->entry = StrDup(out->arena, entry);
    if (!ParseDependencies(JsonGet(root, "dependencies"), out, error))
        return false;
    return ParseCapabilities(JsonGet(root, "capabilities"), out, error);
}

bool PluginManifestRead(Str directory, PluginManifest* out, ShellError* error) {
    Arena* scratch = ArenaNew();
    Str path = Join(scratch, directory, Str(kShellManifestFile));
    TempStr source = ReadBoundedFileTemp(path, kShellMaxManifestBytes);
    if (!source.s) {
        SetError(error, fmt("%s: cannot read the manifest", path));
        ArenaDelete(scratch);
        return false;
    }
    bool ok = PluginManifestParse(source, out, error);
    if (!ok && error && error->message) {
        Str old = error->message;
        error->message = StrDup(fmt("%s: %s", path, old));
        StrFree(old);
    }
    ArenaDelete(scratch);
    return ok;
}

void PluginManifestSchema(StrBuilder* out) {
    out->Append(StrL(
        "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\","
        "\"title\":\"gpui-shell application manifest\",\"type\":\"object\","
        "\"additionalProperties\":false,\"required\":[\"id\",\"name\","
        "\"entry\"],"
        "\"properties\":{\"id\":{\"type\":\"string\"},\"name\":{\"type\":"
        "\"string\"},"
        "\"version\":{\"type\":\"string\"},\"shell-version\":{\"type\":"
        "\"string\"},"
        "\"entry\":{\"type\":\"string\"},\"dependencies\":{\"type\":\"object\"}"
        ","
        "\"capabilities\":{\"type\":\"object\"}}}"));
}

static bool AbsolutePath(Str path) {
    if (StrStartsWithAny(path, "/\\")) return true;
    return path.len >= 3 && path.s[1] == ':' &&
           (path.s[2] == '/' || path.s[2] == '\\');
}

static Str ExpandPath(Str raw, Str plugin, Str data) {
    StrBuilder out;
    for (int i = 0; i < raw.len;) {
        if (i + 12 <= raw.len &&
            StrEq(Str(raw.s + i, 12), StrL("${pluginDir}"))) {
            out.Append(plugin);
            i += 12;
        } else if (i + 10 <= raw.len &&
                   StrEq(Str(raw.s + i, 10), StrL("${dataDir}"))) {
            out.Append(data);
            i += 10;
        } else {
            out.AppendChar(raw.s[i++]);
        }
    }
    Str expanded = out.TakeStr();
    if (AbsolutePath(expanded)) return expanded;
    StrBuilder joined;
    joined.Append(plugin);
    if (plugin && plugin.s[plugin.len - 1] != '/' &&
        plugin.s[plugin.len - 1] != '\\')
        joined.AppendChar(GPUI_OS_WINDOWS ? '\\' : '/');
    joined.Append(expanded);
    StrFree(expanded);
    return joined.TakeStr();
}

Capabilities PluginManifest::Grant(Str pluginDirectory,
                                   Str dataDirectory) const {
    Capabilities result;
    for (int i = 0; i < readRoots.len; i++) {
        Str path = ExpandPath(readRoots[i], pluginDirectory, dataDirectory);
        result.AddReadRoot(path);
        StrFree(path);
    }
    for (int i = 0; i < writeRoots.len; i++) {
        Str path = ExpandPath(writeRoots[i], pluginDirectory, dataDirectory);
        result.AddWriteRoot(path);
        StrFree(path);
    }
    if (executeUnrestricted) {
        result.SetExecute(ExecuteGrant::Unrestricted());
    } else if (execute.len > 0) {
        ExecuteGrant grant = ExecuteGrant::Allowed(execute.els, execute.len);
        result.SetExecute(grant);
    }
    for (int i = 0; i < networkHosts.len; i++)
        result.AddNetworkHost(networkHosts[i]);
    for (int i = 0; i < http.len; i++) {
        PluginHttpGrant* file = http[i];
        HttpRequestGrant grant(file->host);
        grant.Scheme(file->scheme);
        if (file->hasPort) grant.Port(file->port);
        for (int j = 0; j < file->methods.len; j++)
            grant.AddMethod(file->methods[j]);
        for (int j = 0; j < file->paths.len; j++) grant.AddPath(file->paths[j]);
        for (int j = 0; j < file->pathPrefixes.len; j++)
            grant.AddPathPrefix(file->pathPrefixes[j]);
        result.AddHttpRequest(grant);
    }
    return result.Storage(storage)
        .ClipboardRead(clipboardRead)
        .ClipboardWrite(clipboardWrite)
        .Exit(exit);
}

static int ComparePaths(const void* left, const void* right) {
    const Str* a = (const Str*)left;
    const Str* b = (const Str*)right;
    return StrCmp(*a, *b);
}

Str ShellDataHome() {
    const char* explicitHome = getenv("XDG_DATA_HOME");
    if (explicitHome && *explicitHome) return StrDup(Str(explicitHome));
#if GPUI_OS_WINDOWS
    const char* appData = getenv("APPDATA");
    if (appData && *appData) return StrDup(Str(appData));
#endif
    const char* user = getenv(GPUI_OS_WINDOWS ? "USERPROFILE" : "HOME");
    TempStr cwd;
    if (!user || !*user) {
        cwd = AllocStrTemp(kMaxPath - 1);
        cwd.s[0] = 0;
        PlatGetCwd(cwd.s, cwd.len + 1);
        user = cwd.s;
    }
    StrBuilder path;
    path.Append(Str(user));
#if GPUI_OS_MAC
    path.Append(StrL("/Library/Application Support"));
#elif GPUI_OS_WINDOWS
    path.Append(StrL("\\AppData\\Roaming"));
#else
    path.Append(StrL("/.local/share"));
#endif
    return path.TakeStr();
}

Str ShellBundleIdForPath(Str root) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (int i = 0; i < root.len; i++) {
        hash ^= (uint8_t)root.s[i];
        hash *= 0x100000001b3ull;
    }
    int start = root.len;
    while (start > 0 && root.s[start - 1] != '/' && root.s[start - 1] != '\\')
        start--;
    Str name(root.s + start, root.len - start);
    StrBuilder safe;
    bool previousDot = false;
    for (int i = 0; i < name.len; i++) {
        char ch = name.s[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + ('a' - 'A'));
        bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                       ch == '.' || ch == '-' || ch == '_';
        if (!allowed) ch = '-';
        if (ch == '.' && previousDot) ch = '-';
        safe.AppendChar(ch);
        previousDot = ch == '.';
    }
    while (safe.len > 0 &&
           (safe.els[0] == '.' || safe.els[0] == '-' || safe.els[0] == '_')) {
        memmove(safe.els, safe.els + 1, (size_t)--safe.len);
    }
    while (safe.len > 0 &&
           (safe.els[safe.len - 1] == '.' || safe.els[safe.len - 1] == '-' ||
            safe.els[safe.len - 1] == '_'))
        safe.len--;
    if (safe.len == 0) safe.Append(StrL("app"));
    safe.Append(fmt("-%016llx", (unsigned long long)hash));
    return safe.TakeStr();
}

Str ShellAppDataDirectory(Str id, Arena* arena, ShellError* error) {
    ShellErrorClear(error);
    if (!ValidId(id)) {
        SetError(error, fmt("`%s` is not a usable application identity", id));
        return {};
    }
    Str home = ShellDataHome();
    Str first = Join(arena, home, StrL("gpui-shell"));
    Str second = Join(arena, first, StrL("apps"));
    Str result = Join(arena, second, id);
    StrFree(home);
    return result;
}

PluginManager::PluginManager() : dataHome(ShellDataHome()) {}
PluginManager::PluginManager(Str directory) : PluginManager() {
    AddDirectory(directory);
}

static void FreePlugin(Plugin* plugin, App* app) {
    if (!plugin) return;
    ScriptView* view = plugin->view.Get(app);
    if (view && view->object)
        plugin->runtime->ReleaseApplicationState(view->object);
    if (plugin->view.IsValid()) EntityDrop(app, plugin->view.id);
    delete plugin->assets;
    PolicyClearHostModules(plugin->policy);
    PolicyRelease(plugin->policy);
    plugin->runtime->Release();
    StrFree(plugin->root);
    StrFree(plugin->dataDirectory);
    StrFree(plugin->storePath);
    delete plugin;
}

PluginManager::~PluginManager() {
    for (int i = 0; i < loaded.len; i++) FreePlugin(loaded[i], loaded[i]->app);
    VecReset(loaded);
    ClearCatalog();
    for (int i = 0; i < directories.len; i++) StrFree(directories[i]);
    VecReset(directories);
    StrFree(dataHome);
}

PluginManager& PluginManager::AddDirectory(Str directory) {
    VecAppend(directories, StrDup(directory));
    return *this;
}

PluginManager& PluginManager::DataHome(Str directory) {
    StrFree(dataHome);
    dataHome = StrDup(directory);
    return *this;
}

void PluginManager::ClearCatalog() {
    for (int i = 0; i < catalog.len; i++) {
        delete catalog[i].manifest;
        StrFree(catalog[i].root);
        StrFree(catalog[i].error);
    }
    VecReset(catalog);
}

static bool ManifestAt(Str root) {
    Arena* arena = ArenaNew();
    Str manifest = Join(arena, root, Str(kShellManifestFile));
    bool found = false;
    if (manifest.len < kMaxPath) {
        TempStr path = StrDupTemp(manifest);
        found = PlatFileExists(path.s);
    }
    ArenaDelete(arena);
    return found;
}

const Vec<PluginDiscovery>& PluginManager::Discover() {
    ClearCatalog();
    discovered = true;
    Vec<Str> roots;
    for (int d = 0; d < directories.len; d++) {
        if (ManifestAt(directories[d])) {
            VecAppend(roots, StrDup(directories[d]));
            continue;
        }
        if (directories[d].len >= kMaxPath) continue;
        TempStr directory = StrDupTemp(directories[d]);
        DirEntry* entries = AllocArray<DirEntry>(4096);
        int count = entries ? PlatListDir(directory.s, entries, 4096) : 0;
        Vec<Str> directoryRoots;
        for (int i = 0; i < count; i++) {
            if (!entries[i].isDir || entries[i].isSymlink) continue;
            Arena* scratch = ArenaNew();
            Str root = Join(scratch, directories[d], Str(entries[i].name));
            if (ManifestAt(root)) VecAppend(directoryRoots, StrDup(root));
            ArenaDelete(scratch);
        }
        free(entries);
        if (directoryRoots.len > 1)
            qsort(directoryRoots.els, (size_t)directoryRoots.len, sizeof(Str),
                  ComparePaths);
        for (int i = 0; i < directoryRoots.len; i++) {
            VecAppend(roots, directoryRoots[i]);
            directoryRoots[i] = {};
        }
        VecReset(directoryRoots);
    }
    for (int i = 0; i < roots.len; i++) {
        PluginDiscovery found;
        found.root = roots[i];
        roots[i] = {};
        auto* manifest = new PluginManifest();
        ShellError error = {};
        if (!PluginManifestRead(found.root, manifest, &error)) {
            found.error = error.message;
            error.message = {};
            delete manifest;
        } else {
            for (int j = 0; j < catalog.len; j++) {
                if (catalog[j].manifest &&
                    StrEq(catalog[j].manifest->id, manifest->id)) {
                    found.error = StrDup(fmt("`%s` is already provided by %s",
                                             manifest->id, catalog[j].root));
                    delete manifest;
                    manifest = nullptr;
                    break;
                }
            }
            found.manifest = manifest;
        }
        VecAppend(catalog, found);
        ShellErrorClear(&error);
    }
    VecReset(roots);
    return catalog;
}

Str PluginManager::DataDirectory(Str id, Arena* arena) const {
    Str first = Join(arena, dataHome, StrL("gpui-shell"));
    Str second = Join(arena, first, StrL("plugins"));
    return Join(arena, second, id);
}

bool PluginManager::Load(ShellRuntime* runtime, Str id,
                         PluginAuthorizeFn authorize, void* authorizeData,
                         Window* window, App* app, ShellError* error) {
    ShellErrorClear(error);
    if (!discovered) {
        SetError(error,
                 StrL("plugin discovery has not run; call Discover first"));
        return false;
    }
    if (Loaded(id)) {
        SetError(error, fmt("plugin `%s` is already loaded", id));
        return false;
    }
    const PluginDiscovery* selected = nullptr;
    for (int i = 0; i < catalog.len; i++)
        if (catalog[i].manifest && StrEq(catalog[i].manifest->id, id))
            selected = &catalog[i];
    if (!selected) {
        SetError(error, fmt("no plugin `%s`", id));
        return false;
    }
    if (authorize && !authorize(selected->manifest, authorizeData)) {
        SetError(error,
                 fmt("capabilities for plugin `%s` were not approved", id));
        return false;
    }
    Arena* scratch = ArenaNew();
    Str data = DataDirectory(id, scratch);
    FsResult mkdirResult;
    Str mkdirError;
    if (dataHome) {
        Str relative = Join(scratch, StrL("gpui-shell/plugins"), id);
        FsRun(FsOperation::MakeDirectory, dataHome, relative, {}, true,
              &mkdirResult, &mkdirError);
        mkdirResult.Free();
        StrFree(mkdirError);
    }
    Capabilities capabilities = selected->manifest->Grant(selected->root, data);
    Policy* policy = PolicyNew(capabilities);
    // The manifest id is already unique among loaded plugins, which is exactly
    // what a dock layout needs to keep two plugins' panels of the same name
    // apart.
    PolicySetApplication(policy, id);
    Str store = Join(scratch, data, StrL("store.json"));
    if (capabilities.HasStorage()) {
        Str storageError;
        if (!PolicySetStoragePath(policy, store, &storageError)) {
            log(fmt("storage is unavailable for `%s`: %s", id, storageError));
            StrFree(storageError);
        }
    }
    ViewType* type = runtime->LoadApp(selected->root, selected->manifest->entry,
                                      policy, error);
    if (!type) {
        PolicyRelease(policy);
        ArenaDelete(scratch);
        return false;
    }
    Entity<ScriptView> view = ScriptView::New(app, runtime, type, policy);
    ViewTypeRelease(type);
    ScriptView* state = view.Get(app);
    state->object =
        runtime->Instantiate(state->type, window, app, policy, error, view.id);
    if (!state->object) {
        EntityDrop(app, view.id);
        PolicyRelease(policy);
        ArenaDelete(scratch);
        return false;
    }
    auto* plugin = new Plugin();
    plugin->manifest = selected->manifest;
    plugin->root = StrDup(selected->root);
    plugin->dataDirectory = StrDup(data);
    plugin->storePath = StrDup(store);
    plugin->policy = policy;
    plugin->runtime = runtime->Retain();
    plugin->view = view;
    plugin->assets = new AppAssets(selected->root);
    plugin->assets->Install();
    plugin->app = app;
    VecAppend(loaded, plugin);
    ArenaDelete(scratch);
    return true;
}

bool PluginManager::Unload(Str id, App* app) {
    for (int i = 0; i < loaded.len; i++) {
        if (!loaded[i]->manifest || !StrEq(loaded[i]->manifest->id, id))
            continue;
        Plugin* plugin = loaded[i];
        for (int j = i + 1; j < loaded.len; j++) loaded[j - 1] = loaded[j];
        loaded.len--;
        FreePlugin(plugin, app);
        return true;
    }
    return false;
}

const Plugin* PluginManager::Loaded(Str id) const {
    for (int i = 0; i < loaded.len; i++)
        if (loaded[i]->manifest && StrEq(loaded[i]->manifest->id, id))
            return loaded[i];
    return nullptr;
}

Entity<ShellRoot> ShellLoadApplication(ShellRuntime* runtime, Str directory,
                                       Window* window, App* app, Policy* policy,
                                       ShellError* error, Str* resolvedEntry) {
    ShellErrorClear(error);
    Str entry = StrL("main.js");
    PluginManifest manifest;
    if (ManifestAt(directory)) {
        if (!PluginManifestRead(directory, &manifest, error)) return {};
        entry = manifest.entry;
    }
    if (resolvedEntry) *resolvedEntry = StrDup(entry);
    Policy* authority = policy ? PolicyRetain(policy) : PolicyDefault();
    ViewType* type = runtime->LoadApp(directory, entry, authority, error);
    if (!type) {
        PolicyRelease(authority);
        return {};
    }
    Entity<ScriptView> view = ScriptView::New(app, runtime, type, authority);
    ViewTypeRelease(type);
    ScriptView* state = view.Get(app);
    state->object = runtime->Instantiate(state->type, window, app, authority,
                                         error, view.id);
    PolicyRelease(authority);
    if (!state->object) {
        EntityDrop(app, view.id);
        return {};
    }
    return ShellRoot::New(app, view.id);
}

Str ShellCheckApplication(Arena* arena, ShellRuntime* runtime, Str directory,
                          Window* window, App* app, Policy* policy,
                          ShellError* error) {
    Entity<ShellRoot> root =
        ShellLoadApplication(runtime, directory, window, app, policy, error);
    if (!root.IsValid()) return {};
    ShellRoot* shellRoot = root.Get(app);
    ScriptView* view = shellRoot && shellRoot->content.IsValid()
                           ? Entity<ScriptView>{shellRoot->content}.Get(app)
                           : nullptr;
    Str result = view && view->object
                     ? runtime->RenderToSpec(arena, view->object, window, app,
                                             view->self, view->policy, error)
                     : Str{};
    EntityDrop(app, root.id);
    return result;
}

} // namespace gpui::shell
