/* crates/story/examples/stream_markdown.rs — a document that arrives a few
   characters at a time, reparsed and redrawn on every chunk.

   The Rust example keeps a `TextViewState` and calls `push_str` on it, which
   reparses and rerenders itself; a `TextView` here is a per-frame builder over
   a source string, so the document *is* the state — the example appends to a
   StrBuilder and the next frame parses what has arrived. `MdParseCached` is
   what keeps that honest: the parse is keyed by the source, so a frame that
   adds nothing does not reparse, and one that adds five characters parses the
   whole document again. Which is the demonstration: this is what a streaming
   answer costs.

   The chunks come off a pool thread the way Rust's do — `background_executor()
   .spawn` with a channel to the foreground is `ExecSpawn` with `WindowPost`
   here — and are cut 5 to 20 characters at a time with 50 ms between them. A
   wasm page has no thread to give (`ExecHasThreads`), so there the same cutter
   runs on a 50 ms interval timer instead of a worker that sleeps. */

#include "gpui.h"

using namespace gpui;

// One chunk on its way to the main thread. Heap-allocated by whoever cut it
// and freed by the listener that takes it, since the post outlives the frame.
struct StreamChunk {
    int gen = 0;
    Str text = {};
};

// What the worker and the main thread meet over. The worker never reads the
// app: it takes the generation from here, and a Replay — or the window going
// away — bumps it, which is how a run that is still sleeping finds out it is
// no longer the current one. Rust drops the previous `_update_task`, which
// cancels it; a thread cannot be stopped mid-sleep, so this is the same rule
// spelled as a flag it checks.
static Mutex gStreamLock;
static int gStreamGen = 0;

static int StreamGen() {
    gStreamLock.Lock();
    int gen = gStreamGen;
    gStreamLock.Unlock();
    return gen;
}

static int StreamBumpGen() {
    gStreamLock.Lock();
    int gen = ++gStreamGen;
    gStreamLock.Unlock();
    return gen;
}

struct StreamApp {
    // The document as far as it has arrived. Rust's TextViewState holds it.
    StrBuilder doc;
    // The whole fixture, and how much of it has been handed out.
    Str source = {};
    int sent = 0;
    // replay_id: a chunk that names an older run is dropped on arrival.
    int gen = 0;
    bool streaming = false;
    // The interval that paces the wasm path, where there is no worker.
    int timer = 0;
    // Where the view is scrolled, and the two boxes that say how far it can
    // go — scroll_to_bottom needs the content's height, and an element only
    // knows its own after it has been laid out.
    float scroll = 0;
    Bounds viewBox = {};
    Bounds contentBox = {};
    // The example's own handle, for the listener a worker thread posts with.
    Entity<StreamApp> me = {};

    ~StreamApp() { StrFree(source); }
    static El* Render(StreamApp* self, Ctx* cx);
};

// 5 + rand % 15 characters, in Rust. The cut lands on a character boundary
// rather than in the middle of one — `EXAMPLE.chars()` cannot split a `世`,
// and neither can this.
static int ChunkEnd(Str s, int from, uint32_t* seed) {
    *seed = *seed * 1664525u + 1013904223u;
    int want = 5 + (int)((*seed >> 16) % 15u);
    int end = from + want;
    if (end >= len(s)) {
        return len(s);
    }
    while (end < len(s) && ((uint8_t)s.s[end] & 0xC0) == 0x80) {
        end++;
    }
    return end;
}

// The main thread's half: take the chunk if it belongs to the current run.
static void OnChunk(StreamApp* self, Ctx* cx, const StreamChunk* ev) {
    if (!ev) {
        return;
    }
    if (ev->gen == self->gen) {
        self->doc.Append(ev->text);
        // scroll_handle.scroll_to_bottom(): the view follows the text as it
        // grows. The height it can scroll to is last frame's, which is a
        // frame behind and a chunk short — invisible at fifty milliseconds.
        float maxY = self->contentBox.h - self->viewBox.h;
        self->scroll = maxY > 0 ? maxY : 0;
        Notify(cx);
    }
    StrFree(ev->text);
    Free(nullptr, (void*)ev);
}

// The worker's half. Everything it needs is in the job; it touches no entity.
struct StreamJob {
    Window* win = nullptr;
    Listener onChunk;
    Str text = {};
    int gen = 0;
};

static void PostChunk(StreamJob* job, int from, int end) {
    auto* c = AllocArray<StreamChunk>(1);
    if (!c) {
        return;
    }
    c->gen = job->gen;
    c->text = StrDup(Str(job->text.s + from, end - from));
    WindowPost(job->win, job->onChunk, c);
}

