#define GPUI_INCLUDE_PRIVATE_API 1
#include "gpui.h"

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
        // Git's patch is limited by the length of a Str, not by the
        // scripting API's capture budget.
        options.maxOutputBytes = INT32_MAX - 1;
        shell::ProcessOutput result;
        Str failure;
        bool ran = shell::ProcessRunBounded(
            StrL("git"), args, count + 3, nullptr, &result, &failure, &options);
        if (ran && result.code == 0) {
            *out = result.out;
            result.out = {};
        } else if (!quiet || !ran) {
            Str detail = len(failure) ? failure : StrTrim(result.err);
            error =
                StrDup(len(detail) ? detail : StrL("Git returned an error."));
        }
        bool ok = ran && result.code == 0;
        result.Free();
        StrFree(failure);
        return ok;
    }

    bool ParseHistory() {
        int at = 0;
        while (at < len(log)) {
            Str fields[5];
            for (Str& field : fields) {
                const char* start = log.s + at;
                const char* nul = (const char*)memchr(start, 0, len(log) - at);
                if (!nul) return false;
                field = Str(start, (int)(nul - start));
                at += len(field) + 1;
            }
            if ((len(fields[0]) != 40 && len(fields[0]) != 64) ||
                !ObjectId(fields[0], 40))
                return false;
            if (!VecAppend(commits, TigCommit{fields[0], fields[1], fields[2],
                                              fields[3], fields[4]}))
                return false;
        }
        return true;
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
        if (Git(args, dimofi(args), &log) && !ParseHistory()) {
            error = StrDup(StrL("Git returned an invalid commit record."));
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
            message =
                StrDup(StrTrim(Str(text.s + split + 2, len(text) - split - 2)));
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
            error = StrDup(
                fmt("Git returned an invalid patch at line %d.", parse.line));
        }
        StrFree(text);
    }

    static void Run(TigJob* job) {
        if (job->history)
            job->History();
        else
            job->Details();
    }
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
    "tig::OlderCommit",  "tig::NewerCommit",    "tig::NextFile",
    "tig::PreviousFile", "tig::FocusDiff",      "tig::FocusHistory",
    "tig::Refresh",      "tig::CopyHash",       "tig::ToggleSidebar",
    "tig::NextChange",   "tig::PreviousChange", "tig::Quit",
    "tig::Unified",      "tig::Split",          "tig::Wrap",
    "tig::LineNumbers",  "tig::Words",          "tig::Characters",
    "tig::WholeLines",   "tig::ThreeContext",   "tig::AllContext",
    "tig::Expand",       "tig::Collapse",       "tig::Dark",
    "tig::Light",        "tig::Message",
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
    int initialLine = 0;
    Str log, message, error;
    Vec<TigCommit> commits;
    // The selected slot is owned by diff; every other parsed file lives here.
    Vec<DiffFile*> files;
    Entity<DiffState> diff;
    FocusHandle historyFocus, filesFocus;
    VirtualListScrollHandle historyScroll, filesScroll;
    int commitIx = -1, fileIx = -1;
    uint64_t revision = 0;
    bool started = false, readingHistory = false, readingCommit = false;
    bool sidebar = true, wrap = true, numbers = true, messageOpen = false;

    ~Tig() {
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
        }
        DiffFile* file = File(fileIx, cx);
        if (file) {
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
            if (window && !WindowFocused(window).IsValid()) {
                FocusHandleFocus(window, self->historyFocus);
            }
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
            int index = 0;
            for (int i = 0; i < len(self->files); i++) {
                if (StrEq(self->files[i]->Path(), self->initialFile)) {
                    index = i;
                    break;
                }
            }
            self->SelectFile(&cx, index);
            if (self->initialLine && self->fileIx >= 0 &&
                StrEq(self->File(self->fileIx, &cx)->Path(),
                      self->initialFile)) {
                auto* file = self->File(self->fileIx, &cx);
                auto side =
                    file->LineIndex(DiffSide::Modified, self->initialLine) >= 0
                        ? DiffSide::Modified
                        : DiffSide::Original;
                self->diff.Get(&cx)
                    ->ScrollToLine(DiffLinePosition::New(file->Path(), side,
                                                         self->initialLine),
                                   &cx);
            }
            self->initialFile = {};
            self->initialLine = 0;
        }
        Notify(&cx);
        delete job;
    }

    void Start(Ctx* cx, bool history, Str hash) {
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
        if (!ExecSpawn(MkFunc0(&TigJob::Run, job), MkFunc0(&Finish, job))) {
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
            int next = commitIx + (action == TigOlder ? 1 : -1);
            if (!readingHistory && next >= 0 && next < len(commits))
                SelectCommit(cx, next);
            break;
        }
        case TigNextFile:
            SelectFile(cx, fileIx + 1);
            break;
        case TigPreviousFile:
            SelectFile(cx, fileIx - 1);
            break;
        case TigReadDiff:
            FocusHandleFocus(cx->win, state->focus);
            break;
        case TigHistory:
            sidebar = true;
            FocusHandleFocus(cx->win, historyFocus);
            break;
        case TigRefresh:
            Start(cx, true, Commit() ? Commit()->hash : Str{});
            break;
        case TigCopy:
            if (Commit()) ClipboardSetText(cx->win, Commit()->hash);
            break;
        case TigSidebar:
            sidebar = !sidebar;
            FocusHandleFocus(cx->win, sidebar ? historyFocus : state->focus);
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
            messageOpen = !messageOpen;
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

static Str FileStatus(DiffFileStatus status) {
    switch (status) {
        case DiffFileStatus::Added:
            return StrL("A");
        case DiffFileStatus::Deleted:
            return StrL("D");
        case DiffFileStatus::Renamed:
            return StrL("R");
        case DiffFileStatus::Copied:
            return StrL("C");
        case DiffFileStatus::Conflicted:
            return StrL("U");
        default:
            return StrL("M");
    }
}

El* Tig::FileRow(void* user, Ctx* cx, int index) {
    auto* self = static_cast<Tig*>(user);
    const DiffFile* file = self->File(index, cx);
    if (!file) return Div(cx->a);
    const Theme& th = ThemeNow(cx->app);
    El* label = Div(cx->a)
                    ->FlexRow()
                    ->Flex1()
                    ->MinW(0)
                    ->Gap(8)
                    ->ItemsCenter()
                    ->Child(TextEl(cx->a, FileStatus(file->Status()))
                                ->Mono()
                                ->Font(12)
                                ->Fg(th.mutedFg))
                    ->Child(TextEl(cx->a, file->Path())
                                ->Flex1()
                                ->MinW(0)
                                ->Truncate()
                                ->Font(14))
                    ->Child(TextEl(cx->a, fmt("+%d", file->Additions()))
                                ->Mono()
                                ->Font(12)
                                ->Fg(th.success))
                    ->Child(TextEl(cx->a, fmt("-%d", file->Deletions()))
                                ->Mono()
                                ->Font(12)
                                ->Fg(th.danger));
    return component::ListItem::New(cx, label)
        ->Selected(self->fileIx == index)
        ->SecondarySelected(self->fileIx == index &&
                            cx->win->lastInputKeyboard &&
                            FocusHandleIsFocused(cx->win, self->filesFocus))
        ->AccessibilityLabel(file->Path())
        ->IntoEl(file->Path(),
                 Listen(
                     cx,
                     +[](Tig* view, Ctx* cx, const ClickEvent*, int64_t i) {
                         FocusHandleFocus(cx->win, view->filesFocus);
                         view->SelectFile(cx, (int)i);
                     },
                     index),
                 {})
        ->H(32)
        ->PadX(16)
        ->Tip(file->Path());
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
    if (history) {
        header->Child(Div(cx->a)->Flex1())
            ->Child(TigButton(cx, TigRefresh)
                        ->Label(StrL("Refresh"))
                        ->Tooltip(StrL("Refresh history"))
                        ->Disabled(self->readingHistory)
                        ->IntoEl());
    }
    auto* handle = history ? &self->historyScroll : &self->filesScroll;
    El* list = component::VirtualList::New(cx, count)
                   ->RowH(history ? 48.f : 32.f)
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
            ->KeyContext(history ? StrL("TigHistory") : StrL("TigFiles"))
            ->Role(AccessibilityRole::List)
            ->AriaLabel(history ? StrL("Commit history")
                                : StrL("Changed files")));
}

