#include "shell/capability.h"

namespace gpui {

bool IsOpenableUrl(Str url) {
    if (!url || len(url) > 32768) return false;
    int schemeEnd = StrFind(url, StrL("://"));
    if (schemeEnd <= 0) return false;
    Str scheme(url.s, schemeEnd);
    if (!StrEqI(scheme, StrL("http")) && !StrEqI(scheme, StrL("https"))) {
        return false;
    }
    int authorityStart = schemeEnd + 3;
    int authorityEnd = authorityStart;
    while (authorityEnd < len(url) && url.s[authorityEnd] != '/' &&
           url.s[authorityEnd] != '?' && url.s[authorityEnd] != '#') {
        unsigned char c = (unsigned char)url.s[authorityEnd];
        if (c <= 0x20 || c >= 0x7f || c == '\\') return false;
        authorityEnd++;
    }
    int hostStart = authorityStart;
    for (int i = authorityStart; i < authorityEnd; i++) {
        if (url.s[i] == '@') hostStart = i + 1;
    }
    if (hostStart >= authorityEnd) return false;
    int portStart = -1;
    if (url.s[hostStart] == '[') {
        int close = hostStart + 1;
        while (close < authorityEnd && url.s[close] != ']') close++;
        if (close <= hostStart + 1 || close >= authorityEnd) return false;
        int afterHost = close + 1;
        if (afterHost == authorityEnd) return true;
        if (url.s[afterHost] != ':' || afterHost + 1 == authorityEnd)
            return false;
        portStart = afterHost + 1;
    } else {
        int hostBegin = hostStart;
        int hostEnd = authorityEnd;
        for (int i = hostStart; i < authorityEnd; i++) {
            if (url.s[i] == ':') {
                hostEnd = i;
                portStart = i + 1;
                break;
            }
        }
        if (hostEnd <= hostBegin) return false;
        if (portStart < 0) return true;
        if (portStart == authorityEnd) return false;
    }
    uint32_t port = 0;
    for (int i = portStart; i < authorityEnd; i++) {
        if (url.s[i] < '0' || url.s[i] > '9') return false;
        port = port * 10 + (uint32_t)(url.s[i] - '0');
        if (port > 65535) return false;
    }
    return true;
}

static void FreeStrings(Vec<Str>* values) {
    for (int i = 0; i < values->len; i++) {
        StrFree((*values)[i]);
    }
    VecReset(*values);
}

static void CopyStrings(Vec<Str>* into, const Vec<Str>& values) {
    for (int i = 0; i < len(values); i++) {
        VecAppend(*into, StrDup(values[i]));
    }
}

static Str Lower(Str value) {
    Str out = StrDup(value);
    StrLowerAscii(out.s);
    return out;
}

static Str Upper(Str value) {
    Str out = StrDup(value);
    for (int i = 0; i < len(out); i++) {
        char c = out.s[i];
        if (c >= 'a' && c <= 'z') out.s[i] = (char)(c - 'a' + 'A');
    }
    return out;
}

ExecuteGrant::ExecuteGrant() = default;

ExecuteGrant::ExecuteGrant(const ExecuteGrant& other) {
    kind = other.kind;
    CopyStrings(&commands, other.commands);
}

ExecuteGrant& ExecuteGrant::operator=(const ExecuteGrant& other) {
    if (this != &other) {
        FreeStrings(&commands);
        kind = other.kind;
        CopyStrings(&commands, other.commands);
    }
    return *this;
}

ExecuteGrant::~ExecuteGrant() {
    FreeStrings(&commands);
}

ExecuteGrant ExecuteGrant::Denied() {
    return {};
}

ExecuteGrant ExecuteGrant::Allowed(const Str* names, int count) {
    ExecuteGrant out;
    out.kind = ExecuteGrantKind::Allowed;
    for (int i = 0; names && i < count; i++)
        VecAppend(out.commands, StrDup(names[i]));
    return out;
}

ExecuteGrant ExecuteGrant::Unrestricted() {
    ExecuteGrant out;
    out.kind = ExecuteGrantKind::Unrestricted;
    return out;
}

bool ExecuteGrant::Allows(Str command) const {
    if (kind == ExecuteGrantKind::Unrestricted) return true;
    if (kind == ExecuteGrantKind::Denied) return false;
    for (int i = 0; i < commands.len; i++) {
        if (StrEq(commands[i], command)) return true;
    }
    return false;
}

HttpRequestGrant::HttpRequestGrant() : scheme(StrDup(StrL("https"))) {}

HttpRequestGrant::HttpRequestGrant(Str value)
    : scheme(StrDup(StrL("https"))), host(Lower(value)) {}

HttpRequestGrant::HttpRequestGrant(const HttpRequestGrant& other) {
    scheme = StrDup(other.scheme);
    host = StrDup(other.host);
    port = other.port;
    hasPort = other.hasPort;
    CopyStrings(&methods, other.methods);
    CopyStrings(&paths, other.paths);
    CopyStrings(&pathPrefixes, other.pathPrefixes);
}

HttpRequestGrant& HttpRequestGrant::operator=(const HttpRequestGrant& other) {
    if (this == &other) return *this;
    StrFree(scheme);
    StrFree(host);
    FreeStrings(&methods);
    FreeStrings(&paths);
    FreeStrings(&pathPrefixes);
    scheme = StrDup(other.scheme);
    host = StrDup(other.host);
    port = other.port;
    hasPort = other.hasPort;
    CopyStrings(&methods, other.methods);
    CopyStrings(&paths, other.paths);
    CopyStrings(&pathPrefixes, other.pathPrefixes);
    return *this;
}

HttpRequestGrant::~HttpRequestGrant() {
    StrFree(scheme);
    StrFree(host);
    FreeStrings(&methods);
    FreeStrings(&paths);
    FreeStrings(&pathPrefixes);
}

HttpRequestGrant& HttpRequestGrant::Scheme(Str value) {
    StrFree(scheme);
    scheme = Lower(value);
    return *this;
}

HttpRequestGrant& HttpRequestGrant::Port(uint16_t value) {
    port = value;
    hasPort = true;
    return *this;
}

HttpRequestGrant& HttpRequestGrant::AddMethod(Str value) {
    VecAppend(methods, Upper(value));
    return *this;
}

HttpRequestGrant& HttpRequestGrant::AddPath(Str value) {
    VecAppend(paths, StrDup(value));
    return *this;
}

HttpRequestGrant& HttpRequestGrant::AddPathPrefix(Str value) {
    VecAppend(pathPrefixes, StrDup(value));
    return *this;
}

static uint16_t EffectivePort(Str scheme, uint16_t port, bool hasPort) {
    if (hasPort) return port;
    if (StrEqI(scheme, StrL("http"))) return 80;
    if (StrEqI(scheme, StrL("https"))) return 443;
    return 0;
}

static bool PrefixAllows(Str prefix, Str path) {
    if (StrEq(prefix, path)) return true;
    if (!StrStartsWith(path, prefix) || len(path) <= len(prefix)) return false;
    return prefix.s[len(prefix) - 1] == '/' || path.s[len(prefix)] == '/';
}

bool HttpRequestGrant::Allows(Str requestScheme, Str requestHost,
                              uint16_t requestPort, bool requestHasPort,
                              Str method, Str path) const {
    if (!StrEqI(scheme, requestScheme) || !StrEqI(host, requestHost) ||
        EffectivePort(scheme, port, hasPort) !=
            EffectivePort(requestScheme, requestPort, requestHasPort)) {
        return false;
    }
    bool methodAllowed = false;
    for (int i = 0; i < methods.len; i++) {
        if (StrEqI(methods[i], method)) methodAllowed = true;
    }
    if (!methodAllowed) return false;
    for (int i = 0; i < paths.len; i++) {
        if (StrEq(paths[i], path)) return true;
    }
    for (int i = 0; i < pathPrefixes.len; i++) {
        if (PrefixAllows(pathPrefixes[i], path)) return true;
    }
    return false;
}

void CapabilityErrorFree(CapabilityError* error) {
    if (!error) return;
    StrFree(error->subject);
    *error = {};
}

static const char* AccessName(CapabilityAccess access) {
    return access == CapabilityAccess::Read ? "read" : "write";
}

Str CapabilityErrorMessage(Arena* arena, const CapabilityError& error) {
    const char* access = AccessName(error.access);
    switch (error.kind) {
        case CapabilityErrorKind::None:
            return {};
        case CapabilityErrorKind::NotGranted:
            return StrDup(arena, fmt("filesystem %s is not granted; declare "
                                     "capabilities.fs.%s in the manifest",
                                     Str(access), Str(access)));
        case CapabilityErrorKind::OutsideRoots:
            return StrDup(arena, fmt("`%s` is outside every granted %s root",
                                     error.subject, Str(access)));
        case CapabilityErrorKind::ExecuteDenied:
            return StrDup(arena, fmt("running `%s` is not granted; add it to "
                                     "capabilities.fs.execute in the manifest",
                                     error.subject));
        case CapabilityErrorKind::StorageDenied:
            return StrDup(arena, StrL("storage is not granted; set "
                                      "capabilities.storage to true"));
    }
    return {};
}

void CapabilityPath::Free() {
    StrFree(root);
    StrFree(relative);
    root = {};
    relative = {};
}

Capabilities::Capabilities() = default;

Capabilities::Capabilities(const Capabilities& other) {
    CopyFrom(other);
}

Capabilities& Capabilities::operator=(const Capabilities& other) {
    if (this != &other) {
        Clear();
        CopyFrom(other);
    }
    return *this;
}

Capabilities::~Capabilities() {
    Clear();
}

void Capabilities::Clear() {
    FreeStrings(&readRoots);
    FreeStrings(&writeRoots);
    FreeStrings(&networkHosts);
    FreeStrings(&envVariables);
    for (int i = 0; i < httpRequests.len; i++) delete httpRequests[i];
    VecReset(httpRequests);
    execute = ExecuteGrant::Denied();
}

void Capabilities::CopyFrom(const Capabilities& other) {
    CopyStrings(&readRoots, other.readRoots);
    CopyStrings(&writeRoots, other.writeRoots);
    execute = other.execute;
    CopyStrings(&networkHosts, other.networkHosts);
    for (int i = 0; i < other.httpRequests.len; i++) {
        VecAppend(httpRequests, new HttpRequestGrant(*other.httpRequests[i]));
    }
    storage = other.storage;
    clipboardRead = other.clipboardRead;
    clipboardWrite = other.clipboardWrite;
    exit = other.exit;
    CopyStrings(&envVariables, other.envVariables);
}

Capabilities& Capabilities::AddReadRoot(Str root) {
    VecAppend(readRoots, StrDup(root));
    return *this;
}
Capabilities& Capabilities::AddWriteRoot(Str root) {
    VecAppend(writeRoots, StrDup(root));
    return *this;
}
Capabilities& Capabilities::SetExecute(const ExecuteGrant& grant) {
    execute = grant;
    return *this;
}
Capabilities& Capabilities::AddNetworkHost(Str host) {
    VecAppend(networkHosts, Lower(host));
    return *this;
}
Capabilities& Capabilities::AddHttpRequest(const HttpRequestGrant& grant) {
    VecAppend(httpRequests, new HttpRequestGrant(grant));
    return *this;
}
Capabilities& Capabilities::Storage(bool allowed) {
    storage = allowed;
    return *this;
}
Capabilities& Capabilities::ClipboardRead(bool allowed) {
    clipboardRead = allowed;
    return *this;
}
Capabilities& Capabilities::ClipboardWrite(bool allowed) {
    clipboardWrite = allowed;
    return *this;
}
Capabilities& Capabilities::Exit(bool allowed) {
    exit = allowed;
    return *this;
}
Capabilities& Capabilities::AddEnvVariable(Str name) {
    if (name) VecAppend(envVariables, StrDup(name));
    return *this;
}

static bool SameRoot(const Vec<Str>& roots, Str root) {
    for (int i = 0; i < roots.len; i++) {
        if (StrEq(roots[i], root)) return true;
    }
    return false;
}

Capabilities& Capabilities::AddReadRootOnce(Str root) {
    if (!SameRoot(readRoots, root)) AddReadRoot(root);
    return *this;
}
Capabilities& Capabilities::AddWriteRootOnce(Str root) {
    if (!SameRoot(writeRoots, root)) AddWriteRoot(root);
    return *this;
}

// crates/shell/src/default-env, one upper-case name per line.
static const char* const kDefaultEnvNames[] = {
    "HOME",
    "USER",
    "LOGNAME",
    "USERNAME",
    "USERPROFILE",
    "HOMEDRIVE",
    "HOMEPATH",
    "HOSTNAME",
    "COMPUTERNAME",
    "PROCESSOR_ARCHITECTURE",
    "LANG",
    "LANGUAGE",
    "LC_ALL",
    "LC_ADDRESS",
    "LC_COLLATE",
    "LC_CTYPE",
    "LC_IDENTIFICATION",
    "LC_MEASUREMENT",
    "LC_MESSAGES",
    "LC_MONETARY",
    "LC_NAME",
    "LC_NUMERIC",
    "LC_PAPER",
    "LC_TELEPHONE",
    "LC_TIME",
    "TZ",
    "PATH",
    "PATHEXT",
    "SHELL",
    "EDITOR",
    "VISUAL",
    "PAGER",
    "BROWSER",
    "TERM",
    "DISPLAY",
    "WAYLAND_DISPLAY",
    "XDG_CURRENT_DESKTOP",
    "XDG_SESSION_TYPE",
    "DESKTOP_SESSION",
    "HTTP_PROXY",
    "HTTPS_PROXY",
    "ALL_PROXY",
    "NO_PROXY",
    "TMPDIR",
    "TEMP",
    "TMP",
    "XDG_CONFIG_HOME",
    "XDG_DATA_HOME",
    "XDG_CACHE_HOME",
    "XDG_STATE_HOME",
    "XDG_RUNTIME_DIR",
    "XDG_CONFIG_DIRS",
    "XDG_DATA_DIRS",
    "APPDATA",
    "LOCALAPPDATA",
    "PROGRAMDATA",
    "PROGRAMFILES",
    "PROGRAMFILES(X86)",
    "SYSTEMROOT",
    "WINDIR",
};

int ShellDefaultEnvNameCount() {
    return (int)(sizeof(kDefaultEnvNames) / sizeof(kDefaultEnvNames[0]));
}
const char* ShellDefaultEnvName(int index) {
    if (index < 0 || index >= ShellDefaultEnvNameCount()) return nullptr;
    return kDefaultEnvNames[index];
}

static bool EqIgnoreAsciiCase(Str a, Str b) {
    if (len(a) != len(b)) return false;
    for (int i = 0; i < len(a); i++) {
        unsigned char ca = (unsigned char)a.s[i];
        unsigned char cb = (unsigned char)b.s[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

bool Capabilities::MayShowEnvVariable(Str name) const {
    if (!name) return false;
    for (int i = 0; i < ShellDefaultEnvNameCount(); i++) {
        if (EqIgnoreAsciiCase(name, Str(ShellDefaultEnvName(i)))) return true;
    }
    for (int i = 0; i < envVariables.len; i++) {
        if (EqIgnoreAsciiCase(name, envVariables[i])) return true;
    }
    return false;
}
bool Capabilities::HasReadAccess() const {
    return readRoots.len != 0;
}
bool Capabilities::HasWriteAccess() const {
    return writeRoots.len != 0;
}
bool Capabilities::HasStorage() const {
    return storage;
}
bool Capabilities::IsClipboardReadable() const {
    return clipboardRead;
}
bool Capabilities::IsClipboardWritable() const {
    return clipboardWrite;
}
bool Capabilities::MayExit() const {
    return exit;
}
bool Capabilities::MayRun(Str command) const {
    return execute.Allows(command);
}

bool Capabilities::MayReach(Str host) const {
    for (int i = 0; i < networkHosts.len; i++) {
        if (StrEqI(networkHosts[i], host)) return true;
    }
    return false;
}

bool Capabilities::MayRequest(Str scheme, Str host, uint16_t port, bool hasPort,
                              Str method, Str path) const {
    if (MayReach(host)) return true;
    for (int i = 0; i < httpRequests.len; i++) {
        if (httpRequests[i]->Allows(scheme, host, port, hasPort, method, path))
            return true;
    }
    return false;
}

static bool IsSeparator(char c) {
    return c == '/' || c == '\\';
}

static bool IsAbsolute(Str path) {
    if (len(path) == 0) return false;
    if (IsSeparator(path.s[0])) return true;
    if (!base::PlatIsWindows()) return false;
    return len(path) >= 3 &&
           ((path.s[0] >= 'A' && path.s[0] <= 'Z') ||
            (path.s[0] >= 'a' && path.s[0] <= 'z')) &&
           path.s[1] == ':' && IsSeparator(path.s[2]);
}

static Str NormalizePath(Arena* arena, Str path, bool* escaped) {
    if (escaped) *escaped = false;
    StrBuilder out(arena);
    int prefix = 0;
    if (base::PlatIsWindows() && len(path) >= 2 && path.s[1] == ':') {
        char drive = path.s[0];
        if (drive >= 'a' && drive <= 'z') drive = (char)(drive - 'a' + 'A');
        out.AppendChar(drive);
        out.AppendChar(':');
        prefix = 2;
    }
    if (prefix < len(path) && IsSeparator(path.s[prefix])) {
        out.AppendChar('/');
        while (prefix < len(path) && IsSeparator(path.s[prefix])) prefix++;
    }
    Vec<Str> parts;
    int at = prefix;
    while (at <= len(path)) {
        int end = at;
        while (end < len(path) && !IsSeparator(path.s[end])) end++;
        Str part(path.s + at, end - at);
        if (len(part) == 0 || StrEq(part, StrL("."))) {
        } else if (StrEq(part, StrL(".."))) {
            if (len(parts) == 0) {
                if (escaped) *escaped = true;
            } else {
                parts.len--;
            }
        } else {
            VecAppend(parts, part);
        }
        at = end + 1;
    }
    bool rootSlash = out.len > 0 && out.els[out.len - 1] == '/';
    for (int i = 0; i < len(parts); i++) {
        if (out.len > 0 && !(rootSlash && out.len == 1) &&
            out.els[out.len - 1] != '/')
            out.AppendChar('/');
        out.Append(parts[i]);
    }
    Str result = out.TakeStr();
    return len(result) == 0 ? StrDup(arena, StrL(".")) : result;
}

static bool PathSame(Str a, Str b) {
    return base::PlatPathsCaseFold() ? StrEqI(a, b) : StrEq(a, b);
}

static bool PathPrefix(Str root, Str path, Str* relative) {
    if (PathSame(root, path)) {
        *relative = StrL(".");
        return true;
    }
    if (len(path) <= len(root) || path.s[len(root)] != '/') return false;
    Str head(path.s, len(root));
    if (!PathSame(head, root)) return false;
    *relative = Str(path.s + len(root) + 1, len(path) - len(root) - 1);
    return true;
}

bool Capabilities::ResolvePath(Str path, CapabilityAccess access,
                               CapabilityPath* out,
                               CapabilityError* error) const {
    if (out) out->Free();
    if (error) CapabilityErrorFree(error);
    const Vec<Str>& roots =
        access == CapabilityAccess::Read ? readRoots : writeRoots;
    if (len(roots) == 0) {
        if (error) {
            error->kind = CapabilityErrorKind::NotGranted;
            error->access = access;
        }
        return false;
    }

    Arena* arena = GetTempArena();
    bool absolute = IsAbsolute(path);
    bool escaped = false;
    Str normalizedPath = NormalizePath(arena, path, &escaped);
    for (int i = 0; i < len(roots); i++) {
        bool rootEscaped = false;
        Str root = NormalizePath(arena, roots[i], &rootEscaped);
        if (rootEscaped) continue;
        Str relative;
        if (absolute) {
            if (!PathPrefix(root, normalizedPath, &relative)) continue;
        } else {
            if (escaped) continue;
            relative = normalizedPath;
        }
        if (out) {
            out->root = StrDup(root);
            out->relative = StrDup(relative);
        }
        return true;
    }
    if (error) {
        error->kind = CapabilityErrorKind::OutsideRoots;
        error->access = access;
        error->subject = StrDup(path);
    }
    return false;
}

static void AppendJoined(StrBuilder* out, const Vec<Str>& values,
                         const char* separator) {
    for (int i = 0; i < values.len; i++) {
        if (i) out->Append(Str(separator));
        out->Append(values[i]);
    }
}

void Capabilities::AppendSummary(StrBuilder* out) const {
    if (!out) return;
    if (readRoots.len > 0) {
        out->Append(StrL("fs read: "));
        AppendJoined(out, readRoots, ", ");
        out->Append(StrL("\n"));
    }
    if (writeRoots.len > 0) {
        out->Append(StrL("fs write: "));
        AppendJoined(out, writeRoots, ", ");
        out->Append(StrL("\n"));
    }
    if (networkHosts.len > 0) {
        out->Append(StrL("network: "));
        AppendJoined(out, networkHosts, ", ");
        out->Append(StrL("\n"));
    }
    for (int i = 0; i < httpRequests.len; i++) {
        const HttpRequestGrant* request = httpRequests[i];
        if (!request) continue;
        out->Append(StrL("http: "));
        AppendJoined(out, request->methods, "|");
        out->Append(fmt(" %s://%s", request->scheme, request->host));
        if (request->hasPort) out->Append(fmt(":%u", (unsigned)request->port));
        out->Append(StrL("\n"));
    }
    if (execute.kind == ExecuteGrantKind::Allowed) {
        out->Append(StrL("process: "));
        AppendJoined(out, execute.commands, ", ");
        out->Append(StrL("\n"));
    } else if (execute.kind == ExecuteGrantKind::Unrestricted) {
        out->Append(StrL("process: any command\n"));
    }
    if (storage) out->Append(StrL("storage\n"));
    if (clipboardRead && clipboardWrite) {
        out->Append(StrL("clipboard: read, write\n"));
    } else if (clipboardRead) {
        out->Append(StrL("clipboard: read\n"));
    } else if (clipboardWrite) {
        out->Append(StrL("clipboard: write\n"));
    }
    if (envVariables.len > 0) {
        out->Append(StrL("env: "));
        AppendJoined(out, envVariables, ", ");
        out->Append(StrL("\n"));
    }
}

} // namespace gpui
