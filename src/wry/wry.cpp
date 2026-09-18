/* The portable half of the port: wry/src/webview2/mod.rs's custom protocol
 * URI work-around, which is string work and nothing else.
 *
 * Part of the C++ port of lb-wry 0.53.3 (see src/wry/readme.md).
 *
 * It lives out here rather than in wry_win.cpp because the crate's own unit
 * test — `checks_if_custom_protocol_uri` — is over these three functions, and
 * tests/WryTests.cpp is one binary on every platform.
 */

#include "wry/wry.h"

namespace wry {

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

} // namespace wry
