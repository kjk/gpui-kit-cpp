/* Browser HTTP.

   The synchronous surface stays unavailable: blocking the browser thread for
   network I/O freezes the tab, and synchronous XMLHttpRequest is disappearing
   from engines. HttpWasmSendAsync is the real client. It copies a request into
   JavaScript, starts fetch(), and hands the response back to sys/http.cpp on
   the same browser thread.

   An image request lets fetch follow redirects, matching the hosted image
   clients. A shell request sets noRedirect so its capability policy can
   approve every Location before another request starts. Browsers expose some
   manual redirects only as `opaqueredirect`, with neither status nor target;
   those are refused. Following them automatically would contact an
   unauthorized origin before the policy could see it.

   CORS is the browser's security boundary and still applies. The request
   sends no ambient credentials or referrer, matching this tree's clients,
   which have no cookie jar. */

#include "sys/http.h"

#include <emscripten/emscripten.h>

namespace gpui {

struct WasmHttpTransfer {
    Func1<HttpAsyncResult> done;
    HttpRsp response;
};

// clang-format off
EM_JS(int, GpJsHttpBegin,
      (int token, const char* url, int urlLen, const char* method,
       int methodLen, int noRedirect), {
    let G = globalThis.__gpuiHttp;
    if (!G) {
        G = {
            pending: new Map(),
            decoder: new TextDecoder("utf-8"),
            encoder: new TextEncoder()
        };
        G.str = function(ptr, len) {
            return ptr && len > 0
                ? G.decoder.decode(HEAPU8.subarray(ptr, ptr + len))
                : "";
        };
        G.encoded = function(text) {
            if (!text) {
                return { ptr: 0, len: 0 };
            }
            const bytes = G.encoder.encode(text);
            const ptr = _malloc(bytes.length);
            if (!ptr) {
                return { ptr: 0, len: 0 };
            }
            HEAPU8.set(bytes, ptr);
            return { ptr: ptr, len: bytes.length };
        };
        G.finish = function(tokenValue, ok, status, bytes, contentType,
                            redirectUrl) {
            let body = 0;
            let bodyLen = 0;
            const type = G.encoded(contentType);
            const redirect = G.encoded(redirectUrl);
            if (bytes && bytes.length > 0) {
                bodyLen = bytes.length;
                body = _malloc(bodyLen);
                if (body) {
                    HEAPU8.set(bytes, body);
                } else {
                    bodyLen = 0;
                    ok = false;
                }
            }
            _gpui_wasm_http_done(tokenValue, ok ? 1 : 0, status, body,
                                 bodyLen, type.ptr, type.len, redirect.ptr,
                                 redirect.len);
            if (type.ptr) {
                _free(type.ptr);
            }
            if (redirect.ptr) {
                _free(redirect.ptr);
            }
        };
        globalThis.__gpuiHttp = G;
    }
    if (!token || G.pending.has(token)) {
        return 0;
    }
    G.pending.set(token, {
        url: G.str(url, urlLen),
        method: G.str(method, methodLen) || "GET",
        headers: [],
        body: null,
        noRedirect: noRedirect !== 0
    });
    return 1;
});

EM_JS(void, GpJsHttpHeader,
      (int token, const char* name, int nameLen, const char* value,
       int valueLen), {
    const G = globalThis.__gpuiHttp;
    const request = G ? G.pending.get(token) : null;
    if (request) {
        request.headers.push([G.str(name, nameLen), G.str(value, valueLen)]);
    }
});

EM_JS(void, GpJsHttpStart,
      (int token, const uint8_t* body, int bodyLen, int timeoutMs,
       int maxBody), {
    const G = globalThis.__gpuiHttp;
    const request = G ? G.pending.get(token) : null;
    if (!request) {
        return;
    }
    G.pending.delete(token);
    if (body && bodyLen > 0) {
        request.body = new Uint8Array(HEAPU8.subarray(body, body + bodyLen));
    }
    Promise.resolve().then(async function() {
        const controller = new AbortController();
        const timeout = setTimeout(function() {
            controller.abort();
        }, timeoutMs);
        try {
            const headers = new Headers();
            for (const pair of request.headers) {
                headers.append(pair[0], pair[1]);
            }
            const options = {
                method: request.method,
                headers: headers,
                redirect: request.noRedirect ? "manual" : "follow",
                credentials: "omit",
                referrerPolicy: "no-referrer",
                cache: "no-store",
                signal: controller.signal
            };
            if (request.body) {
                options.body = request.body;
            }
            const response = await fetch(request.url, options);
            if (request.noRedirect &&
                (response.type === "opaqueredirect" || response.status === 0)) {
                throw new Error("the browser hid a manual redirect");
            }
            const length = Number(response.headers.get("content-length"));
            if (Number.isFinite(length) && length > maxBody) {
                throw new Error("the response body is too large");
            }
            const chunks = [];
            let total = 0;
            if (response.body && response.body.getReader) {
                const reader = response.body.getReader();
                for (;;) {
                    const part = await reader.read();
                    if (part.done) {
                        break;
                    }
                    total += part.value.byteLength;
                    if (total > maxBody) {
                        await reader.cancel();
                        throw new Error("the response body is too large");
                    }
                    chunks.push(part.value);
                }
            } else {
                const whole = new Uint8Array(await response.arrayBuffer());
                total = whole.byteLength;
                if (total > maxBody) {
                    throw new Error("the response body is too large");
                }
                chunks.push(whole);
            }
            const bytes = new Uint8Array(total);
            let at = 0;
            for (const chunk of chunks) {
                bytes.set(chunk, at);
                at += chunk.byteLength;
            }
            let redirectUrl = "";
            if (request.noRedirect && response.status >= 300 &&
                response.status < 400) {
                const location = response.headers.get("location");
                if (location) {
                    redirectUrl = new URL(location, response.url).href;
                }
            }
            G.finish(token, true, response.status, bytes,
                     response.headers.get("content-type") || "", redirectUrl);
        } catch (error) {
            G.finish(token, false, 0, null, "", "");
        } finally {
            clearTimeout(timeout);
        }
    });
});
// clang-format on

extern "C" EMSCRIPTEN_KEEPALIVE void gpui_wasm_http_done(
    int token, int ok, int status, uint8_t* body, int bodyLen,
    const char* contentType, int contentTypeLen, const char* redirectUrl,
    int redirectUrlLen) {
    WasmHttpTransfer* transfer = (WasmHttpTransfer*)(intptr_t)token;
    if (!transfer) {
        Free(nullptr, body);
        return;
    }
    if (bodyLen < 0 || bodyLen > kHttpMaxBody) {
        Free(nullptr, body);
        body = nullptr;
        bodyLen = 0;
        ok = 0;
    }
    transfer->response.status = status;
    transfer->response.body.els = body;
    transfer->response.body.len = bodyLen;
    transfer->response.body.cap = bodyLen;
    if (contentType && contentTypeLen > 0) {
        transfer->response
            .contentType = StrDup(Str(contentType, contentTypeLen));
    }
    if (redirectUrl && redirectUrlLen > 0) {
        transfer->response
            .redirectUrl = StrDup(Str(redirectUrl, redirectUrlLen));
    }
    HttpAsyncResult result = {ok != 0, &transfer->response};
    transfer->done.Call(result);
    HttpRspFree(&transfer->response);
    delete transfer;
}

bool HttpWasmSendAsync(const HttpReq& req, Func1<HttpAsyncResult> done) {
    WasmHttpTransfer* transfer = new WasmHttpTransfer();
    transfer->done = done;
    int token = (int)(intptr_t)transfer;
    if (!GpJsHttpBegin(token, req.url.s, len(req.url), req.method.s,
                       len(req.method), req.noRedirect ? 1 : 0)) {
        delete transfer;
        return false;
    }
    for (int i = 0; i < req.nHeaders; i++) {
        GpJsHttpHeader(token, req.headers[i].name.s, len(req.headers[i].name),
                       req.headers[i].value.s, len(req.headers[i].value));
    }
    GpJsHttpStart(token, (const uint8_t*)req.body.s, len(req.body),
                  kHttpTimeoutMs, kHttpMaxBody);
    return true;
}

bool HttpSend(const HttpReq& req, HttpRsp* out) {
    (void)req;
    (void)out;
    return false;
}

bool HttpGet(Str url, HttpRsp* out) {
    (void)url;
    (void)out;
    return false;
}

bool HttpGetNoRedirect(Str url, HttpRsp* out) {
    (void)url;
    (void)out;
    return false;
}

} // namespace gpui