static El* TigToolbar(Ctx* cx, Tig* self, const DiffState* state) {
    auto* menu = component::PopupMenu::New(cx, StrL("diff-options"));
    menu->MenuWithCheck(StrL("Wrap long lines"), self->wrap)
        ->OnClick(Listen(cx, &ClickTigAction, TigWrap));
    menu->MenuWithCheck(StrL("Line numbers"), self->numbers)
        ->OnClick(Listen(cx, &ClickTigAction, TigNumbers));
    auto* inlineMenu = component::PopupMenu::New(cx, StrL("inline-changes"));
    inlineMenu->Menu(StrL("Words"))
        ->OnClick(Listen(cx, &ClickTigAction, TigWords));
    inlineMenu->Menu(StrL("Characters"))
        ->OnClick(Listen(cx, &ClickTigAction, TigCharacters));
    inlineMenu->Menu(StrL("Whole lines"))
        ->OnClick(Listen(cx, &ClickTigAction, TigWholeLines));
    auto* context = component::PopupMenu::New(cx, StrL("context"));
    context
        ->MenuWithCheck(StrL("Three context lines"), state->HasContextLines())
        ->OnClick(Listen(cx, &ClickTigAction, TigThreeContext));
    context
        ->MenuWithCheck(StrL("All supplied context"), !state->HasContextLines())
        ->OnClick(Listen(cx, &ClickTigAction, TigAllContext));
    context->Menu(StrL("Expand all"))
        ->OnClick(Listen(cx, &ClickTigAction, TigExpand));
    context->Menu(StrL("Collapse all"))
        ->OnClick(Listen(cx, &ClickTigAction, TigCollapse));
    auto* appearance = component::PopupMenu::New(cx, StrL("appearance"));
    appearance->Menu(StrL("Light"))
        ->OnClick(Listen(cx, &ClickTigAction, TigLight));
    appearance->Menu(StrL("Dark"))
        ->OnClick(Listen(cx, &ClickTigAction, TigDark));
    menu->Submenu(StrL("Inline changes"), inlineMenu);
    menu->Submenu(StrL("Context"), context);
    menu->Submenu(StrL("Appearance"), appearance);
    return Div(cx->a)
        ->FlexRow()
        ->ItemsCenter()
        ->Gap(4)
        ->Child(TigButton(cx, TigUnified)
                    ->Label(StrL("Unified"))
                    ->Selected(state->Mode() == DiffMode::Unified)
                    ->IntoEl())
        ->Child(TigButton(cx, TigSplit)
                    ->Label(StrL("Split"))
                    ->Selected(state->Mode() == DiffMode::Split)
                    ->IntoEl())
        ->Child(TigButton(cx, TigPreviousChange)
                    ->Icon(IconName::ArrowUp)
                    ->Tooltip(StrL("Previous change"))
                    ->IntoEl())
        ->Child(TigButton(cx, TigNextChange)
                    ->Icon(IconName::ArrowDown)
                    ->Tooltip(StrL("Next change"))
                    ->IntoEl())
        ->Child(component::DropdownMenu::New(cx, StrL("diff-display"))
                    ->Trigger(
                        component::Button::New(cx, StrL("diff-display-options"))
                            ->Ghost()
                            ->Icon(IconName::Settings)
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
        El* subject = Div(cx->a)->FlexRow()->Gap(4)->ItemsCenter()->Child(
            TextEl(cx->a, commit->Subject())->Font(16)->Medium()->Wrap());
        if (len(self->message)) {
            subject->Child(TigButton(cx, TigMessage)
                               ->Icon(self->messageOpen ? IconName::ChevronUp
                                                        : IconName::ChevronDown)
                               ->Tooltip(self->messageOpen
                                             ? StrL("Hide commit message")
                                             : StrL("Show commit message"))
                               ->IntoEl());
        }
        El* info =
            Div(cx->a)
                ->FlexRow()
                ->Gap(8)
                ->ItemsCenter()
                ->Fg(th.mutedFg)
                ->Child(TextEl(cx->a, commit->shortHash)->Mono()->Font(12))
                ->Child(TigButton(cx, TigCopy)
                            ->Icon(IconName::Copy)
                            ->Tooltip(StrL("Copy commit hash"))
                            ->IntoEl())
                ->Child(TextEl(cx->a, commit->author)->Font(12)->Truncate())
                ->Child(TextEl(cx->a, commit->date)->Font(12))
                ->Child(Div(cx->a)->Flex1())
                ->Child(TigToolbar(cx, self, state));
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
    if (self->messageOpen) {
        column->Child(Div(cx->a)
                          ->H(160)
                          ->FlexNone()
                          ->PadX(16)
                          ->PadY(12)
                          ->PathId(StrL("commit-message"))
                          ->ScrollFromPath()
                          ->ScrollY(0.f)
                          ->Bg(th.groupBox)
                          ->BorderB(1, th.border)
                          ->Child(TextView::New(cx, self->message)->IntoEl()));
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
                ->Child(
                    TigButton(cx, TigRefresh)->Label(StrL("Retry"))->IntoEl());
    } else if (self->fileIx < 0) {
        content = Div(cx->a)
                      ->FlexCol()
                      ->SizeFull()
                      ->ItemsCenter()
                      ->JustifyCenter()
                      ->Gap(8)
                      ->Child(TextEl(
                          cx->a, len(self->commits)
                                     ? StrL("This commit has no file changes")
                                     : StrL("This repository has no commits")));
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
    El* navigation =
        Div(cx->a)
            ->SizeFull()
            ->Bg(th.sidebar)
            ->Child(v_resizable(cx, StrL("tig-navigation"))
                        ->Panel(Navigation(cx, self, true), 25 * 16, 12 * 16,
                                100 * 16)
                        ->Grow(Navigation(cx, self, false), 10 * 16)
                        ->IntoEl());
    El* panes = h_resizable(cx, StrL("tig-panes"))
                    ->Panel(navigation, 22 * 16, 18 * 16, 30 * 16)
                    ->Visible(self->sidebar)
                    ->Grow(RenderDiff(cx, self), 38 * 16)
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
    auto* status =
        component::StatusBar::New(cx)
            ->Left(TigButton(cx, TigSidebar)
                       ->Icon(self->sidebar ? IconName::PanelLeftClose
                                            : IconName::PanelLeftOpen)
                       ->Tooltip(self->sidebar ? StrL("Hide sidebar")
                                               : StrL("Show sidebar"))
                       ->IntoEl())
            ->Left(TextEl(cx->a, commitStatus)->Font(12)->Fg(th.mutedFg))
            ->Right(TextEl(cx->a, fileStatus)->Font(12)->Fg(th.mutedFg));
    const DiffState* state = self->diff.Get(cx);
    if (state && state->HasSelectedLines()) {
        DiffLineRange selected = state->SelectedLines();
        Str text = selected.IsSingleSide()
                       ? fmt("%d lines selected",
                             abs(selected.start - selected.end) + 1)
                       : StrL("Source selected");
        status->Right(TextEl(cx->a, text)->Font(12)->Fg(th.mutedFg));
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
    Str path, file, head = StrL("HEAD");
    int line = 0;
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
                if (!ObjectId(head, 7)) {
                    fprintf(stderr,
                            "Commit must be a hexadecimal object ID of 7-64 "
                            "characters.\n");
                    return 2;
                }
            } else if (chooseFile) {
                file = Str(argv[i]);
            } else {
                char* end = nullptr;
                long long number = strtoll(argv[i], &end, 10);
                if (number <= 0 || number > INT_MAX || *end) {
                    fprintf(stderr, "Line must be a positive integer.\n");
                    return 2;
                }
                line = (int)number;
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
    if (!path.s) path = StrL(".");
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
    return KitRunView(StrL("Git history"), 1400, 860, view.id, app, options);
}
