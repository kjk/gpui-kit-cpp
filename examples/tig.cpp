#define GPUI_INCLUDE_PRIVATE_API 1
#include "gpui.h"

#include <errno.h>

// longbridge/gpui-kit examples/tig/src/{repository,app,main}.rs.
using namespace gpui;
using component::DiffFile;
using component::DiffFileStatus;
using component::DiffInlineUnit;
using component::DiffLinePosition;
using component::DiffLineRange;
using component::DiffMode;
using component::DiffParseError;
using component::DiffSide;
using component::DiffState;

static bool ObjectId(Str value, int minLength) {
    if (len(value) < minLength || len(value) > 64) return false;
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static bool GitOutputIsUtf8(Str text) {
    const auto* bytes = (const uint8_t*)text.s;
    for (int at = 0; at < len(text);) {
        int count = bytes[at] < 0x80
                        ? 1
                        : Utf8StrictAt(bytes + at, len(text) - at, nullptr);
        if (count <= 0) return false;
        at += count;
    }
    return true;
}

struct TigCommit {
    Str hash, shortHash, author, date, subject;
    Str Subject() const {
        return len(subject) ? subject : StrL("(No subject)");
    }
};

struct TigJob {
    App* app = nullptr;
    Window* window = nullptr;
    EntityId owner;
    uint64_t revision = 0;
    bool history = false;
    Str path, head, keep, hash;
    Str log, message, error;
    Vec<TigCommit> commits;
    Vec<DiffFile*> files;

    ~TigJob() {
        StrFree(path);
        StrFree(head);
        StrFree(keep);
        StrFree(hash);
        StrFree(log);
        StrFree(message);
        StrFree(error);
        for (auto* file : files) delete file;
    }

    bool Git(const Str* arguments, int count, Str* out, bool quiet = false) {
        Str args[] = {StrL("--no-pager"),
                      StrL("-C"),
                      path,
                      {},
                      {},
                      {},
                      {},
                      {},
                      {},
                      {},
                      {},
                      {},
                      {},
                      {},
                      {},
                      {}};
        if (count > dimofi(args) - 3) return false;
        for (int i = 0; i < count; i++) args[i + 3] = arguments[i];
        shell::ProcessOptions options;
        options.inheritEnvironment = true;
        options.timeoutSeconds = -1;
        // Git's patch is limited by the length of a Str, not by the
        // scripting API's capture budget.
        options.maxOutputBytes = INT32_MAX - 1;
        shell::ProcessOutput result;
        Str failure;
        bool ran = shell::ProcessRunBounded(
            StrL("git"), args, count + 3, nullptr, &result, &failure, &options);
        bool ok = ran && result.code == 0;
        if (ok) {
            ok = GitOutputIsUtf8(result.out);
            if (ok) {
                *out = result.out;
                result.out = {};
            } else {
                error =
                    StrDup(StrL("This Git output isn't UTF-8. Convert the "
                                "source encoding to view it."));
            }
        } else if (!quiet || !ran) {
            Str detail = len(failure) ? failure : StrTrim(result.err);
            error =
                StrDup(len(detail) ? detail : StrL("Git returned an error."));
        }
        result.Free();
        StrFree(failure);
        return ok;
    }

    Str ParseHistory() {
        int end = len(log);
        if (!end) return {};
        if (log.s[end - 1] == '\0') end--;
        int fieldCount = 1;
        for (int i = 0; i < end; i++) {
            if (log.s[i] == '\0') fieldCount++;
        }
        if (fieldCount % 5 != 0)
            return StrL("Git returned an incomplete commit record.");
        int count = fieldCount / 5;
        if (!VecReserve(commits, count))
            return StrL("Could not store Git commit history.");

        int at = 0;
        for (int i = 0; i < count; i++) {
            Str fields[5];
            for (Str& field : fields) {
                const char* start = log.s + at;
                const char* nul = (const char*)memchr(start, 0, end - at);
                if (nul) {
                    field = Str(start, (int)(nul - start));
                    at += len(field) + 1;
                } else {
                    field = Str(start, end - at);
                    at = end;
                }
            }
            if ((len(fields[0]) != 40 && len(fields[0]) != 64) ||
                !ObjectId(fields[0], 40)) {
                VecClear(commits);
                return StrL("Git returned an invalid commit object ID.");
            }
            VecAppend(commits, TigCommit{fields[0], fields[1], fields[2],
                                         fields[3], fields[4]});
        }
        return {};
    }

    void History() {
        Str discarded;
        const Str repo[] = {StrL("rev-parse"), StrL("--git-dir")};
        if (!Git(repo, dimofi(repo), &discarded)) return;
        StrFree(discarded);
        const Str verify[] = {StrL("rev-parse"), StrL("--verify"),
                              StrL("--quiet"), head};
        if (!Git(verify, dimofi(verify), &discarded, true)) {
            if (!error.s && !StrEq(head, "HEAD")) {
                error = StrDup(fmt("Commit %s was not found.", head));
            }
            return;
        }
        StrFree(discarded);
        const Str args[] = {StrL("log"),
                            StrL("--no-show-signature"),
                            StrL("--encoding=UTF-8"),
                            StrL("--date=short"),
                            StrL("-z"),
                            StrL("--max-count=200"),
                            StrL("--format=%H%x00%h%x00%an%x00%ad%x00%s"),
                            head,
                            StrL("--")};
        if (Git(args, dimofi(args), &log)) {
            Str parseError = ParseHistory();
            if (parseError.s) error = StrDup(parseError);
        }
    }

    void Details() {
        const Str body[] = {StrL("show"),
                            StrL("--no-show-signature"),
                            StrL("--encoding=UTF-8"),
                            StrL("--no-patch"),
                            StrL("--format=%B"),
                            hash,
                            StrL("--")};
        Str text;
        if (!Git(body, dimofi(body), &text)) return;
        int split = StrFind(text, StrL("\n\n"));
        if (split >= 0) {
            int start = split + 2;
            int end = len(text);
            // Keep the leading indentation for the Markdown renderer.
            while (end > start) {
                int last = Utf8Prev(text, end);
                uint32_t cp;
                Utf8At(text, last, &cp);
                if (!IsUnicodeWhitespace(cp)) break;
                end = last;
            }
            message = StrDup(Str(text.s + start, end - start));
        }
        StrFree(text);
        const Str patch[] = {StrL("show"),
                             StrL("--no-show-signature"),
                             StrL("--no-color"),
                             StrL("--no-ext-diff"),
                             StrL("--no-textconv"),
                             StrL("--format="),
                             StrL("--first-parent"),
                             StrL("--patch"),
                             StrL("--find-renames"),
                             StrL("--src-prefix=a/"),
                             StrL("--dst-prefix=b/"),
                             hash,
                             StrL("--")};
        if (!Git(patch, dimofi(patch), &text)) return;
        DiffParseError parse;
        if (!DiffFile::Parse(text, &files, &parse)) {
            error =
                StrDup(fmt("patch line %d: %s", parse.Line(), parse.Message()));
        }
        StrFree(text);
    }

    static void Run(TigJob* job) {
        if (job->history)
            job->History();
        else
            job->Details();
    }

    static void Discard(TigJob* job) { delete job; }
};

enum TigAction {
    TigOlder,
    TigNewer,
    TigNextFile,
    TigPreviousFile,
    TigReadDiff,
    TigHistory,
    TigRefresh,
    TigCopy,
    TigSidebar,
    TigNextChange,
    TigPreviousChange,
    TigQuit,
    TigUnified,
    TigSplit,
    TigWrap,
    TigNumbers,
    TigWords,
    TigCharacters,
    TigWholeLines,
    TigThreeContext,
    TigAllContext,
    TigExpand,
    TigCollapse,
    TigDark,
    TigLight,
    TigMessage,
    TigActionCount
};

static const char* const kActions[] = {
    "tig::OlderCommit",     "tig::NewerCommit",     "tig::NextFile",
    "tig::PreviousFile",    "tig::FocusDiff",       "tig::FocusHistory",
    "tig::Refresh",         "tig::CopyHash",        "tig::ToggleSidebar",
    "tig::NextChange",      "tig::PreviousChange",  "tig::Quit",
    "tig::UnifiedMode",     "tig::SplitMode",       "tig::ToggleWrap",
    "tig::ToggleNumbers",   "tig::Words",           "tig::Characters",
    "tig::WholeLines",      "tig::ThreeContext",    "tig::AllContext",
    "tig::ExpandContext",   "tig::CollapseContext", "tig::DarkAppearance",
    "tig::LightAppearance", "tig::ToggleMessage",
};
static_assert(dimofi(kActions) == TigActionCount);

static uint32_t TigKey(TigAction action) {
    return ActionOf(Str(kActions[action]));
}

static void BindTigKeys() {
    const KeyBinding keys[] = {
        {"down", TigKey(TigOlder), "TigHistory"},
        {"j", TigKey(TigOlder), "TigHistory"},
        {"up", TigKey(TigNewer), "TigHistory"},
        {"k", TigKey(TigNewer), "TigHistory"},
        {"down", TigKey(TigNextFile), "TigFiles"},
        {"j", TigKey(TigNextFile), "TigFiles"},
        {"up", TigKey(TigPreviousFile), "TigFiles"},
        {"k", TigKey(TigPreviousFile), "TigFiles"},
        {"enter", TigKey(TigReadDiff), "TigHistory || TigFiles"},
        {"[", TigKey(TigPreviousFile), "Tig"},
        {"]", TigKey(TigNextFile), "Tig"},
        {"escape", TigKey(TigHistory), "Tig"},
        {"secondary-b", TigKey(TigSidebar), "Tig"},
        {"secondary-r", TigKey(TigRefresh), "Tig"},
        {"secondary-shift-c", TigKey(TigCopy), "Tig"},
        {"n", TigKey(TigNextChange), "Tig"},
        {"shift-n", TigKey(TigPreviousChange), "Tig"},
        {"secondary-q", TigKey(TigQuit), "Tig"},
    };
    KeymapBind(keys, dimofi(keys));
}

struct Tig {
    Str path, head = StrL("HEAD"), initialFile;
    size_t initialLine = 0;
    Str log, message, error;
    Vec<TigCommit> commits;
    // The selected slot is owned by diff; every other parsed file lives here.
    Vec<DiffFile*> files;
    Entity<DiffState> diff;
    FocusHandle historyFocus, filesFocus;
    VirtualListScrollHandle historyScroll, filesScroll;
    int commitIx = -1, fileIx = -1;
    uint64_t revision = 0;
    TaskId loadTask = 0;
    float messageScroll = 0;
    bool started = false, readingHistory = false, readingCommit = false;
    bool sidebar = true, wrap = true, numbers = true, messageOpen = false;

    ~Tig() {
        ExecCancel(loadTask);
        StrFree(log);
        StrFree(message);
        StrFree(error);
        for (auto* file : files) delete file;
    }

    const TigCommit* Commit() const {
        return commitIx >= 0 && commitIx < len(commits) ? &commits[commitIx]
                                                        : nullptr;
    }

    DiffFile* File(int index, Ctx* cx) const {
        if (index != fileIx) return files[index];
        auto* state = diff.Get(cx);
        return state && len(state->files) ? state->FileAt(0) : nullptr;
    }

    void SelectFile(Ctx* cx, int index) {
        if (index < 0 || index >= len(files)) return;
        auto* state = diff.Get(cx);
        if (!state) return;
        if (index != fileIx) {
            if (fileIx >= 0) {
                Vec<DiffFile*> prior = state->TakeFiles();
                if (!len(prior)) return;
                files[fileIx] = prior[0];
            }
            DiffFile* next = files[index];
            files[index] = nullptr;
            fileIx = index;
            state->SetFiles(&next, 1, cx);
        } else if (DiffFile* file = File(fileIx, cx)) {
            if (state->IsFileCollapsed(file->Path()))
                state->ToggleFileCollapsed(0, cx);
            state->ScrollToFile(file->Path(), cx);
        }
        VirtualListScrollToItemDeferred(&filesScroll, index,
                                        ScrollStrategy::Top);
        Notify(cx);
    }

    static void Finish(TigJob* job) {
        auto* self = static_cast<Tig*>(EntityGet(job->app, job->owner));
        if (!self || self->revision != job->revision) {
            delete job;
            return;
        }
        Window* window = nullptr;
        for (Window* win : job->app->windows) {
            if (win == job->window) {
                window = win;
                break;
            }
        }
        Ctx cx = {job->app, window, window ? window->frameArena : nullptr,
                  job->owner};
        self->loadTask = 0;
        self->readingCommit = self->readingHistory = false;
        StrFree(self->error);
        self->error = job->error;
        job->error = {};
        if (!self->error.s && job->history) {
            StrFree(self->log);
            self->log = job->log;
            job->log = {};
            self->commits = job->commits;
            int index = len(self->commits) ? 0 : -1;
            for (int i = 0; i < len(self->commits); i++) {
                if (StrEq(self->commits[i].hash, job->keep)) {
                    index = i;
                    break;
                }
            }
            self->SelectCommit(&cx, index);
        } else if (!self->error.s) {
            self->message = job->message;
            job->message = {};
            if (!VecAppendVec(self->files, job->files)) {
                self->error =
                    StrDup(StrL("Couldn't retain the changed files."));
                Notify(&cx);
                delete job;
                return;
            }
            VecClear(job->files);
            Str startupFile = self->initialFile;
            size_t startupLine = self->initialLine;
            self->initialFile = {};
            self->initialLine = 0;
            int index = 0;
            if (startupFile.s) {
                while (index < len(self->files) &&
                       !StrEq(self->files[index]->Path(), startupFile)) {
                    index++;
                }
                if (index == len(self->files)) {
                    self->error = StrDup(fmt(
                        "File %s is not changed in this commit.", startupFile));
                    Notify(&cx);
                    delete job;
                    return;
                }
            }
            self->SelectFile(&cx, index);
            if (startupLine && self->fileIx >= 0) {
                self->diff.Get(&cx)->ScrollToLine(
                    DiffLinePosition::New(startupFile, DiffSide::Modified,
                                          startupLine),
                    &cx);
            }
        }
        Notify(&cx);
        delete job;
    }

    void Start(Ctx* cx, bool history, Str hash) {
        ExecCancel(loadTask);
        auto* job = new TigJob();
        job->app = cx->app;
        job->window = cx->win;
        job->owner = cx->self;
        job->revision = ++revision;
        job->path = StrDup(path);
        job->head = StrDup(head);
        job->history = history;
        if (history)
            job->keep = StrDup(hash);
        else
            job->hash = StrDup(hash);
        readingHistory = history;
        readingCommit = !history;
        StrFree(error);
        error = {};
        loadTask = ExecSpawn(MkFunc0(&TigJob::Run, job), MkFunc0(&Finish, job),
                             MkFunc0(&TigJob::Discard, job));
        if (!loadTask) {
            job->error = StrDup(StrL("Couldn't start Git."));
            Finish(job);
        }
        Notify(cx);
    }

    void SelectCommit(Ctx* cx, int index) {
        commitIx = index;
        fileIx = -1;
        for (auto* file : files) delete file;
        VecClear(files);
        StrFree(message);
        message = {};
        messageOpen = false;
        messageScroll = 0;
        if (auto* state = diff.Get(cx)) state->SetFiles(nullptr, 0, cx);
        if (const auto* commit = Commit()) {
            VirtualListScrollToItemDeferred(&historyScroll, index,
                                            ScrollStrategy::Top);
            Start(cx, false, commit->hash);
        }
    }

    void Action(Ctx* cx, TigAction action);
    static El* CommitRow(void*, Ctx* cx, int index);
    static El* FileRow(void*, Ctx* cx, int index);
    static El* Navigation(Ctx* cx, Tig* self, bool history);
    static El* RenderDiff(Ctx* cx, Tig* self);
    static El* Render(Tig* self, Ctx* cx);
};

void Tig::Action(Ctx* cx, TigAction action) {
    DiffState* state = diff.Get(cx);
    if (!state) return;
    switch (action) {
        case TigOlder:
        case TigNewer: {
            if (readingHistory || !len(commits)) break;
            int next = std::max(0, commitIx) + (action == TigOlder ? 1 : -1);
            next = std::max(0, std::min(next, len(commits) - 1));
            if (next != commitIx) SelectCommit(cx, next);
            break;
        }
        case TigNextFile:
        case TigPreviousFile: {
            if (!len(files)) break;
            int next = std::max(0, fileIx) + (action == TigNextFile ? 1 : -1);
            SelectFile(cx, std::max(0, std::min(next, len(files) - 1)));
            break;
        }
        case TigReadDiff:
            if (!readingHistory && !readingCommit && len(state->files))
                FocusHandleFocus(cx->win, state->focus);
            break;
        case TigHistory:
            sidebar = true;
            FocusHandleFocus(cx->win, historyFocus);
            break;
        case TigRefresh: {
            Str previous = Commit() ? Commit()->hash : Str{};
            SelectCommit(cx, -1);
            VecClear(commits);
            Start(cx, true, previous);
            break;
        }
        case TigCopy:
            if (const auto* commit = Commit()) {
                ClipboardSetText(cx->win, commit->hash);
                WindowPushNotification(cx, StrL("Commit hash copied"));
            }
            break;
        case TigSidebar:
            sidebar = !sidebar;
            if (!sidebar && (FocusHandleIsFocused(cx->win, historyFocus) ||
                             FocusHandleIsFocused(cx->win, filesFocus))) {
                FocusHandleFocus(cx->win, state->focus);
            }
            break;
        case TigNextChange:
            state->NextChange(cx);
            break;
        case TigPreviousChange:
            state->PreviousChange(cx);
            break;
        case TigQuit:
            AppQuitAll(cx->app);
            return;
        case TigUnified:
            state->SetMode(DiffMode::Unified, cx);
            break;
        case TigSplit:
            state->SetMode(DiffMode::Split, cx);
            break;
        case TigWrap:
            wrap = !wrap;
            break;
        case TigNumbers:
            numbers = !numbers;
            break;
        case TigWords:
            state->SetInlineUnit(true, DiffInlineUnit::Word, cx);
            break;
        case TigCharacters:
            state->SetInlineUnit(true, DiffInlineUnit::Character, cx);
            break;
        case TigWholeLines:
            state->SetInlineUnit(false, DiffInlineUnit::Word, cx);
            break;
        case TigThreeContext:
            state->SetContextLines(true, 3, cx);
            break;
        case TigAllContext:
            state->SetContextLines(false, 0, cx);
            break;
        case TigExpand:
            state->ExpandUnchanged(cx);
            break;
        case TigCollapse:
            state->CollapseUnchanged(cx);
            break;
        case TigDark:
            ThemeSet(cx->app, ThemeMode::Dark);
            break;
        case TigLight:
            ThemeSet(cx->app, ThemeMode::Light);
            break;
        case TigMessage:
            if (len(message)) messageOpen = !messageOpen;
            break;
        case TigActionCount:
            break;
    }
    Notify(cx);
}

static void ClickTigAction(Tig* self, Ctx* cx, const ClickEvent*,
                           int64_t action) {
    self->Action(cx, (TigAction)action);
}

static void DispatchTigAction(Tig* self, Ctx* cx, const ActionEvent*,
                              int64_t action) {
    self->Action(cx, (TigAction)action);
}

static void ScrollTigMessage(Tig* self, Ctx* cx, const ScrollEvent* event) {
    if (self->messageScroll == event->offsetY) return;
    self->messageScroll = event->offsetY;
    Notify(cx);
}

static component::Button* TigButton(Ctx* cx, TigAction action) {
    return component::Button::New(cx, Str(kActions[action]))
        ->Ghost()
        ->WithSize(UiSize::Small)
        ->OnClick(Listen(cx, &ClickTigAction, action));
}

El* Tig::CommitRow(void* user, Ctx* cx, int index) {
    auto* self = static_cast<Tig*>(user);
    const TigCommit& commit = self->commits[index];
    const Theme& th = ThemeNow(cx->app);
    El* details = Div(cx->a)
                      ->FlexRow()
                      ->Gap(8)
                      ->Fg(th.mutedFg)
                      ->Child(TextEl(cx->a, commit.shortHash)->Font(12)->Mono())
                      ->Child(TextEl(cx->a, commit.author)
                                  ->Font(12)
                                  ->Flex1()
                                  ->MinW(0)
                                  ->Truncate())
                      ->Child(TextEl(cx->a, commit.date)->Font(12));
    El* label =
        Div(cx->a)
            ->FlexCol()
            ->Flex1()
            ->MinW(0)
            ->Gap(4)
            ->Child(TextEl(cx->a, commit.Subject())->Font(14)->Truncate())
            ->Child(details);
    return component::ListItem::New(cx, label)
        ->Selected(self->commitIx == index)
        ->SecondarySelected(self->commitIx == index &&
                            cx->win->lastInputKeyboard &&
                            FocusHandleIsFocused(cx->win, self->historyFocus))
        ->AccessibilityLabel(
            StrDup(cx->a, fmt("%s %s", commit.shortHash, commit.Subject())))
        ->IntoEl(commit.hash,
                 Listen(
                     cx,
                     +[](Tig* view, Ctx* cx, const ClickEvent*, int64_t i) {
                         FocusHandleFocus(cx->win, view->historyFocus);
                         if (!view->readingHistory && view->commitIx != i)
                             view->SelectCommit(cx, (int)i);
                     },
                     index),
                 {})
        ->H(48)
        ->PadX(16)
        ->Tip(commit.Subject());
}

struct TigFileStatus {
    Str marker;
    Str name;
};

static TigFileStatus FileStatus(DiffFileStatus status) {
    switch (status) {
        case DiffFileStatus::Added:
            return {StrL("A"), StrL("Added")};
        case DiffFileStatus::Deleted:
            return {StrL("D"), StrL("Deleted")};
        case DiffFileStatus::Renamed:
            return {StrL("R"), StrL("Renamed")};
        case DiffFileStatus::Copied:
            return {StrL("C"), StrL("Copied")};
        case DiffFileStatus::Conflicted:
            return {StrL("U"), StrL("Conflicted")};
        case DiffFileStatus::Unchanged:
            return {StrL("-"), StrL("Unchanged")};
        case DiffFileStatus::Modified:
            return {StrL("M"), StrL("Modified")};
    }
    return {StrL("?"), StrL("Unknown")};
}

El* Tig::FileRow(void* user, Ctx* cx, int index) {
    auto* self = static_cast<Tig*>(user);
    const DiffFile* file = self->File(index, cx);
    if (!file) return Div(cx->a);
    const Theme& th = ThemeNow(cx->app);
    Str path = file->Path();
    int nameStart = len(path);
    while (nameStart > 0 && path.s[nameStart - 1] != '/') nameStart--;
    Str name =
        nameStart ? Str(path.s + nameStart, len(path) - nameStart) : path;
    Str directory(path.s, std::max(0, nameStart - 1));
    const TigFileStatus status = FileStatus(file->Status());
    int added = file->Additions(), deleted = file->Deletions();
    bool hasCounts = !file->IsBinary() && (added || deleted);
    El* location = Div(cx->a)
                       ->FlexCol()
                       ->Flex1()
                       ->MinW(0)
                       ->Child(TextEl(cx->a, name)
                                   ->Font(14)
                                   ->LineHeight(20.f / 14)
                                   ->Truncate())
                       ->Child(TextEl(cx->a, directory)
                                   ->H(16)
                                   ->Font(12)
                                   ->LineHeight(16.f / 12)
                                   ->Truncate()
                                   ->Fg(th.mutedFg));
    El* label =
        Div(cx->a)
            ->FlexRow()
            ->WFrac(1)
            ->MinW(0)
            ->Gap(8)
            ->Child(Div(cx->a)
                        ->FlexRow()
                        ->Flex1()
                        ->MinW(0)
                        ->Gap(8)
                        ->Child(IconEl(cx->a, IconName::FileText,
                                       UiIconPx(cx, UiSize::Small))
                                    ->Fg(th.mutedFg))
                        ->Child(location))
            ->Child(TextEl(cx->a, status.marker)
                        ->W(16)
                        ->FlexNone()
                        ->Font(12)
                        ->Fg(th.mutedFg))
            ->Child(TextEl(cx->a, hasCounts ? fmt("+%d", added) : StrL("-"))
                        ->W(48)
                        ->FlexNone()
                        ->TextRight()
                        ->Mono()
                        ->Font(12)
                        ->Fg(added ? th.foreground : th.mutedFg))
            ->Child(TextEl(cx->a, hasCounts ? fmt("-%d", deleted) : StrL("-"))
                        ->W(48)
                        ->FlexNone()
                        ->TextRight()
                        ->Mono()
                        ->Font(12)
                        ->Fg(deleted ? th.foreground : th.mutedFg));
    return component::ListItem::New(cx, label)
        ->Selected(self->fileIx == index)
        ->SecondarySelected(self->fileIx == index &&
                            cx->win->lastInputKeyboard &&
                            FocusHandleIsFocused(cx->win, self->filesFocus))
        ->AccessibilityLabel(StrDup(
            cx->a, fmt("%s · %s · +%d -%d", path, status.name, added, deleted)))
        ->IntoEl(path,
                 Listen(
                     cx,
                     +[](Tig* view, Ctx* cx, const ClickEvent*, int64_t i) {
                         FocusHandleFocus(cx->win, view->filesFocus);
                         view->SelectFile(cx, (int)i);
                     },
                     index),
                 {})
        ->H(48)
        ->PadY(4)
        ->PadX(16)
        ->MinW(0)
        ->Tip(StrDup(cx->a,
                     fmt("%s · %s%s", path, status.name,
                         file->IsBinary() ? StrL(" · Binary") : StrL(""))));
}

El* Tig::Navigation(Ctx* cx, Tig* self, bool history) {
    const Theme& th = ThemeNow(cx->app);
    int count = history ? len(self->commits) : len(self->files);
    El* header =
        Div(cx->a)
            ->FlexRow()
            ->FlexNone()
            ->H(40)
            ->PadX(16)
            ->Gap(8)
            ->ItemsCenter()
            ->BorderB(1, th.border)
            ->Child(
                TextEl(cx->a, history ? StrL("Commits") : StrL("Changed files"))
                    ->Font(14)
                    ->Medium())
            ->Child(TextEl(cx->a, fmt("%d", count))->Font(12)->Fg(th.mutedFg));
    header->Child(Div(cx->a)->Flex1());
    if (history) {
        header->Child(TigButton(cx, TigRefresh)
                          ->Label(StrL("Refresh"))
                          ->TooltipWithAction(StrL("Refresh history"),
                                              TigKey(TigRefresh), "Tig")
                          ->Disabled(self->readingHistory)
                          ->IntoEl());
    } else {
        header->Child(
            Div(cx->a)
                ->FlexRow()
                ->Gap(4)
                ->Child(TigButton(cx, TigPreviousFile)
                            ->Icon(IconName::ChevronLeft)
                            ->AccessibilityLabel(StrL("Previous file"))
                            ->TooltipWithAction(StrL("Previous file"),
                                                TigKey(TigPreviousFile), "Tig")
                            ->Disabled(self->fileIx <= 0)
                            ->IntoEl())
                ->Child(TigButton(cx, TigNextFile)
                            ->Icon(IconName::ChevronRight)
                            ->AccessibilityLabel(StrL("Next file"))
                            ->TooltipWithAction(StrL("Next file"),
                                                TigKey(TigNextFile), "Tig")
                            ->Disabled(self->fileIx < 0 ||
                                       self->fileIx + 1 >= count)
                            ->IntoEl()));
    }
    auto* handle = history ? &self->historyScroll : &self->filesScroll;
    El* list = component::VirtualList::New(cx, count)
                   ->Id(history ? StrL("commits") : StrL("files"))
                   ->RowH(48)
                   ->Axis(ScrollAxis::Vertical)
                   ->Handle(handle)
                   ->Row(history ? &CommitRow : &FileRow, self)
                   ->IntoEl();
    return Div(cx->a)->FlexCol()->SizeFull()->MinH(0)->Child(header)->Child(
        Div(cx->a)
            ->Flex1()
            ->MinH(0)
            ->Child(list)
            ->PathId(history ? StrL("history-list") : StrL("file-list"))
            ->TabStop(true)
            ->TrackFocus(history ? self->historyFocus : self->filesFocus)
            ->OnMouseDown(Listen(
                cx,
                +[](Tig* view, Ctx* cx, const MouseDownEvent* event,
                    int64_t history) {
                    if (event->button != MouseButton::Left) return;
                    FocusHandleFocus(cx->win, history ? view->historyFocus
                                                      : view->filesFocus);
                    Notify(cx);
                },
                history))
            ->KeyContext(history ? StrL("TigHistory") : StrL("TigFiles"))
            ->Role(AccessibilityRole::List)
            ->AriaLabel(history ? StrL("Commit history")
                                : StrL("Changed files")));
}

static El* TigToolbar(Ctx* cx, Tig* self, const DiffState* state) {
    const DiffFile* file = len(state->files) ? state->FileAt(0) : nullptr;
    bool available =
        file && !file->IsBinary() && (file->Additions() || file->Deletions());
    bool dark = ThemeGet(cx->app) == ThemeMode::Dark;
    auto* appearance = component::PopupMenu::New(cx, StrL("appearance"));
    appearance->MenuWithCheck(StrL("Light"), !dark)->Action(TigKey(TigLight));
    appearance->MenuWithCheck(StrL("Dark"), dark)->Action(TigKey(TigDark));
    auto* menu = component::PopupMenu::New(cx, StrL("diff-options"))
                     ->ActionContext("Tig");
    menu->MenuWithCheck(StrL("Unified"), state->Mode() == DiffMode::Unified)
        ->Action(TigKey(TigUnified));
    menu->MenuWithCheck(StrL("Split"), state->Mode() == DiffMode::Split)
        ->Action(TigKey(TigSplit));
    menu->Separator()->Submenu(StrL("Appearance"), appearance)->Separator();
    menu->MenuWithCheck(StrL("Wrap long lines"), self->wrap)
        ->Action(TigKey(TigWrap));
    menu->MenuWithCheck(StrL("Line numbers"), self->numbers)
        ->Action(TigKey(TigNumbers));
    menu->Separator();
    auto* inlineMenu = component::PopupMenu::New(cx, StrL("inline-changes"));
    inlineMenu
        ->MenuWithCheck(StrL("Words"),
                        state->HasInlineUnit() &&
                            state->InlineUnit() == DiffInlineUnit::Word)
        ->Action(TigKey(TigWords));
    inlineMenu
        ->MenuWithCheck(StrL("Characters"),
                        state->HasInlineUnit() &&
                            state->InlineUnit() == DiffInlineUnit::Character)
        ->Action(TigKey(TigCharacters));
    inlineMenu->MenuWithCheck(StrL("Whole lines"), !state->HasInlineUnit())
        ->Action(TigKey(TigWholeLines));
    auto* context = component::PopupMenu::New(cx, StrL("context"));
    context
        ->MenuWithCheck(StrL("Three lines"),
                        state->HasContextLines() && state->ContextLines() == 3)
        ->Action(TigKey(TigThreeContext));
    context
        ->MenuWithCheck(StrL("All supplied lines"), !state->HasContextLines())
        ->Action(TigKey(TigAllContext));
    context->Separator()
        ->MenuWithAction(StrL("Expand unchanged lines"), TigKey(TigExpand));
    context
        ->MenuWithAction(StrL("Collapse unchanged lines"), TigKey(TigCollapse));
    menu->Submenu(StrL("Inline changes"), inlineMenu);
    menu->Submenu(StrL("Context"), context);
    return Div(cx->a)
        ->FlexRow()
        ->FlexNone()
        ->ItemsCenter()
        ->Gap(8)
        ->Child(
            Div(cx->a)
                ->FlexRow()
                ->Gap(4)
                ->Child(TigButton(cx, TigPreviousChange)
                            ->Icon(IconName::ArrowUp)
                            ->AccessibilityLabel(StrL("Previous change"))
                            ->TooltipWithAction(StrL("Previous change"),
                                                TigKey(TigPreviousChange),
                                                "Tig")
                            ->Disabled(!available)
                            ->IntoEl())
                ->Child(TigButton(cx, TigNextChange)
                            ->Icon(IconName::ArrowDown)
                            ->AccessibilityLabel(StrL("Next change"))
                            ->TooltipWithAction(StrL("Next change"),
                                                TigKey(TigNextChange), "Tig")
                            ->Disabled(!available)
                            ->IntoEl()))
        ->Child(component::DropdownMenu::New(cx, StrL("diff-display"))
                    ->AnchorRight()
                    ->Trigger(
                        component::Button::New(cx, StrL("diff-display-options"))
                            ->Outline()
                            ->WithSize(UiSize::Small)
                            ->Icon(IconName::Settings2)
                            ->AccessibilityLabel(StrL("Diff display options"))
                            ->Tooltip(StrL("Diff display options"))
                            ->IntoEl())
                    ->Menu(menu)
                    ->IntoEl());
}

static Str RepositoryName(Str path) {
    int end = len(path);
    while (end > 0 && (path.s[end - 1] == '/' || path.s[end - 1] == '\\'))
        end--;
    int start = end;
    while (start > 0 && path.s[start - 1] != '/' && path.s[start - 1] != '\\')
        start--;
    if (start == end || StrEq(path, ".")) return StrL("Repository");
    return Str(path.s + start, end - start);
}

El* Tig::RenderDiff(Ctx* cx, Tig* self) {
    const Theme& th = ThemeNow(cx->app);
    auto* state = self->diff.Get(cx);
    El* column = Div(cx->a)->FlexCol()->SizeFull()->MinW(0)->MinH(0);
    if (const TigCommit* commit = self->Commit()) {
        El* subject = Div(cx->a)->FlexRow()->Gap(4)->ItemsStart()->Child(
            TextEl(cx->a, commit->Subject())
                ->MinW(0)
                ->Font(16)
                ->Medium()
                ->Wrap());
        if (len(self->message)) {
            Str messageAction = self->messageOpen ? StrL("Hide commit message")
                                                  : StrL("Show commit message");
            subject->Child(TigButton(cx, TigMessage)
                               ->Icon(self->messageOpen ? IconName::ChevronUp
                                                        : IconName::ChevronDown)
                               ->Toggled(self->messageOpen)
                               ->AccessibilityLabel(messageAction)
                               ->Tooltip(messageAction)
                               ->IntoEl()
                               ->FlexNone());
        }
        El* info =
            Div(cx->a)
                ->FlexRow()
                ->MinW(0)
                ->Gap(8)
                ->ItemsCenter()
                ->Fg(th.mutedFg)
                ->Child(TextEl(cx->a, commit->shortHash)
                            ->FlexNone()
                            ->Mono()
                            ->Font(12))
                ->Child(TigButton(cx, TigCopy)
                            ->Icon(IconName::Copy)
                            ->AccessibilityLabel(StrL("Copy commit hash"))
                            ->TooltipWithAction(StrL("Copy commit hash"),
                                                TigKey(TigCopy), "Tig")
                            ->IntoEl())
                ->Child(TextEl(cx->a, commit->author)
                            ->MinW(0)
                            ->Font(12)
                            ->Truncate())
                ->Child(TextEl(cx->a, StrL("·"))->FlexNone()->Font(12))
                ->Child(TextEl(cx->a, commit->date)->FlexNone()->Font(12))
                ->Child(Div(cx->a)->Flex1());
        if (!self->readingCommit) {
            size_t additions = 0;
            size_t deletions = 0;
            for (int i = 0; i < len(self->files); i++) {
                if (const auto* file = self->File(i, cx)) {
                    additions += (size_t)file->Additions();
                    deletions += (size_t)file->Deletions();
                }
            }
            info->Child(
                Div(cx->a)
                    ->FlexRow()
                    ->FlexNone()
                    ->Gap(8)
                    ->Child(TextEl(cx->a, fmt("%d files", len(self->files)))
                                ->Font(12))
                    ->Child(
                        TextEl(cx->a, fmt("+%zu -%zu", additions, deletions))
                            ->Font(12)
                            ->Mono()));
        }
        info->Child(TigToolbar(cx, self, state));
        column->Child(Div(cx->a)
                          ->FlexCol()
                          ->FlexNone()
                          ->Gap(4)
                          ->PadX(16)
                          ->PadY(12)
                          ->BorderB(1, th.border)
                          ->Child(subject)
                          ->Child(info));
    }
    if (self->messageOpen && !self->readingCommit && !self->readingHistory) {
        El* body = Div(cx->a)
                       ->FlexCol()
                       ->FlexNone()
                       ->PadX(Rems(cx, 1.f))
                       ->PadY(Rems(cx, 0.75f))
                       ->Child(TextView::New(cx, self->message)
                                   ->Font(Rems(cx, 0.875f))
                                   ->Selectable()
                                   ->IntoEl()
                                   ->PathId(StrL("commit-message-content")));
        El* area = Div(cx->a)
                       ->FlexCol()
                       ->H(Rems(cx, 10.f))
                       ->FlexNone()
                       ->MinH(0)
                       ->Bg(th.groupBox)
                       ->BorderB(1, th.border)
                       ->Child(body);
        column->Child(component::ScrollableElement::VerticalScrollbar(
            cx, area, StrL("commit-message"), self->messageScroll,
            Listen(cx, &ScrollTigMessage)));
    }
    El* content;
    if (self->readingHistory || self->readingCommit) {
        content = Div(cx->a)
                      ->FlexCol()
                      ->SizeFull()
                      ->ItemsCenter()
                      ->JustifyCenter()
                      ->Gap(12)
                      ->Child(component::Spinner::New(cx)->IntoEl())
                      ->Child(TextEl(cx->a, self->readingHistory
                                                ? StrL("Reading history…")
                                                : StrL("Reading commit…")));
    } else if (self->error.s) {
        content =
            Div(cx->a)
                ->FlexCol()
                ->SizeFull()
                ->Pad(24)
                ->Gap(12)
                ->Child(TextEl(cx->a, StrL("Couldn't read Git data"))->Medium())
                ->Child(TextEl(cx->a, self->error)
                            ->Font(14)
                            ->Fg(th.mutedFg)
                            ->Wrap())
                ->Child(component::Button::New(cx, StrL("retry"))
                            ->Label(StrL("Retry"))
                            ->OnClick(Listen(cx, &ClickTigAction, TigRefresh))
                            ->IntoEl()
                            ->SelfStart());
    } else if (!len(state->files)) {
        bool emptyHistory = !len(self->commits);
        content =
            Div(cx->a)
                ->FlexCol()
                ->SizeFull()
                ->ItemsCenter()
                ->JustifyCenter()
                ->Gap(8)
                ->Child(TextEl(cx->a,
                               emptyHistory
                                   ? StrL("This repository has no commits")
                                   : StrL("This commit has no file changes")))
                ->Child(
                    TextEl(
                        cx->a,
                        emptyHistory
                            ? StrL("Create a commit, then refresh the history.")
                            : StrL("Select another commit to inspect its "
                                   "changes."))
                        ->Font(14)
                        ->Fg(th.mutedFg));
    } else {
        content = component::Diff::New(cx, self->diff)
                      ->LineNumber(self->numbers)
                      ->SoftWrap(self->wrap)
                      ->IntoEl();
    }
    return column
        ->Child(Div(cx->a)->Flex1()->MinW(0)->MinH(0)->Pad(12)->Child(content));
}

El* Tig::Render(Tig* self, Ctx* cx) {
    if (!self->started) {
        self->started = true;
        self->diff = EntityNew<DiffState>(cx->app);
        self->diff.Get(cx)->Bind(cx->app, self->diff);
        self->diff.Get(cx)->WithMode(DiffMode::Split);
        self->historyFocus = FocusHandleNew(cx);
        self->filesFocus = FocusHandleNew(cx);
        FocusHandleFocus(cx->win, self->historyFocus);
        self->Start(cx, true, {});
    }
    const Theme& th = ThemeNow(cx->app);
    El* root = Div(cx->a)
                   ->FlexCol()
                   ->SizeFull()
                   ->KeyContext(StrL("Tig"))
                   ->Bg(th.background)
                   ->Fg(th.foreground);
    for (int i = 0; i < TigActionCount; i++) {
        root->OnAction(TigKey((TigAction)i), Listen(cx, &DispatchTigAction, i));
    }
    Str revisionText = fmt("%s history", self->head);
    root->Child(
        component::TitleBar::New(cx)
            ->Child(Div(cx->a)
                        ->FlexRow()
                        ->ItemsCenter()
                        ->Gap(8)
                        ->PadX(8)
                        ->Child(TextEl(cx->a, RepositoryName(self->path))
                                    ->Font(14)
                                    ->Medium()
                                    ->Truncate())
                        ->Child(TextEl(cx->a, revisionText)
                                    ->Font(12)
                                    ->Fg(th.mutedFg)))
            ->IntoEl());
    float rem = WindowRemSize(cx->win);
    El* navigation =
        Div(cx->a)
            ->SizeFull()
            ->Bg(th.sidebar)
            ->Child(v_resizable(cx, StrL("tig-navigation"))
                        ->Panel(Navigation(cx, self, true), 25 * rem, 12 * rem,
                                100 * rem)
                        ->Child(ResizablePanel::New(cx)
                                    ->SizeRange(10 * rem, 100 * rem)
                                    ->Child(Navigation(cx, self, false)))
                        ->IntoEl());
    El* panes = h_resizable(cx, StrL("tig-panes"))
                    ->Panel(navigation, 22 * rem, 18 * rem, 30 * rem)
                    ->Visible(self->sidebar)
                    ->Child(ResizablePanel::New(cx)
                                ->SizeRange(38 * rem, 200 * rem)
                                ->Child(RenderDiff(cx, self)))
                    ->IntoEl();
    root->Child(Div(cx->a)->Flex1()->MinH(0)->Child(panes));
    Str commitStatus =
        self->readingHistory ? StrL("Reading history")
        : !len(self->commits)
            ? StrL("No commits")
            : fmt("Commit %d of %d", self->commitIx + 1, len(self->commits));
    Str fileStatus = self->readingCommit ? StrL("Reading files")
                     : self->fileIx < 0 ? StrL("No file selected")
                                        : fmt("File %d of %d", self->fileIx + 1,
                                              len(self->files));
    Str sidebarAction =
        self->sidebar ? StrL("Hide sidebar") : StrL("Show sidebar");
    auto* status =
        component::StatusBar::New(cx)
            ->Left(TigButton(cx, TigSidebar)
                       ->Icon(self->sidebar ? IconName::PanelLeftClose
                                            : IconName::PanelLeftOpen)
                       ->Toggled(self->sidebar)
                       ->AccessibilityLabel(sidebarAction)
                       ->TooltipWithAction(sidebarAction, TigKey(TigSidebar),
                                           "Tig")
                       ->IntoEl())
            ->Left(TextEl(cx->a, commitStatus)->Font(12)->Fg(th.mutedFg))
            ->Right(TextEl(cx->a, fileStatus)->Font(12)->Fg(th.mutedFg));
    const DiffState* state = self->diff.Get(cx);
    if (state && state->HasSelectedLines()) {
        DiffLineRange selected = state->SelectedLines();
        size_t lo = std::min(selected.start, selected.end);
        size_t hi = std::max(selected.start, selected.end);
        Str text = selected.IsSingleSide()
                       ? fmt("%zu lines selected", hi - lo + 1)
                       : StrL("Source selected");
        status->Right(TextEl(cx->a, text)->Font(12)->Fg(th.mutedFg));
    }
    bool historyFocused = FocusHandleIsFocused(cx->win, self->historyFocus);
    if (historyFocused || FocusHandleIsFocused(cx->win, self->filesFocus)) {
        if (auto* key = component::Kbd::ForAction(
                cx, TigKey(TigReadDiff),
                historyFocused ? "TigHistory" : "TigFiles")) {
            status->Right(Div(cx->a)
                              ->FlexRow()
                              ->ItemsCenter()
                              ->Gap(4)
                              ->Child(key->IntoEl())
                              ->Child(TextEl(cx->a, StrL("Read diff"))
                                          ->Font(12)
                                          ->Fg(th.mutedFg)));
        }
    }
    return root->Child(status->IntoEl());
}

static int CheckTig(Str path, Str head) {
    TigJob job;
    job.path = StrDup(path);
    job.head = StrDup(head);
    job.History();
    if (!job.error.s) {
        printf("%.*s: %d commits in the history window\n", len(path), path.s,
               len(job.commits));
        if (len(job.commits)) {
            const TigCommit& commit = job.commits[0];
            job.hash = StrDup(commit.hash);
            job.Details();
            if (!job.error.s)
                printf("%.*s %.*s: %d changed files\n", len(commit.shortHash),
                       commit.shortHash.s, len(commit.subject),
                       commit.subject.s, len(job.files));
        }
    }
    if (!job.error.s) return 0;
    fprintf(stderr, "%.*s\n", len(job.error), job.error.s);
    return 1;
}

int GpuiMain(int argc, char** argv) {
    constexpr auto usage =
        "Usage: tig [--check] [--commit HASH] [--file PATH] [--line N] "
        "[repository]";
    Str path, file, head;
    size_t line = 0;
    bool check = false;
    for (int i = 1; i < argc; i++) {
        Str option(argv[i]);
        if (StrEq(option, "--help")) {
            printf(
                "%s\nDefaults to the current directory and HEAD. Requires Git "
                "on PATH.\n",
                usage);
            return 0;
        }
        if (StrEq(option, "--check")) {
            check = true;
            continue;
        }
        bool commit = StrEq(option, "--commit");
        bool chooseFile = StrEq(option, "--file");
        if (commit || chooseFile || StrEq(option, "--line")) {
            if (++i == argc) {
                fprintf(stderr, "%s requires a value.\n%s\n", option.s, usage);
                return 2;
            }
            if (commit) {
                head = Str(argv[i]);
            } else if (chooseFile) {
                file = Str(argv[i]);
            } else {
                const char* value = argv[i];
                if (*value == '+') value++;
                char* end = nullptr;
                errno = 0;
                unsigned long long number = strtoull(value, &end, 10);
                if (*value < '0' || *value > '9' || number == 0 ||
                    number > SIZE_MAX || *end || errno == ERANGE) {
                    fprintf(stderr, "Line must be a positive integer.\n");
                    return 2;
                }
                line = (size_t)number;
            }
        } else if (!path.s && option.s[0] != '-') {
            path = option;
        } else {
            fprintf(stderr, "%s\n", usage);
            return 2;
        }
    }
    if (line && !file.s) {
        fprintf(stderr, "--line requires --file.\n");
        return 2;
    }
    if (head.s && !ObjectId(head, 7)) {
        fprintf(stderr,
                "Commit must be a hexadecimal object ID of 7-64 characters.\n");
        return 2;
    }
    if (!head.s) head = StrL("HEAD");
    if (!path.s) path = StrL(".");
    char canonical[kMaxPath];
    if (PlatCanonicalPath(path.s, canonical, dimofi(canonical))) {
        path = Str(canonical);
    }
    if (check) return CheckTig(path, head);
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Dark);
    BindTigKeys();
    auto view = EntityNew<Tig>(app);
    auto* tig = view.Get(app);
    tig->path = path;
    tig->head = head;
    tig->initialFile = file;
    tig->initialLine = line;
    WinOpts options;
    options.clientTitleBar = true;
    options.minSize = {1100, 640};
    return KitRunView(StrL("Git history — GPUI Kit"), 1400, 860, view.id, app,
                      options);
}