static void StreamWorker(StreamJob* job) {
    uint32_t seed = (uint32_t)job->gen * 2654435761u + 12345u;
    int at = 0;
    while (at < len(job->text)) {
        if (StreamGen() != job->gen) {
            break;
        }
        int end = ChunkEnd(job->text, at, &seed);
        PostChunk(job, at, end);
        at = end;
        // Rust's `thread::sleep(50ms)`, whole: a Windows sleep rounds up to
        // the scheduler's tick, so five ten-millisecond ones are half again
        // as long as one fifty. A Replay is heard at the end of the wait,
        // which is where Rust hears it too -- its task cannot interrupt a
        // sleep either, and the stale chunks are dropped on arrival.
        PlatSleepMs(50);
    }
    StrFree(job->text);
    Free(nullptr, job);
}

// The wasm path: no thread, so the interval cuts the next chunk itself.
static void OnTick(StreamApp* self, Ctx* cx, const TickEvent*) {
    if (!self->streaming || self->sent >= len(self->source)) {
        if (self->timer) {
            WindowCancelTimer(cx->win, self->timer);
            self->timer = 0;
        }
        self->streaming = false;
        return;
    }
    static uint32_t seed = 7u;
    int end = ChunkEnd(self->source, self->sent, &seed);
    self->doc.Append(Str(self->source.s + self->sent, end - self->sent));
    self->sent = end;
    float maxY = self->contentBox.h - self->viewBox.h;
    self->scroll = maxY > 0 ? maxY : 0;
    Notify(cx);
}

static void Replay(StreamApp* self, Ctx* cx, const ClickEvent*) {
    // set_text("") and a new run: the generation is what tells a worker that
    // is still going that its chunks are no longer wanted.
    self->gen = StreamBumpGen();
    self->doc.Reset();
    self->sent = 0;
    self->scroll = 0;
    self->streaming = true;
    if (self->timer) {
        WindowCancelTimer(cx->win, self->timer);
        self->timer = 0;
    }
    if (ExecHasThreads()) {
        auto* job = AllocArray<StreamJob>(1);
        if (!job) {
            return;
        }
        job->win = cx->win;
        job->onChunk = ListenTo(self->me, &OnChunk);
        job->text = StrDup(self->source);
        job->gen = self->gen;
        if (!ExecSpawn(MkFunc0(StreamWorker, job))) {
            StrFree(job->text);
            Free(nullptr, job);
        }
    } else {
        self->timer = WindowSetInterval(cx->win, 50, Listen(cx, &OnTick));
    }
    Notify(cx);
}

static void OnScroll(StreamApp* self, Ctx* cx, const ScrollEvent* ev) {
    self->scroll = ev->offsetY;
    Notify(cx);
}

El* StreamApp::Render(StreamApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);

    El* bar = Div(a)->FlexRow()->W(kFill)->Child(
        component::Button::New(cx, StrL("replay"))
            ->Outline()
            ->Label(StrL("Replay"))
            ->OnClick(Listen(cx, &Replay))
            ->IntoEl());

    Str doc = Str(self->doc.els, len(self->doc));
    El* content =
        Div(a)
            ->FlexCol()
            ->W(kFill)
            ->BoundsOut(&self->contentBox)
            ->Child(component::TextView::New(cx, doc)->Selectable()->IntoEl());
    El* body = Div(a)
                   ->FlexCol()
                   ->Flex1()
                   ->W(kFill)
                   ->ClipY()
                   ->BoundsOut(&self->viewBox)
                   ->ScrollY(self->scroll)
                   ->ScrollId(HashClickId(StrL("contents")))
                   ->OnScroll(Listen(cx, &OnScroll))
                   ->Child(content);

    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->Pad(16)
        ->Gap(16)
        ->Bg(th.tokens.background)
        ->Child(bar)
        ->Child(body);
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    AssetsClear();
    AssetsAddDefaultRoots(StrL("stream_markdown"));
    AssetsAddRoot(StrL("assets/stream_markdown"));
    Entity<StreamApp> view = EntityNew<StreamApp>(app);
    StreamApp* self = view.Get(app);
    self->me = view;
    // include_str!("./fixtures/test.md"), read from the asset roots rather
    // than baked into the binary — the same place markdown_table's report.md
    // comes from.
    TempStr md = AssetsLoadTextTemp(StrL("test.md"));
    self->source = StrDup(md);
    self->doc.Append(StrL("# Streaming Markdown Parse\n\n"));
    Window* win = WindowOpenView(app, StrL("Stream Markdown"), 600, 800,
                                 view.id, WinOpts{});
    (void)win;
    int rc = AppRun(app);
    // The window is going; a worker still cutting chunks has to hear about it
    // before anything it would post to is freed.
    StreamBumpGen();
    ExecWaitIdle(500);
    AppFree(app);
    return rc;
}
