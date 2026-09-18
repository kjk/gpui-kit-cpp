#include "Story.h"

// crates/story/src/stories/message_scroller_story.rs

// section(..).max_w(rems(45.)) — 720 at the 16px root.
static const float kMsSectionMaxW = 720;
// preview_frame: w(rems(26.)) by h(rems(19.)), less its one-pixel border.
static const float kMsPreviewW = 416;
static const float kMsPreviewH = 304;
// The main demo frame: w_96 by h(rems(35.)). The transcript is given the
// height the header and composer leave it, because the virtual list here is
// told how tall its viewport is rather than filling a flexible box.
static const float kMsFrameW = 384;
static const float kMsFrameH = 560;
static const float kMsFrameListH = 340;

static const int kMsInitialStreamMessageCount = 7;

static const char* kMsStreamResponse =
    "The virtual list follows new content while you remain at the live edge. "
    "Scroll upward during this response and your reading position stays in "
    "place until you choose to return to the latest message.";

static const char* kMsExpandSuffix =
    "\n\nExpanded details demonstrate how images, Markdown, and progressive "
    "content can change an existing row without replacing its identity.";

struct DemoMessage {
    int id = 0;
    bool sent = false;
    Str body = {};
    // Whether `body` is a heap copy this story owns.
    bool owned = false;
};

static void MsSetBody(DemoMessage* m, Str body, bool own) {
    if (m->owned) {
        StrFree(m->body);
    }
    m->body = own ? StrDup(body) : body;
    m->owned = own;
}

static void MsFreeList(Vec<DemoMessage>& list) {
    for (int i = 0; i < len(list); i++) {
        if (list[i].owned) {
            StrFree(list[i].body);
        }
    }
    VecReset(list);
}

static void MsAppend(Vec<DemoMessage>& list, int id, bool sent, Str body,
                     bool own) {
    DemoMessage m;
    m.id = id;
    m.sent = sent;
    m.body = own ? StrDup(body) : body;
    m.owned = own;
    VecAppend(list, m);
}

// preview_messages(first_id, count).
static void MsPreviewMessages(Vec<DemoMessage>& out, int firstId, int count) {
    for (int i = 0; i < count; i++) {
        Str body;
        if (i % 4 == 0) {
            body =
                fmt("Message %d wraps across more than one line to keep "
                    "virtual-row measurement realistic.",
                    i + 1);
        } else {
            body = fmt("Conversation message %d", i + 1);
        }
        MsAppend(out, firstId + i, i % 3 == 2, body, true);
    }
}

// conversation_script(): shadcn's message-scroller demo, an AI chat with no
// avatars or author names.
struct MsScriptLine {
    bool sent;
    const char* body;
};

static const MsScriptLine kMsScript[] = {
    {true,
     "I'm building a chat for our app and the scroll behavior is driving me "
     "nuts. Every time the AI streams a reply, the whole thread jumps "
     "around."},
    {false,
     "That's the classic streaming scroll problem. Render the rows with "
     "MessageScroller — it follows the tail while the reader sits at the live "
     "edge, so streamed tokens land in place instead of shoving the thread "
     "around."},
    {true,
     "Okay, but what happens when someone scrolls up to re-read an older "
     "answer? I don't want to yank them back down."},
    {false,
     "You won't. Scrolling up releases tail following, so the reading "
     "position is preserved while new rows keep arriving below.\n\nA "
     "jump-to-latest button appears once the reader leaves the tail; one "
     "click returns to the newest row and resumes following."},
    {true, "And loading older history when they reach the top?"},
    {false,
     "prepend inserts the earlier rows while the row the reader is on stays "
     "anchored in place — no jump to the top, no lost context."},
    {true,
     "Last one — does it handle rows that change height while streaming?"},
    {false,
     "Yes. Remeasure just the growing row and the list keeps its anchor, so "
     "streamed markdown, images, and expanding content stay stable."},
};

static void MsConversationScript(Vec<DemoMessage>& out) {
    int n = (int)(sizeof(kMsScript) / sizeof(kMsScript[0]));
    for (int i = 0; i < n; i++) {
        MsAppend(out, i, kMsScript[i].sent, Str(kMsScript[i].body), false);
    }
}

