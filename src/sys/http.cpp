/* The portable half of the network: what a URL is, and the table that lets a
   paint ask for one without waiting for it.

   Hosted platforms adapt their blocking client through the executor. The
   browser starts fetch() directly and reports through the same callback. */

#include "sys/http.h"
#include "sys/executor.h"

namespace gpui {

void HttpRspFree(HttpRsp* r) {
    if (!r) {
        return;
    }
    VecReset(r->body);
    if (r->contentType.s) {
        StrFree(r->contentType);
        r->contentType = {};
    }
    StrFree(r->redirectUrl);
    r->redirectUrl = {};
    r->status = 0;
}

bool HttpUrlIsRemote(Str url) {
    if (!url.s || len(url) <= 0) {
        return false;
    }
    return base::StrStartsWithI(url, "http://") ||
           base::StrStartsWithI(url, "https://");
}

// ─── one request without waiting ─────────────────────────────────────────

// Owns the request fields while a hosted worker uses them. wasm copies them
// into JavaScript before HttpWasmSendAsync returns, but keeping the same
// ownership on every target makes the public contract one thing.
struct HttpAsyncJob {
    HttpReq req;
    Str url;
    Str method;
    Str body;
    Vec<HttpHeader> headers;
    HttpRsp response;
    Func1<HttpAsyncResult> done;
    bool ok = false;
};

static void HttpAsyncJobFree(HttpAsyncJob* job) {
    if (!job) {
        return;
    }
    StrFree(job->url);
    StrFree(job->method);
    StrFree(job->body);
    for (int i = 0; i < job->headers.len; i++) {
        StrFree(job->headers[i].name);
        StrFree(job->headers[i].value);
    }
    VecReset(job->headers);
    HttpRspFree(&job->response);
    delete job;
}

static HttpAsyncJob* HttpAsyncJobNew(const HttpReq& req,
                                     Func1<HttpAsyncResult> done) {
    HttpAsyncJob* job = new HttpAsyncJob();
    job->url = StrDup(req.url);
    job->method = StrDup(req.method);
    job->body = StrDup(req.body);
    bool ok = (job->url.s || len(req.url) == 0) &&
              (job->method.s || len(req.method) == 0) &&
              (job->body.s || len(req.body) == 0);
    for (int i = 0; i < req.nHeaders && ok; i++) {
        HttpHeader header;
        header.name = StrDup(req.headers[i].name);
        header.value = StrDup(req.headers[i].value);
        if ((!header.name.s && len(req.headers[i].name) != 0) ||
            (!header.value.s && len(req.headers[i].value) != 0) ||
            !VecAppend(job->headers, header)) {
            StrFree(header.name);
            StrFree(header.value);
            ok = false;
        }
    }
    if (!ok) {
        HttpAsyncJobFree(job);
        return nullptr;
    }
    job->req.url = job->url;
    job->req.method = job->method;
    job->req.headers = job->headers.len ? job->headers.els : nullptr;
    job->req.nHeaders = job->headers.len;
    job->req.body = job->body;
    job->req.noRedirect = req.noRedirect;
    job->done = done;
    return job;
}

#if GPUI_OS_WASM
// http_wasm.cpp copies the request into JavaScript before returning and calls
// `done` later, on the browser thread.
bool HttpWasmSendAsync(const HttpReq& req, Func1<HttpAsyncResult> done);

static void HttpAsyncWasmDone(HttpAsyncJob* job, HttpAsyncResult result) {
    job->done.Call(result);
    HttpAsyncJobFree(job);
}
#else
static void HttpAsyncWork(HttpAsyncJob* job) {
    job->ok = HttpSend(job->req, &job->response);
}

static void HttpAsyncDone(HttpAsyncJob* job) {
    HttpAsyncResult result = {job->ok, &job->response};
    job->done.Call(result);
    HttpAsyncJobFree(job);
}
#endif

bool HttpSendAsync(const HttpReq& req, Func1<HttpAsyncResult> done) {
    if (!done.IsValid()) {
        return false;
    }
    HttpAsyncJob* job = HttpAsyncJobNew(req, done);
    if (!job) {
        return false;
    }
#if GPUI_OS_WASM
    if (!HttpWasmSendAsync(job->req, MkFunc1(HttpAsyncWasmDone, job))) {
        HttpAsyncJobFree(job);
        return false;
    }
    return true;
#else
    if (!ExecSpawn(MkFunc0(HttpAsyncWork, job), MkFunc0(HttpAsyncDone, job))) {
        HttpAsyncJobFree(job);
        return false;
    }
    return true;
#endif
}

// ─── the table ────────────────────────────────────────────────────────────
//
// A document holds a handful of pictures, so this is a handful of slots and
// no more. A slot the worker still owns is never reused; everything else is
// fair game, oldest first, and evicting one drops the bytes it held.

struct FetchJob {
    int slot = 0;
    Func0 done = {};
#if !GPUI_OS_WASM
    Str url = {};
#endif
};

struct FetchSlot {
    Str url = {}; // owned; stable for as long as the slot is Pending
    FetchState state = FetchState::None;
    uint8_t* body = nullptr; // owned
    int len = 0;
    FetchJob* job = nullptr;
    TaskId task = 0;
    bool discard = false;
};

constexpr int kFetchSlots = 24;
// Enough that a page of pictures arrives together, few enough that a document
// pointing at a hundred URLs does not open a hundred connections.
constexpr int kMaxConcurrent = 4;

static FetchSlot gFetch[kFetchSlots];
static Mutex gFetchLock;
static int gFetchPending = 0;
static int gFetchNext = 0;
static bool gHttpEnabled = true;
// Read on the main thread when a fetch is started and run on the main thread
// when it lands, so it needs no lock of its own.
static Func0 gOnFetchDone;

void HttpSetOnFetchDone(Func0 f) {
    gOnFetchDone = f;
}

void HttpSetEnabled(bool on) {
    gFetchLock.Lock();
    gHttpEnabled = on;
    gFetchLock.Unlock();
}

bool HttpEnabled() {
    gFetchLock.Lock();
    bool on = gHttpEnabled;
    gFetchLock.Unlock();
    return on;
}

int HttpFetchPending() {
    gFetchLock.Lock();
    int n = gFetchPending;
    gFetchLock.Unlock();
    return n;
}

static void SlotDrop(FetchSlot* s) {
    if (s->body) {
        Free(nullptr, s->body);
        s->body = nullptr;
    }
    s->len = 0;
    if (s->url.s) {
        StrFree(s->url);
        s->url = {};
    }
    s->job = nullptr;
    s->task = 0;
    s->discard = false;
    s->state = FetchState::None;
}

#if GPUI_OS_WASM
// Runs on the browser thread after fetch() has landed. The slot index is still
// ours: eviction skips Pending.
static void FetchDone(FetchJob* job, HttpAsyncResult result) {
    HttpRsp* response = result.response;
    bool got = result.ok && response && response->status >= 200 &&
               response->status < 300 && len(response->body) > 0;

    gFetchLock.Lock();
    FetchSlot* s = &gFetch[job->slot];
    bool discard = s->discard;
    if (discard) {
        SlotDrop(s);
    } else if (got) {
        // The body moves rather than copies: Vec's assignment is a deep copy
        // and this is the one place that would rather not pay for it.
        s->body = response->body.els;
        s->len = len(response->body);
        response->body.els = nullptr;
        response->body.len = 0;
        response->body.cap = 0;
        s->state = FetchState::Done;
    } else {
        s->state = FetchState::Failed;
    }
    s->job = nullptr;
    s->task = 0;
    gFetchPending--;
    gFetchLock.Unlock();

    Func0 done = job->done;
    Free(nullptr, job);
    if (!discard) {
        done.Call();
    }
}
#else
// Hosted clients block, so their established image path owns and updates the
// slot on a pool thread. In particular, it does not leave a main-thread
// callback holding heap state when ExecShutdown deliberately drops late
// completions.
static void FetchWorker(FetchJob* job) {
    HttpRsp response;
    bool ok = HttpGet(job->url, &response);
    bool got = ok && response.status >= 200 && response.status < 300 &&
               len(response.body) > 0;

    gFetchLock.Lock();
    FetchSlot* s = &gFetch[job->slot];
    bool discard = s->discard;
    if (discard) {
        SlotDrop(s);
    } else if (got) {
        s->body = response.body.els;
        s->len = len(response.body);
        response.body.els = nullptr;
        response.body.len = 0;
        response.body.cap = 0;
        s->state = FetchState::Done;
    } else {
        s->state = FetchState::Failed;
    }
    s->job = nullptr;
    s->task = 0;
    gFetchPending--;
    gFetchLock.Unlock();

    Func0 done = job->done;
    HttpRspFree(&response);
    StrFree(job->url);
    Free(nullptr, job);
    if (!discard) {
        ExecPost(done);
    }
}
#endif

// The lock is held. Null when every slot is either Pending or the table is
// full of them.
static FetchSlot* SlotFree() {
    for (int i = 0; i < kFetchSlots; i++) {
        int ix = (gFetchNext + i) % kFetchSlots;
        FetchSlot* s = &gFetch[ix];
        if (s->state == FetchState::Pending) {
            continue;
        }
        gFetchNext = (ix + 1) % kFetchSlots;
        SlotDrop(s);
        return s;
    }
    return nullptr;
}

FetchState HttpFetch(Str url, const uint8_t** bytes, int* len) {
    if (bytes) {
        *bytes = nullptr;
    }
    if (len) {
        *len = 0;
    }
    if (!HttpUrlIsRemote(url)) {
        return FetchState::Failed;
    }

    gFetchLock.Lock();
    for (int i = 0; i < kFetchSlots; i++) {
        FetchSlot* s = &gFetch[i];
        if (s->state == FetchState::None || !base::StrEq(s->url, url)) {
            continue;
        }
        FetchState st = s->state;
        if (st == FetchState::Done) {
            if (bytes) {
                *bytes = s->body;
            }
            if (len) {
                *len = s->len;
            }
        }
        gFetchLock.Unlock();
        return st;
    }
    // Nothing knows this URL. Start it, unless we are already busy or the
    // application has said not to — either way the caller asks again.
    if (!gHttpEnabled || gFetchPending >= kMaxConcurrent) {
        gFetchLock.Unlock();
        return FetchState::None;
    }
    FetchSlot* s = SlotFree();
    if (!s) {
        gFetchLock.Unlock();
        return FetchState::None;
    }
    FetchJob* job = AllocArray<FetchJob>(1);
    if (!job) {
        gFetchLock.Unlock();
        return FetchState::None;
    }
    s->url = StrDup(url);
    s->state = FetchState::Pending;
    s->job = job;
    job->slot = (int)(s - gFetch);
    job->done = gOnFetchDone;
#if !GPUI_OS_WASM
    job->url = StrDup(url);
#endif
    gFetchPending++;
    gFetchLock.Unlock();

#if GPUI_OS_WASM
    HttpReq req;
    req.url = url;
    bool started = HttpSendAsync(req, MkFunc1(FetchDone, job));
#else
    TaskId task = job->url.s ? ExecSpawn(MkFunc0(FetchWorker, job)) : 0;
    bool started = task != 0;
#endif
    if (!started) {
        gFetchLock.Lock();
        gFetchPending--;
        SlotDrop(s);
        gFetchLock.Unlock();
#if !GPUI_OS_WASM
        StrFree(job->url);
#endif
        Free(nullptr, job);
        return FetchState::None;
    }
#if !GPUI_OS_WASM
    gFetchLock.Lock();
    // A very small response may have completed before ExecSpawn returned.
    if (s->state == FetchState::Pending && s->job == job) {
        s->task = task;
    }
    gFetchLock.Unlock();
#endif
    return FetchState::Pending;
}

void HttpFetchClear() {
    FetchJob* cancelled[kFetchSlots] = {};
    int cancelledN = 0;
    gFetchLock.Lock();
    for (int i = 0; i < kFetchSlots; i++) {
        FetchSlot* s = &gFetch[i];
        if (s->state != FetchState::Pending) {
            SlotDrop(s);
            continue;
        }
        s->discard = true;
#if !GPUI_OS_WASM
        if (s->task && ExecCancel(s->task)) {
            cancelled[cancelledN++] = s->job;
            gFetchPending--;
            SlotDrop(s);
        }
#endif
    }
    gFetchNext = 0;
    gFetchLock.Unlock();
    for (int i = 0; i < cancelledN; i++) {
        FetchJob* job = cancelled[i];
#if !GPUI_OS_WASM
        StrFree(job->url);
#endif
        Free(nullptr, job);
    }
}

void HttpFetchDrop(Str url) {
    gFetchLock.Lock();
    for (int i = 0; i < kFetchSlots; i++) {
        FetchSlot* s = &gFetch[i];
        if (s->state != FetchState::None && s->state != FetchState::Pending &&
            base::StrEq(s->url, url)) {
            SlotDrop(s);
            break;
        }
    }
    gFetchLock.Unlock();
}

} // namespace gpui
