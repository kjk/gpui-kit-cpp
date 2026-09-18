/* libcurl: the one HTTP client a Linux desktop already has, found the same
   way X11, cairo and Pango are — pkg-config, at build time.

   It is a soft dependency, which the other three are not. A machine without
   `libcurl4-openssl-dev` still builds this tree; it just cannot fetch, and a
   remote image renders as its alt text there the way it did before any of
   this existed. cmd/build-linux.ts defines GPUI_HAVE_CURL when the package
   answers, and cmd/ubuntu-install-deps.sh installs it. */

#include "sys/http.h"

#if defined(GPUI_HAVE_CURL) && GPUI_HAVE_CURL
#include <curl/curl.h>
#endif

namespace gpui {

#if defined(GPUI_HAVE_CURL) && GPUI_HAVE_CURL

// "image/png; charset=..." -> "image/png", lowercased where it stands.
static void TrimMediaType(Str* s) {
    for (int i = 0; i < s->len; i++) {
        char c = s->s[i];
        if (c == ';' || c == ' ') {
            s->len = i;
            break;
        }
        if (c >= 'A' && c <= 'Z') {
            s->s[i] = (char)(c - 'A' + 'a');
        }
    }
}

// Returns short of `n * size` to tell curl to stop, which is how a body over
// the cap is refused rather than truncated.
static size_t OnBody(char* data, size_t size, size_t n, void* userp) {
    HttpRsp* out = (HttpRsp*)userp;
    size_t want = size * n;
    if ((int64_t)len(out->body) + (int64_t)want > (int64_t)kHttpMaxBody) {
        return 0;
    }
    uint8_t* dst = VecAppendBlanks(out->body, (int)want);
    if (!dst) {
        return 0;
    }
    memcpy(dst, data, want);
    return want;
}

bool HttpSend(const HttpReq& req, HttpRsp* out) {
    Str url = req.url;
    bool noRedirect = req.noRedirect;
    if (!out || !HttpUrlIsRemote(url)) {
        return false;
    }
    CURL* c = curl_easy_init();
    if (!c) {
        return false;
    }
    // curl wants a C string and Str is a slice, so the URL is copied once.
    char* u = AllocArray<char>(len(url) + 1);
    if (!u) {
        curl_easy_cleanup(c);
        return false;
    }
    memcpy(u, url.s, (size_t)len(url));
    u[len(url)] = 0;

    curl_easy_setopt(c, CURLOPT_URL, u);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, noRedirect ? 0L : 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)kHttpTimeoutMs);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "gpui/1.0");
    // This runs on a worker thread and curl's alarm-based DNS timeout is not
    // something to install from one.
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    // http and https and nothing else, whatever a redirect asks for. The
    // string form is 7.85 and later; older curl takes the bitmask, which 7.85
    // deprecated rather than removed — and which -Werror will not let us name
    // once it is. Both spellings are enum values rather than macros, so the
    // version is what this can ask about.
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS,
                     (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif

    // The method, and the body that goes with it. CUSTOMREQUEST changes the
    // verb without changing anything else about the transfer, which is what
    // this wants: POSTFIELDS alone would also turn a redirect's method into
    // GET behind curl's back, and the redirect rules are the caller's.
    char* verb = nullptr;
    if (len(req.method) > 0 && !StrEq(req.method, StrL("GET"))) {
        verb = AllocArray<char>(len(req.method) + 1);
        if (!verb) {
            curl_easy_cleanup(c);
            Free(nullptr, u);
            return false;
        }
        memcpy(verb, req.method.s, (size_t)len(req.method));
        verb[len(req.method)] = 0;
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, verb);
    }
    if (len(req.body) > 0) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.s);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)len(req.body));
    }
    struct curl_slist* headers = nullptr;
    bool headersReady = true;
    for (int i = 0; i < req.nHeaders && headersReady; i++) {
        StrBuilder line;
        line.Append(req.headers[i].name);
        line.Append(StrL(": "));
        line.Append(req.headers[i].value);
        Str text = line.TakeStr();
        if (!text.s) {
            headersReady = false;
        } else {
            struct curl_slist* next = curl_slist_append(headers, text.s);
            if (next) {
                headers = next;
            } else {
                headersReady = false;
            }
        }
        StrFree(text);
    }
    if (headers) {
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode err = headersReady ? curl_easy_perform(c) : CURLE_OUT_OF_MEMORY;
    bool ok = err == CURLE_OK;
    if (ok) {
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        out->status = (int)status;
        if (noRedirect && status >= 300 && status < 400) {
            char* redirect = nullptr;
            if (curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &redirect) ==
                    CURLE_OK &&
                redirect) {
                out->redirectUrl = StrDup(Str(redirect));
            }
        }
        char* ct = nullptr;
        if (curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &ct) == CURLE_OK &&
            ct) {
            Str s = StrDup(Str(ct));
            TrimMediaType(&s);
            out->contentType = s;
        }
    }
    curl_easy_cleanup(c);
    if (headers) {
        curl_slist_free_all(headers);
    }
    Free(nullptr, verb);
    Free(nullptr, u);
    if (!ok) {
        VecReset(out->body);
    }
    return ok;
}

bool HttpGet(Str url, HttpRsp* out) {
    HttpReq req;
    req.url = url;
    return HttpSend(req, out);
}

bool HttpGetNoRedirect(Str url, HttpRsp* out) {
    HttpReq req;
    req.url = url;
    req.noRedirect = true;
    return HttpSend(req, out);
}

#else

bool HttpSend(const HttpReq& req, HttpRsp* out) {
    // Built without libcurl: there is no client here to make the request
    // with, so every request fails the way one to an unreachable host would.
    (void)req;
    (void)out;
    return false;
}

bool HttpGet(Str url, HttpRsp* out) {
    // Built without libcurl: there is no client here to make the request
    // with, so every fetch fails the way a request to an unreachable host
    // would and the picture stays its alt text.
    (void)url;
    (void)out;
    return false;
}

bool HttpGetNoRedirect(Str url, HttpRsp* out) {
    (void)url;
    (void)out;
    return false;
}

#endif

} // namespace gpui