struct MessageScrollerStory {
    Entity<component::MessageScrollerState> scroller;
    Entity<component::MessageScrollerState> streamScroller;
    Entity<component::MessageScrollerState> historyScroller;
    Entity<component::MessageScrollerState> navigationScroller;
    Entity<component::MessageScrollerState> customScroller;
    Entity<component::MessageScrollerState> applicationScroller;
    Entity<component::MessageScrollerState> emptyScroller;
    InputState composer;
    Vec<DemoMessage> messages;
    Vec<DemoMessage> streamMessages;
    Vec<DemoMessage> historyMessages;
    Vec<DemoMessage> previewMessages;
    Vec<DemoMessage> emptyMessages;
    int unreadIndex = 4;
    int nextId = 10000;
    bool streaming = false;
    int streamTimer = 0;
    int streamPrefix = 0;
    int streamMessageIx = 0;
    bool seeded = false;

    ~MessageScrollerStory() {
        MsFreeList(messages);
        MsFreeList(streamMessages);
        MsFreeList(historyMessages);
        MsFreeList(previewMessages);
        MsFreeList(emptyMessages);
    }

    static El* Render(MessageScrollerStory* self, Ctx* cx);
};

static component::MessageScrollerState* MsState(
    Ctx* cx, Entity<component::MessageScrollerState> e) {
    return e.Get(cx->app);
}

// create_scroller(count, cx).
static Entity<component::MessageScrollerState> MsCreateScroller(App* app,
                                                                int count) {
    Entity<component::MessageScrollerState> e =
        EntityNewState<component::MessageScrollerState>(app);
    component::MessageScrollerState::Init(e.Get(app), count);
    return e;
}

static void MsSeed(MessageScrollerStory* self, Ctx* cx) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;
    MsConversationScript(self->messages);
    MsPreviewMessages(self->streamMessages, 1000,
                      kMsInitialStreamMessageCount - 1);
    MsAppend(self->streamMessages, 1000 + kMsInitialStreamMessageCount - 1,
             true, StrL("How does streaming preserve my position?"), false);
    MsPreviewMessages(self->historyMessages, 2000, 14);
    MsPreviewMessages(self->previewMessages, 3000, 18);

    App* app = cx->app;
    self->scroller = MsCreateScroller(app, self->messages.len);
    self->streamScroller = MsCreateScroller(app, self->streamMessages.len);
    self->historyScroller = MsCreateScroller(app, self->historyMessages.len);
    self->navigationScroller = MsCreateScroller(app, self->previewMessages.len);
    self->customScroller = MsCreateScroller(app, self->previewMessages.len);
    self->applicationScroller =
        MsCreateScroller(app, self->previewMessages.len);
    self->emptyScroller = MsCreateScroller(app, 0);
    InputSetPlaceholder(&self->composer, StrL("Write a message…"));
}

// ─── the row renderer ─────────────────────────────────────────────────────

struct MsRows {
    Vec<DemoMessage>* list = nullptr;
    int unread = -1;
};

static El* MsRow(void* user, Ctx* cx, int index) {
    Arena* a = cx->a;
    MsRows* rows = (MsRows*)user;
    if (!rows->list || index < 0 || index >= rows->list->len) {
        return Div(a);
    }
    const DemoMessage& message = (*rows->list)[index];
    // Mirror shadcn's message-scroller demo: no avatars or author names, sent
    // rows on a muted surface, received rows as ghost text.
    component::MessageAlignment alignment =
        message.sent ? component::MessageAlignment::End
                     : component::MessageAlignment::Start;
    component::Bubble* bubble =
        component::Bubble::New(cx)
            ->WithVariant(message.sent ? component::BubbleVariant::Muted
                                       : component::BubbleVariant::Ghost)
            ->Child(TextEl(a, message.body)->Wrap());

    El* row = Div(a)->FlexCol()->W(kFill)->MinW(0)->Gap(12);
    if (index == rows->unread) {
        row->Child(component::Marker::New(cx)
                       ->WithVariant(component::MarkerVariant::Separator)
                       ->Content(component::MarkerContent::New(cx)
                                     ->Child(TextEl(a, StrL("Unread"))))
                       ->IntoEl());
    }
    row->Child(Div(a)->W(kFill)->Child(
        component::Message::New(cx)
            ->Alignment(alignment)
            ->Content(component::MessageContent::New(cx)->WithBubble(bubble))
            ->IntoEl()));
    return row;
}

