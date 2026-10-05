/* The portable half of the port: wry/src/webview2/mod.rs's custom protocol
 * URI work-around, which is string work and nothing else.
 *
 * Part of the C++ port of lb-wry 0.53.3 (see src/wry/readme.md).
 *
 * It lives out here rather than in wry_win.cpp because the crate's own unit
 * test — `checks_if_custom_protocol_uri` — is over these three functions, and
 * tests/WryTests.cpp is one binary on every platform. The string and number
 * work of webkitgtk/ (the proxy URI, dropped-file paths, download names, the
 * screen scale, the synthetic back/forward mouse events) is here for the same
 * reason: it is what can be tested without a display or WebKitGTK.
 */

#include "wry/wry.h"

namespace wry {

// wkwebview/mod.rs cookies_for_url intentionally tests exact domain only:
// no subdomain or path matching, unlike the native Windows cookie manager.
bool MacCookieMatchesUrl(const Cookie* cookie, Str scheme, Str domain) {
    if (!cookie || !domain.s || !base::StrEq(cookie->domain, domain))
        return false;
    if (!cookie->hasSecure || !cookie->secure) return true;
    return base::StrEq(scheme, StrL("https")) ||
           (base::StrEq(scheme, StrL("http")) &&
            base::StrEq(domain, StrL("localhost")));
}

// wkwebview/download.rs splits at the first dot, preserving compound
// extensions.
Str MacDownloadFileNameTemp(Str suggested, int collision) {
    if (collision <= 0) return suggested;
    int dot = base::StrFind(suggested, ".");
    Str stem = dot < 0 ? suggested : Str(suggested.s, dot);
    Str extension =
        dot < 0 ? Str() : Str(suggested.s + dot, len(suggested) - dot);
    return base::FormatTemp("%s (%d)%s", stem, collision, extension);
}

void CookieListFree(Vec<Cookie>* cookies) {
    if (!cookies) {
        return;
    }
    for (int i = 0; i < cookies->len; i++) {
        Cookie& cookie = cookies->els[i];
        base::StrFree(cookie.name);
        base::StrFree(cookie.value);
        base::StrFree(cookie.domain);
        base::StrFree(cookie.path);
    }
    VecReset(*cookies);
}

Str WorkAroundUriPrefix(Str httpOrHttps, Str protocol) {
    return base::FormatTemp("%s://%s.", httpOrHttps, protocol);
}

bool IsWorkAroundUri(Str uri, Str httpOrHttps, Str protocol) {
    return base::StrStartsWith(uri, WorkAroundUriPrefix(httpOrHttps, protocol));
}

Str ApplyUriWorkAround(Str uri, Str httpOrHttps, Str protocol) {
    return base::StrReplaceAll(uri, base::FormatTemp("%s://", protocol),
                               WorkAroundUriPrefix(httpOrHttps, protocol));
}

Str RevertUriWorkAround(Str uri, Str httpOrHttps, Str protocol) {
    return base::StrReplaceAll(uri, WorkAroundUriPrefix(httpOrHttps, protocol),
                               base::FormatTemp("%s://", protocol));
}

// ─── webkitgtk/ ──────────────────────────────────────────────────────────

Str ProxyUriTemp(const ProxyConfig* proxy) {
    if (!proxy || proxy->kind == ProxyKind::None) {
        return {};
    }
    const char* scheme = proxy->kind == ProxyKind::Socks5 ? "socks5" : "http";
    return base::FormatTemp("%s://%s:%s", Str(scheme), proxy->host,
                            proxy->port);
}

static int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// `percent_encoding::percent_decode(..).decode_utf8_lossy()`, without the
// lossy part: a Linux path is bytes, so a sequence that is not UTF-8 is kept
// as it decoded rather than turned into U+FFFD.
Str PathFromFileUriTemp(Str uri) {
    Str path = uri;
    if (base::StrStartsWith(path, "file://")) {
        path = Str(path.s + 7, len(path) - 7);
    }
    Str res = base::AllocStrTemp(len(path) + 1);
    if (!res.s) {
        return {};
    }
    int n = 0;
    for (int i = 0; i < len(path); i++) {
        char c = path.s[i];
        if (c == '%' && i + 2 < len(path)) {
            int hi = HexDigit(path.s[i + 1]);
            int lo = HexDigit(path.s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                res.s[n++] = (char)(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        res.s[n++] = c;
    }
    res.s[n] = 0;
    res.len = n;
    return res;
}

void DownloadFileNameParts(Str uri, Str suggested, Str* stem, Str* ext) {
    // `suggested_filename.split_once('.')`.
    Str name = suggested;
    Str dotExt;
    for (int i = 0; i < len(suggested); i++) {
        if (suggested.s[i] == '.') {
            name = Str(suggested.s, i);
            dotExt = Str(suggested.s + i, len(suggested) - i);
            break;
        }
    }
    // WebKitGTK suggests the payload of a nameless `data:` download as its
    // name: `data:attachment/text,sometext` comes back as `text,sometext`.
    if (base::StrStartsWith(uri, "data:")) {
        int slash = -1;
        for (int i = 0; i < len(uri); i++) {
            if (uri.s[i] == '/') {
                slash = i;
                break;
            }
        }
        if (slash >= 0) {
            Str rest = Str(uri.s + slash + 1, len(uri) - slash - 1);
            for (int i = 0; i < len(rest); i++) {
                if (rest.s[i] == ',') {
                    Str prefix = Str(rest.s, i + 1); // "text," with the comma
                    if (base::StrStartsWith(name, prefix)) {
                        name = StrL("Unknown");
                    }
                    break;
                }
            }
        }
    }
    *stem = base::StrDupTemp(name);
    *ext = base::StrDupTemp(dotExt);
}

double ScaleFactorFromScreen(int widthPx, int widthMm) {
    if (widthPx <= 0 || widthMm <= 0) {
        return 1.0;
    }
    return ((double)widthPx * 25.4 / (double)widthMm) / 96.0;
}

Str SyntheticMouseEventJsTemp(const SyntheticMouseEvent* ev) {
    base::StrBuilder b(base::GetTempArena());
    Str name = ev->pressed ? StrL("mousedown") : StrL("mouseup");
    Str x = base::FormatTemp("%d", ev->x);
    Str y = base::FormatTemp("%d", ev->y);
    auto boolStr = [](bool v) { return v ? StrL("true") : StrL("false"); };
    auto add = [&b](Str s) { b.Append(s); };
    add(StrL("(() => {\n        const el = document.elementFromPoint("));
    add(x);
    add(StrL(","));
    add(y);
    add(StrL(");\n        const ev = new MouseEvent('"));
    add(name);
    add(StrL("', {\n          view: window,\n          button: "));
    add(base::FormatTemp("%d", ev->button == 8 ? 3 : 4));
    add(StrL(",\n          buttons: "));
    add(base::FormatTemp("%d", ev->buttons));
    add(StrL(",\n          x: "));
    add(x);
    add(StrL(",\n          y: "));
    add(y);
    add(StrL(",\n          bubbles: true,\n          detail: "));
    add(base::FormatTemp("%d", ev->detail));
    add(
        StrL(",\n          cancelBubble: false,\n          cancelable: true,\n"
             "          clientX: "));
    add(x);
    add(StrL(",\n          clientY: "));
    add(y);
    add(StrL(",\n          composed: true,\n          layerX: "));
    add(x);
    add(StrL(",\n          layerY: "));
    add(y);
    add(StrL(",\n          pageX: "));
    add(x);
    add(StrL(",\n          pageY: "));
    add(y);
    add(StrL(",\n          screenX: window.screenX + "));
    add(x);
    add(StrL(",\n          screenY: window.screenY + "));
    add(y);
    add(StrL(",\n          ctrlKey: "));
    add(boolStr(ev->ctrlKey));
    add(StrL(",\n          metaKey: "));
    add(boolStr(ev->metaKey));
    add(StrL(",\n          shiftKey: "));
    add(boolStr(ev->shiftKey));
    add(StrL(",\n          altKey: "));
    add(boolStr(ev->altKey));
    add(
        StrL(",\n        });\n        el.dispatchEvent(ev)\n"
             "        if (!ev.defaultPrevented && \""));
    add(name);
    add(
        StrL("\" === \"mouseup\") {\n          if (ev.button === 3) {\n"
             "            window.history.back();\n          }\n"
             "          if (ev.button === 4) {\n"
             "            window.history.forward();\n          }\n"
             "        }\n      })()"));
    return b.TakeStr();
}

} // namespace wry
