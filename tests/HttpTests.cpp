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

void TestHttp() {
    TestSuite("http");
    AStreamedRequestIsNotSentAsync();
    HttpOnlyKnowsTwoSchemes();
    AFetchOfSomethingLocalFailsAtOnce();
    FetchingOffStartsNothing();
}