static MsRows* MsRowsOf(Ctx* cx, Vec<DemoMessage>* list, int unread) {
    MsRows* rows = ArenaNew<MsRows>(cx->a);
    rows->list = list;
    rows->unread = unread;
    return rows;
}

// ─── the story's own actions ──────────────────────────────────────────────

static void MsAppendMessage(MessageScrollerStory* self, Ctx* cx,
                            const ClickEvent*) {
    int id = self->nextId++;
    MsAppend(self->messages, id, true,
             fmt("New message %d", self->messages.len + 1), true);
    if (auto* st = MsState(cx, self->scroller)) {
        st->Append(cx, 1);
    }
    Notify(cx);
}

static void MsPrependHistory(MessageScrollerStory* self, Ctx* cx,
                             const ClickEvent*) {
    const int kCount = 5;
    int firstId = self->nextId;
    self->nextId += kCount;
    DemoMessage* space = VecInsertSpace(self->messages, 0, kCount);
    for (int i = 0; i < kCount && space; i++) {
        space[i] = DemoMessage{};
        space[i].id = firstId + i;
        space[i].sent = false;
        MsSetBody(&space[i], fmt("Earlier history %d", i + 1), true);
    }
    self->unreadIndex += kCount;
    if (auto* st = MsState(cx, self->scroller)) {
        st->Prepend(cx, kCount);
    }
    Notify(cx);
}

static void MsScrollToUnread(MessageScrollerStory* self, Ctx* cx,
                             const ClickEvent*) {
    if (auto* st = MsState(cx, self->scroller)) {
        st->ScrollToItem(cx, self->unreadIndex);
    }
    Notify(cx);
}

static void MsSend(MessageScrollerStory* self, Ctx* cx, const ClickEvent*) {
    Str body = InputValue(&self->composer);
    bool blank = true;
    for (int i = 0; i < len(body); i++) {
        if (body.s[i] != ' ' && body.s[i] != '\t' && body.s[i] != '\n') {
            blank = false;
            break;
        }
    }
    if (blank) {
        return;
    }
    MsAppend(self->messages, self->nextId++, true, body, true);
    InputSetValue(&self->composer, StrL(""));
    if (auto* st = MsState(cx, self->scroller)) {
        st->Append(cx, 1);
    }
    Notify(cx);
}

static void MsReset(MessageScrollerStory* self, Ctx* cx, const ClickEvent*) {
    MsFreeList(self->messages);
    MsConversationScript(self->messages);
    self->unreadIndex = 4;
    if (auto* st = MsState(cx, self->scroller)) {
        st->Reset(cx, self->messages.len);
    }
    Notify(cx);
}

static void MsStreamTick(MessageScrollerStory* self, Ctx* cx, const TickEvent*);

static void MsStartStream(MessageScrollerStory* self, Ctx* cx,
                          const ClickEvent*) {
    if (self->streaming) {
        return;
    }
    self->streamMessageIx = self->streamMessages.len;
    self->streamPrefix = 0;
    self->streaming = true;
    MsAppend(self->streamMessages, self->nextId++, false,
             StrL("Preparing response…"), false);
    if (auto* st = MsState(cx, self->streamScroller)) {
        st->Append(cx, 1);
    }
    self->streamTimer =
        WindowSetInterval(cx->win, 110, Listen(cx, &MsStreamTick));
    Notify(cx);
}

static void MsStopStream(MessageScrollerStory* self, Ctx* cx,
                         const ClickEvent*) {
    self->streaming = false;
    if (self->streamTimer) {
        WindowCancelTimer(cx->win, self->streamTimer);
        self->streamTimer = 0;
    }
    Notify(cx);
}

