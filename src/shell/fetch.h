#ifndef GPUI_SHELL_FETCH_H_
#define GPUI_SHELL_FETCH_H_

#include "shell/capability.h"
#include "sys/http.h"

namespace gpui::shell {

constexpr int kFetchMaxBody = 8 * 1024 * 1024;
constexpr int kFetchMaxRedirects = 10;

struct FetchResult {
    int status = 0;
    Str url;
    Str body;
    Str error;

    void Free();
};

// `parse_method`: a token check, not a list. Which method may reach which
// host on which path is `Capabilities::MayRequest`'s decision and it already
// takes the method; a second list here would be a second policy to keep in
// step with the first, and the way that goes wrong is a grant that cannot be
// exercised. What is refused here is a string that is not an HTTP method at
// all — an empty field, a space, a quote.
bool FetchIsHttpMethod(Str method);

// `prohibited_header`: the ones the client owns. A script that sets its own
// Content-Length or Transfer-Encoding is describing a message the client is
// about to describe differently, and Host picks the connection.
bool FetchHeaderIsProhibited(Str name);

// One request header, owned by the request that carries it.
struct FetchHeader {
    Str name;
    Str value;
};

struct FetchRequest {
    Str url;
    // Empty means GET.
    Str method;
    Vec<FetchHeader> headers;
    Str body;
    // One deadline over the whole redirect walk, as a TimeNow() reading; 0
    // means none. FetchSendAsync fails a hop that lands past it rather than
    // following or answering it, so the walk as a whole ends there however
    // many hops it took, each also bounded by the transport's own limit.
    double deadline = 0;

    void Free();
};

constexpr int kFetchMaxRequestBody = 8 * 1024 * 1024;

// Whether the capability policy grants this method to this URL.
bool FetchAuthorize(Str url, Str method, const Capabilities& capabilities,
                    Str* error = nullptr);

// Sends `request`, following redirects only where the policy allows it.
bool FetchSend(const FetchRequest& request, const Capabilities& capabilities,
               FetchResult* out);

struct FetchAsyncResult {
    bool ok = false;
    // Borrowed for the callback. Move or copy the fields before returning.
    FetchResult* result = nullptr;
};

// The same policy-carrying redirect walk as FetchSend, one asynchronous
// HttpSendAsync at a time. `request` and `capabilities` are copied before
// returning. False means it could not start and no callback will arrive.
bool FetchSendAsync(const FetchRequest& request,
                    const Capabilities& capabilities,
                    Func1<FetchAsyncResult> done);

// ─── the redirect rules, exposed because they are what the tests pin ──────

// `follows_location`: 301, 302, 303, 307 and 308, and nothing else in 3xx.
bool FetchFollowsLocation(int status);

// `rewrite_redirect_request`: a 301 or 302 answering a POST, and a 303
// answering anything but HEAD, continue as a GET with no body — so the entity
// and the headers describing it do not follow.
void FetchRewriteRedirect(int status, Str* method, Vec<FetchHeader>* headers,
                          Str* body);

// `same_origin`: scheme, host and *effective* port, so https://h and
// https://h:443 are one origin and http://h is not.
bool FetchSameOrigin(Str left, Str right);

// `authorize_redirect`. Beyond the target needing its own grant: an HTTPS
// request never continues onto plaintext, a method that is not GET never
// replays its body across origins, and neither Authorization nor any other
// caller-supplied header follows a redirect off its origin.
bool FetchAuthorizeRedirect(const Capabilities& capabilities, Str method,
                            Str current, Str next,
                            const Vec<FetchHeader>& headers, Str* error);

using FetchHttpSend = bool (*)(const HttpReq& req, HttpRsp* out);
void FetchSetHttpSendForTests(FetchHttpSend send);

} // namespace gpui::shell
#endif // GPUI_SHELL_FETCH_H_
