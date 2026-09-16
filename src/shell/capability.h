#ifndef GPUI_SHELL_CAPABILITY_H_
#define GPUI_SHELL_CAPABILITY_H_

#include "base.h"

namespace gpui {

enum class ExecuteGrantKind : uint8_t {
    Denied,
    Allowed,
    Unrestricted,
};

struct ExecuteGrant {
    ExecuteGrantKind kind = ExecuteGrantKind::Denied;
    Vec<Str> commands;

    ExecuteGrant();
    ExecuteGrant(const ExecuteGrant& other);
    ExecuteGrant& operator=(const ExecuteGrant& other);
    ~ExecuteGrant();

    static ExecuteGrant Denied();
    static ExecuteGrant Allowed(const Str* commands, int count);
    static ExecuteGrant Unrestricted();
    bool Allows(Str command) const;
};

struct HttpRequestGrant {
    Str scheme;
    Str host;
    uint16_t port = 0;
    bool hasPort = false;
    Vec<Str> methods;
    Vec<Str> paths;
    Vec<Str> pathPrefixes;

    HttpRequestGrant();
    explicit HttpRequestGrant(Str host);
    HttpRequestGrant(const HttpRequestGrant& other);
    HttpRequestGrant& operator=(const HttpRequestGrant& other);
    ~HttpRequestGrant();

    HttpRequestGrant& Scheme(Str value);
    HttpRequestGrant& Port(uint16_t value);
    HttpRequestGrant& AddMethod(Str value);
    HttpRequestGrant& AddPath(Str value);
    HttpRequestGrant& AddPathPrefix(Str value);
    bool Allows(Str requestScheme, Str requestHost, uint16_t requestPort,
                bool requestHasPort, Str method, Str path) const;
};

enum class CapabilityAccess : uint8_t {
    Read,
    Write,
};

enum class CapabilityErrorKind : uint8_t {
    None,
    NotGranted,
    OutsideRoots,
    ExecuteDenied,
    StorageDenied,
};

struct CapabilityError {
    CapabilityErrorKind kind = CapabilityErrorKind::None;
    CapabilityAccess access = CapabilityAccess::Read;
    Str subject;
};

void CapabilityErrorFree(CapabilityError* error);
Str CapabilityErrorMessage(Arena* arena, const CapabilityError& error);

// One rule for every route out to the system URL opener: Link.href,
// cx.open_url and TextView's default link handling.
bool IsOpenableUrl(Str url);

struct CapabilityPath {
    Str root;
    Str relative;

    void Free();
};

class Capabilities {
  public:
    Capabilities();
    Capabilities(const Capabilities& other);
    Capabilities& operator=(const Capabilities& other);
    ~Capabilities();

    Capabilities& AddReadRoot(Str root);
    Capabilities& AddWriteRoot(Str root);
    Capabilities& SetExecute(const ExecuteGrant& grant);
    Capabilities& AddNetworkHost(Str host);
    Capabilities& AddHttpRequest(const HttpRequestGrant& grant);
    Capabilities& Storage(bool allowed);
    Capabilities& ClipboardRead(bool allowed);
    Capabilities& ClipboardWrite(bool allowed);
    Capabilities& Exit(bool allowed);

    bool HasReadAccess() const;
    bool HasWriteAccess() const;
    bool HasStorage() const;
    bool IsClipboardReadable() const;
    bool IsClipboardWritable() const;
    bool MayExit() const;
    bool MayRun(Str command) const;
    bool MayReach(Str host) const;
    bool MayRequest(Str scheme, Str host, uint16_t port, bool hasPort,
                    Str method, Str path) const;

    // Selects a granted root and returns a path relative to it. The result is
    // authority metadata, not a path to open ambiently: filesystem operations
    // pass both halves to the platform capability opener in one call.
    bool ResolvePath(Str path, CapabilityAccess access, CapabilityPath* out,
                     CapabilityError* error = nullptr) const;

  private:
    Vec<Str> readRoots;
    Vec<Str> writeRoots;
    ExecuteGrant execute;
    Vec<Str> networkHosts;
    Vec<HttpRequestGrant*> httpRequests;
    bool storage = false;
    bool clipboardRead = false;
    bool clipboardWrite = false;
    bool exit = false;

    void Clear();
    void CopyFrom(const Capabilities& other);
};

} // namespace gpui
#endif // GPUI_SHELL_CAPABILITY_H_