// One token of the scripted response, appended to the row that is streaming.
// The tokens are the response's own words, so the body is always a prefix of
// it; only its length moves.
static void MsStreamTick(MessageScrollerStory* self, Ctx* cx,
                         const TickEvent*) {
    if (!self->streaming || self->streamMessageIx >= self->streamMessages.len) {
        return;
    }
    int total = (int)strlen(kMsStreamResponse);
    int at = self->streamPrefix;
    while (at < total && kMsStreamResponse[at] == ' ') {
        at++;
    }
    while (at < total && kMsStreamResponse[at] != ' ') {
        at++;
    }
    self->streamPrefix = at;
    MsSetBody(&self->streamMessages[self->streamMessageIx],
              Str(kMsStreamResponse, at), true);
    if (auto* st = MsState(cx, self->streamScroller)) {
        st->RemeasureItems(cx, self->streamMessageIx,
                           self->streamMessageIx + 1);
    }
    if (at >= total) {
        self->streaming = false;
        if (self->streamTimer) {
            WindowCancelTimer(cx->win, self->streamTimer);
            self->streamTimer = 0;
        }
    }
    Notify(cx);
}

static void MsResetStream(MessageScrollerStory* self, Ctx* cx,
                          const ClickEvent*) {
    self->streaming = false;
    if (self->streamTimer) {
        WindowCancelTimer(cx->win, self->streamTimer);
        self->streamTimer = 0;
    }
    for (int i = self->streamMessages.len - 1;
         i >= kMsInitialStreamMessageCount; i--) {
        if (self->streamMessages[i].owned) {
            StrFree(self->streamMessages[i].body);
        }
        VecRemoveAt(self->streamMessages, i);
    }
    if (auto* st = MsState(cx, self->streamScroller)) {
        st->Reset(cx, kMsInitialStreamMessageCount);
    }
    Notify(cx);
}

static void MsExpandStream(MessageScrollerStory* self, Ctx* cx,
                           const ClickEvent*) {
    int ix = self->streamMessages.len - 1;
    if (ix < 0) {
        return;
    }
    DemoMessage& m = self->streamMessages[ix];
    Str grown = fmt("%s%s", m.body, Str(kMsExpandSuffix));
    MsSetBody(&m, grown, true);
    if (auto* st = MsState(cx, self->streamScroller)) {
        st->RemeasureItems(cx, ix, ix + 1);
    }
    Notify(cx);
}

static void MsLoadEarlier(MessageScrollerStory* self, Ctx* cx,
                          const ClickEvent*) {
    const int kCount = 5;
    int firstId = self->nextId;
    self->nextId += kCount;
    DemoMessage* space = VecInsertSpace(self->historyMessages, 0, kCount);
    for (int i = 0; i < kCount && space; i++) {
        space[i] = DemoMessage{};
        space[i].id = firstId + i;
        MsSetBody(&space[i], fmt("Earlier saved message %d", i + 1), true);
    }
    if (auto* st = MsState(cx, self->historyScroller)) {
        st->Prepend(cx, kCount);
    }
    Notify(cx);
}

static void MsToggleEmpty(MessageScrollerStory* self, Ctx* cx,
                          const ClickEvent*) {
    if (self->emptyMessages.len == 0) {
        MsAppend(self->emptyMessages, 20000, true,
                 StrL("A first message replaces the application-owned empty "
                      "state."),
                 false);
        if (auto* st = MsState(cx, self->emptyScroller)) {
            st->Append(cx, 1);
        }
    } else {
        MsFreeList(self->emptyMessages);
        if (auto* st = MsState(cx, self->emptyScroller)) {
            st->Reset(cx, 0);
        }
    }
    Notify(cx);
}

// Which scroller a navigation button drives, and where it sends it.
enum MsNav {
    MsNavHistoryOldest = 1,
    MsNavFirst,
    MsNavMiddle,
    MsNavLatest,
    MsNavCustomTop,
    MsNavApplicationTop,
    MsNavApplicationLatest
};

static void MsNavigate(MessageScrollerStory* self, Ctx* cx, const ClickEvent*,
                       intptr_t which) {
    switch (which) {
        case MsNavHistoryOldest:
            if (auto* st = MsState(cx, self->historyScroller)) {
                st->ScrollToItem(cx, 0);
            }
            break;
        case MsNavFirst:
            if (auto* st = MsState(cx, self->navigationScroller)) {
                st->ScrollToItem(cx, 0);
            }
            break;
        case MsNavMiddle:
            if (auto* st = MsState(cx, self->navigationScroller)) {
                st->ScrollToItem(cx, 8);
            }
            break;
        case MsNavLatest:
            if (auto* st = MsState(cx, self->navigationScroller)) {
                st->ScrollToEnd(cx);
            }
            break;
        case MsNavCustomTop:
            if (auto* st = MsState(cx, self->customScroller)) {
                st->ScrollToItem(cx, 0);
            }
            break;
        case MsNavApplicationTop:
            if (auto* st = MsState(cx, self->applicationScroller)) {
                st->ScrollToItem(cx, 0);
            }
            break;
        case MsNavApplicationLatest:
            if (auto* st = MsState(cx, self->applicationScroller)) {
                st->ScrollToEnd(cx);
            }
            break;
        default:
            break;
    }
    Notify(cx);
}

