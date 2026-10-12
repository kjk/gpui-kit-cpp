/* sys/http.h without a network: what counts as a URL this can fetch, and
   that the fetch table refuses everything else and starts nothing at all
   when fetching is turned off.

   Nothing here makes a request. A suite that reached the network would fail
   on a machine without one and would be slow on a machine with one, so
   TestsMain turns fetching off before any of it runs and the check below is
   that the switch is honoured. */

#include "Test.h"

static void HttpOnlyKnowsTwoSchemes() {
    utassert(HttpUrlIsRemote(StrL("http://example.com/a.png")));
    utassert(HttpUrlIsRemote(StrL("https://example.com/a.png")));
    // The scheme is not case sensitive; a host without a path is still a URL.
    utassert(HttpUrlIsRemote(StrL("HTTPS://EXAMPLE.COM")));
    utassert(!HttpUrlIsRemote(StrL("ftp://example.com/a.png")));
    utassert(!HttpUrlIsRemote(StrL("data:image/png;base64,iVBORw0KGgo=")));
    utassert(!HttpUrlIsRemote(StrL("icons/inbox.svg")));
    utassert(!HttpUrlIsRemote(StrL("")));
    utassert(!HttpUrlIsRemote(StrL("https:/")));
}

// A src the client cannot even attempt is a failure now, not something to
// wait for: image.cpp writes that answer down and stops asking.
static void AFetchOfSomethingLocalFailsAtOnce() {
    const uint8_t* bytes = (const uint8_t*)1;
    int len = 7;
    utassert(HttpFetch(StrL("icons/inbox.svg"), &bytes, &len) ==
             FetchState::Failed);
    utassert(bytes == nullptr);
    utassert(len == 0);
    utassert(HttpFetchPending() == 0);
}

static void FetchingOffStartsNothing() {
    utassert(!HttpEnabled());
    const uint8_t* bytes = nullptr;
    int len = 0;
    // None, not Pending: nothing was handed to a thread, and the caller is
    // free to ask again later.
    utassert(HttpFetch(StrL("https://example.com/a.png"), &bytes, &len) ==
             FetchState::None);
    utassert(HttpFetchPending() == 0);
}

static int NoBody(void*, uint8_t*, int) {
    return -1;
}

static bool DropBody(void*, const uint8_t*, int) {
    return true;
}

static void IgnoreResult(HttpAsyncResult) {}

// The callbacks of a streamed body are borrowed for one blocking call, so the
// asynchronous client has nothing it could safely do with them.
static void AStreamedRequestIsNotSentAsync() {
    HttpReq up;
    up.url = StrL("https://example.com/upload");
    up.method = StrL("PUT");
    up.readBody = NoBody;
    up.bodyLen = 1;
    utassert(!HttpSendAsync(up, MkFunc1Void(IgnoreResult)));
    HttpReq down;
    down.url = StrL("https://example.com/big");
    down.onBody = DropBody;
    utassert(!HttpSendAsync(down, MkFunc1Void(IgnoreResult)));
}

// The header block a client hands over becomes one line a header, and a
// followed redirect leaves only the last answer's.
static void HeadersComeFromTheLastAnswer() {
    Str raw = StrL(
        "HTTP/1.1 302 Found\r\n"
        "Location: /b\r\n"
        "X-Hop: first\r\n"
        "\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "ETag:   \"abc\"  \r\n"
        "x-amz-meta-name: geometry.pdf\r\n"
        "Set-Cookie: a=1\r\n"
        "Set-Cookie: b=2\r\n"
        "\r\n");
    HttpRsp r;
    r.headers = HttpHeadersFromRaw(raw);
    utassert(StrEq(r.headers, StrL("Content-Type: text/plain\n"
                                   "ETag: \"abc\"\n"
                                   "x-amz-meta-name: geometry.pdf\n"
                                   "Set-Cookie: a=1\n"
                                   "Set-Cookie: b=2\n")));
    // A name is found whatever its case, and the first of two wins.
    utassert(StrEq(HttpRspHeader(r, StrL("etag")), StrL("\"abc\"")));
    utassert(StrEq(HttpRspHeader(r, StrL("CONTENT-TYPE")), StrL("text/plain")));
    utassert(StrEq(HttpRspHeader(r, StrL("Set-Cookie")), StrL("a=1")));
    utassert(len(HttpRspHeader(r, StrL("X-Hop"))) == 0);
    utassert(len(HttpRspHeader(r, StrL("ETa"))) == 0);
    HttpRspFree(&r);
    utassert(len(r.headers) == 0);
    // LF alone ends a line too, and a line without a colon is not a header.
    Str bare = HttpHeadersFromRaw(StrL("HTTP/2 200\nA: 1\nnot a header\nB:\n"));
    utassert(StrEq(bare, StrL("A: 1\nB: \n")));
    StrFree(bare);
    utassert(len(HttpHeadersFromRaw(StrL(""))) == 0);
}

void TestHttp() {
    TestSuite("http");
    HeadersComeFromTheLastAnswer();
    AStreamedRequestIsNotSentAsync();
    HttpOnlyKnowsTwoSchemes();
    AFetchOfSomethingLocalFailsAtOnce();
    FetchingOffStartsNothing();
}