// with_jump_button_renderer(|button| button.outline().small().label("Latest"))
static void MsCustomJumpButton(component::Button* button) {
    button->Outline()->WithSize(UiSize::Small)->Label(StrL("Latest"));
}

// ─── rendering ────────────────────────────────────────────────────────────

static El* MsSection(Ctx* cx, const char* title, const char* desc, float gap) {
    El* section = StorySection(cx, title, desc);
    StorySectionBody(section)->FlexCol()->Gap(gap)->MaxW(kMsSectionMaxW);
    return section;
}

static El* MsButton(Ctx* cx, const char* id, const char* label,
                    Listener onClick) {
    return component::Button::New(cx, Str(id))
        ->Label(Str(label))
        ->OnClick(onClick)
        ->IntoEl();
}

// preview_frame(scroller, cx).
static El* MsPreviewFrame(Ctx* cx, El* scroller) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->W(kMsPreviewW)
        ->MaxW(kFill)
        ->H(kMsPreviewH)
        ->ClipX()
        ->ClipY()
        ->Radius(ThemeRadius2xl(th))
        ->Border(1, th.border)
        ->Bg(th.tokens.background)
        ->Fg(th.foreground)
        ->Child(scroller);
}

El* MessageScrollerStory::Render(MessageScrollerStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    MsSeed(self, cx);
    Listener navigate = Listen(cx, &MsNavigate);

    component::MessageScrollerState* main = MsState(cx, self->scroller);
    bool customScrolledUp = false;
    if (auto* st = MsState(cx, self->customScroller)) {
        customScrolledUp = st->IsScrolledUp();
    }
    bool applicationScrolledUp = false;
    if (auto* st = MsState(cx, self->applicationScroller)) {
        applicationScrolledUp = st->IsScrolledUp();
    }
    bool empty = self->emptyMessages.len == 0;
    bool streamHasResponse = self->streamMessages
                                 .len > kMsInitialStreamMessageCount;

    El* page = Div(a)->FlexCol()->W(kFill)->Gap(16);

    // ── Conversation ──
    El* conversation = MsSection(
        cx, "Conversation",
        "Scroll upward, append a row, jump to unread, or prepend history to "
        "exercise each behavior.",
        12);
    StorySectionAdd(
        conversation,
        Div(a)
            ->FlexRow()
            ->ItemsCenter()
            ->Gap(8)
            ->Child(MsButton(cx, "message-scroller-append", "Append",
                             Listen(cx, &MsAppendMessage)))
            ->Child(MsButton(cx, "message-scroller-prepend", "Prepend history",
                             Listen(cx, &MsPrependHistory)))
            ->Child(MsButton(cx, "message-scroller-unread", "Scroll to unread",
                             Listen(cx, &MsScrollToUnread))));

    El* frame = Div(a)
                    ->FlexCol()
                    ->W(kMsFrameW)
                    ->MaxW(kFill)
                    ->H(kMsFrameH)
                    ->ClipX()
                    ->ClipY()
                    ->Radius(ThemeRadius4xl(th))
                    ->Border(1, th.border)
                    ->Bg(th.tokens.background)
                    ->Fg(th.foreground);
    frame->Child(
        Div(a)
            ->FlexRow()
            ->W(kFill)
            ->ItemsStart()
            ->JustifyBetween()
            ->Gap(8)
            ->Pad(20)
            ->BorderB(1, th.border)
            ->Child(Div(a)
                        ->FlexCol()
                        ->Gap(4)
                        ->Child(Div(a)->Semibold()->Child(
                            TextEl(a, StrL("New Chat"))))
                        ->Child(Div(a)
                                    ->Font(14)
                                    ->Fg(th.mutedFg)
                                    ->Child(TextEl(
                                        a, StrL("How can I help you today?")))))
            ->Child(component::Button::New(cx, StrL("message-scroller-reset"))
                        ->Outline()
                        ->Icon(IconName::RotateCw)
                        ->Rounded(th.radiusFull)
                        ->Tooltip(StrL("Reset conversation"))
                        ->OnClick(Listen(cx, &MsReset))
                        ->IntoEl()));
    Style listPad = {};
    listPad.pad = Edges::New(20, 20, 20, 20);
    frame->Child(component::MessageScroller::New(
                     cx, StrL("message-scroller-demo"), self->scroller, &MsRow,
                     MsRowsOf(cx, &self->messages, self->unreadIndex))
                     ->H(kMsFrameListH)
                     ->WithListStyle(listPad, StyleFieldPad)
                     ->WithBottomFade(th.background)
                     ->IntoEl());
    frame->Child(Div(a)->W(kFill)->PadX(20)->PadB(20)->Child(
        Div(a)
            ->FlexCol()
            ->W(kFill)
            ->Gap(4)
            ->Pad(8)
            ->Radius(th.radius * 2.f)
            ->Bg(th.tokens.muted)
            ->Child(component::Input::New(cx,
                                          StrL("message-scroller-"
                                               "composer"),
                                          &self->composer)
                        ->Appearance(false)
                        ->IntoEl())
            ->Child(Div(a)->FlexRow()->W(kFill)->JustifyEnd()->Child(
                component::Button::New(cx, StrL("message-scroller-send"))
                    ->Primary()
                    ->Icon(IconName::ArrowUp)
                    ->Rounded(th.radiusFull)
                    ->Tooltip(StrL("Send"))
                    ->OnClick(Listen(cx, &MsSend))
                    ->IntoEl()))));
    StorySectionAdd(conversation, frame);
    StorySectionAdd(
        conversation,
        Div(a)
            ->Font(12)
            ->Fg(th.mutedFg)
            ->Child(TextEl(
                a, StoryFmt(cx, "Following tail: %s · Scrolled up: %s",
                            main && main->IsFollowingTail() ? "true" : "false",
                            main && main->IsScrolledUp() ? "true" : "false"))));
    page->Child(conversation);

    // ── Streaming responses ──
    El* streaming = MsSection(
        cx, "Streaming responses",
        "Append one assistant response, grow its text progressively, and "
        "remeasure only that row.",
        12);
    StorySectionAdd(
        streaming,
        Div(a)
            ->FlexRow()
            ->ItemsCenter()
            ->FlexWrap()
            ->Gap(8)
            ->Child(component::Button::New(
                        cx, StrL("message-scroller-start-stream"))
                        ->Label(StrL("Stream response"))
                        ->Disabled(self->streaming)
                        ->OnClick(Listen(cx, &MsStartStream))
                        ->IntoEl())
            ->Child(
                component::Button::New(cx, StrL("message-scroller-stop-stream"))
                    ->Outline()
                    ->Label(StrL("Stop"))
                    ->Disabled(!self->streaming)
                    ->OnClick(Listen(cx, &MsStopStream))
                    ->IntoEl())
            ->Child(component::Button::New(
                        cx, StrL("message-scroller-expand-response"))
                        ->Label(StrL("Expand response"))
                        ->Disabled(self->streaming || !streamHasResponse)
                        ->OnClick(Listen(cx, &MsExpandStream))
                        ->IntoEl())
            ->Child(component::Button::New(
                        cx, StrL("message-scroller-reset-stream"))
                        ->Ghost()
                        ->Label(StrL("Reset"))
                        ->OnClick(Listen(cx, &MsResetStream))
                        ->IntoEl()));
    Style previewPad = {};
    previewPad.pad = Edges::New(16, 16, 16, 16);
    StorySectionAdd(
        streaming,
        MsPreviewFrame(cx, component::MessageScroller::New(
                               cx, StrL("message-scroller-streaming"),
                               self->streamScroller, &MsRow,
                               MsRowsOf(cx, &self->streamMessages, -1))
                               ->H(kMsPreviewH - 2)
                               ->WithListStyle(previewPad, StyleFieldPad)
                               ->WithBottomFade(th.background)
                               ->IntoEl()));
    StorySectionAdd(
        streaming,
        Div(a)
            ->Font(12)
            ->Fg(th.mutedFg)
            ->Child(TextEl(
                a,
                self->streaming
                    ? StrL("Streaming · existing row is remeasured as each "
                           "token arrives")
                    : StrL("Idle · scroll upward before streaming to preserve "
                           "reading position"))));
    page->Child(streaming);

    // ── Loading earlier messages ──
    El* history = MsSection(
        cx, "Loading earlier messages",
        "Prepend history without disturbing the currently visible message "
        "anchor.",
        12);
    StorySectionAdd(
        history,
        Div(a)
            ->FlexRow()
            ->ItemsCenter()
            ->Gap(8)
            ->Child(MsButton(cx, "message-scroller-load-earlier",
                             "Load five earlier", Listen(cx, &MsLoadEarlier)))
            ->Child(component::Button::New(
                        cx, StrL("message-scroller-history-start"))
                        ->Outline()
                        ->Label(StrL("Scroll to oldest"))
                        ->OnClick(ListenerArg(navigate, MsNavHistoryOldest))
                        ->IntoEl()));
    StorySectionAdd(
        history,
        MsPreviewFrame(
            cx, component::MessageScroller::New(
                    cx, StrL("message-scroller-history"), self->historyScroller,
                    &MsRow, MsRowsOf(cx, &self->historyMessages, -1))
                    ->H(kMsPreviewH - 2)
                    ->WithListStyle(previewPad, StyleFieldPad)
                    ->WithBottomFade(th.background)
                    ->IntoEl()));
    page->Child(history);

    // ── Jumping to messages ──
    El* jumping = MsSection(
        cx, "Jumping to messages",
        "Applications resolve stable message IDs to their current row "
        "indices.",
        12);
    StorySectionAdd(
        jumping, Div(a)
                     ->FlexRow()
                     ->ItemsCenter()
                     ->Gap(8)
                     ->Child(component::Button::New(
                                 cx, StrL("message-scroller-first-message"))
                                 ->Label(StrL("First message"))
                                 ->OnClick(ListenerArg(navigate, MsNavFirst))
                                 ->IntoEl())
                     ->Child(component::Button::New(
                                 cx, StrL("message-scroller-middle-message"))
                                 ->Label(StrL("Message 9"))
                                 ->OnClick(ListenerArg(navigate, MsNavMiddle))
                                 ->IntoEl())
                     ->Child(component::Button::New(
                                 cx, StrL("message-scroller-last-message"))
                                 ->Outline()
                                 ->Label(StrL("Latest"))
                                 ->OnClick(ListenerArg(navigate, MsNavLatest))
                                 ->IntoEl()));
    StorySectionAdd(
        jumping,
        MsPreviewFrame(cx, component::MessageScroller::New(
                               cx, StrL("message-scroller-navigation"),
                               self->navigationScroller, &MsRow,
                               MsRowsOf(cx, &self->previewMessages, -1))
                               ->H(kMsPreviewH - 2)
                               ->WithListStyle(previewPad, StyleFieldPad)
                               ->WithBottomFade(th.background)
                               ->IntoEl()));
    page->Child(jumping);

    // ── Empty conversation ──
    El* emptySection = MsSection(
        cx, "Empty conversation",
        "The application owns empty states and switches to a virtual list "
        "when data arrives.",
        12);
    StorySectionAdd(emptySection,
                    MsButton(cx, "message-scroller-toggle-empty",
                             empty ? "Add first message" : "Clear conversation",
                             Listen(cx, &MsToggleEmpty)));
    El* emptyFrame = Div(a)
                         ->W(kMsPreviewW)
                         ->MaxW(kFill)
                         // h(rems(14.))
                         ->H(224)
                         ->ClipX()
                         ->ClipY()
                         ->Radius(ThemeRadius2xl(th))
                         ->Border(1, th.border)
                         ->Bg(th.tokens.background);
    if (empty) {
        emptyFrame->Child(
            Div(a)
                ->FlexCol()
                ->SizeFull()
                ->ItemsCenter()
                ->JustifyCenter()
                ->Gap(4)
                ->Child(Div(a)->Semibold()->Child(
                    TextEl(a, StrL("No messages yet"))))
                ->Child(
                    Div(a)
                        ->Font(14)
                        ->Fg(th.mutedFg)
                        ->Child(TextEl(
                            a,
                            StrL(
                                "Send a message to start the conversation")))));
    } else {
        emptyFrame
            ->Child(component::MessageScroller::New(
                        cx, StrL("message-scroller-empty"), self->emptyScroller,
                        &MsRow, MsRowsOf(cx, &self->emptyMessages, -1))
                        ->H(222)
                        ->WithListStyle(previewPad, StyleFieldPad)
                        ->WithBottomFade(th.background)
                        ->IntoEl());
    }
    StorySectionAdd(emptySection, emptyFrame);
    page->Child(emptySection);

    // ── Custom jump button ──
    El* custom = MsSection(
        cx, "Custom jump button",
        "Change the built-in button, transition, tooltip, list spacing, and "
        "row styles.",
        12);
    StorySectionAdd(
        custom,
        Div(a)
            ->FlexRow()
            ->ItemsCenter()
            ->Gap(8)
            ->Child(component::Button::New(
                        cx, StrL("message-scroller-show-custom-jump"))
                        ->Label(StrL("Reveal jump button"))
                        ->OnClick(ListenerArg(navigate, MsNavCustomTop))
                        ->IntoEl())
            ->Child(Div(a)
                        ->Font(14)
                        ->Fg(th.mutedFg)
                        ->Child(TextEl(a, customScrolledUp ? StrL("Visible")
                                                           : StrL("Hidden")))));
    Style customContent = {};
    customContent.bg = Background(th.background);
    Style customRow = {};
    customRow.pad = Edges::New(0, 0, 0, 16);
    Style customJump = {};
    customJump.radius = th.radiusLg;
    StorySectionAdd(
        custom,
        MsPreviewFrame(
            cx, component::MessageScroller::New(
                    cx, StrL("message-scroller-custom-control"),
                    self->customScroller, &MsRow,
                    MsRowsOf(cx, &self->previewMessages, -1))
                    ->H(kMsPreviewH - 2)
                    ->WithContentStyle(customContent, StyleFieldBg)
                    ->WithListStyle(previewPad, StyleFieldPad)
                    ->WithRowStyle(customRow, StyleFieldPad)
                    ->WithJumpButtonLabel(StrL("Return to the latest message"))
                    ->WithJumpButtonRenderer(&MsCustomJumpButton)
                    ->WithJumpButtonStyle(customJump, StyleFieldRadius)
                    ->WithJumpButtonTransition(350)
                    ->WithBottomFade(th.background)
                    ->IntoEl()));
    page->Child(custom);

    // ── Application-owned controls ──
    El* application = MsSection(
        cx, "Application-owned controls",
        "Disable built-in chrome and place the jump action wherever the "
        "product needs it.",
        12);
    StorySectionAdd(
        application,
        Div(a)
            ->FlexRow()
            ->ItemsCenter()
            ->Gap(8)
            ->Child(component::Button::New(
                        cx, StrL("message-scroller-application-scroll-up"))
                        ->Label(StrL("Scroll to first"))
                        ->OnClick(ListenerArg(navigate, MsNavApplicationTop))
                        ->IntoEl())
            ->Child(component::Button::New(
                        cx, StrL("message-scroller-application-latest"))
                        ->Outline()
                        ->Label(StrL("Jump to latest"))
                        ->Disabled(!applicationScrolledUp)
                        ->OnClick(ListenerArg(navigate, MsNavApplicationLatest))
                        ->IntoEl()));
    StorySectionAdd(
        application,
        MsPreviewFrame(cx, component::MessageScroller::New(
                               cx,
                               StrL("message-scroller-application-"
                                    "controls"),
                               self->applicationScroller, &MsRow,
                               MsRowsOf(cx, &self->previewMessages, -1))
                               ->H(kMsPreviewH - 2)
                               ->JumpButton(false)
                               ->Scrollbar(false)
                               ->WithListStyle(previewPad, StyleFieldPad)
                               ->WithBottomFade(th.background)
                               ->IntoEl()));
    page->Child(application);
    return page;
}

STORY_PAGE(StoryMessageScroller, MessageScrollerStory);
