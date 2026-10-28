#include "Story.h"

// crates/story/src/stories/diff_story.rs — the example patches, the
// toolbar, and the pull-request review comment. Soft wrap is stored on
// the diff and not applied; the add-annotation button is not painted.

static const char* kReview =
    "diff --git a/src/retry.rs b/src/retry.rs\n"
    "index 30a471b..604db2a 100644\n"
    "--- a/src/retry.rs\n"
    "+++ b/src/retry.rs\n"
    "@@ -9,7 +9,7 @@ impl Default for RetryPolicy {\n"
    " impl Default for RetryPolicy {\n"
    "     fn default() -> Self {\n"
    "         Self {\n"
    "-            max_attempts: 3,\n"
    "+            max_attempts: 5,\n"
    "             base_delay: Duration::from_millis(100),\n"
    "         }\n"
    "     }\n"
    "@@ -18,7 +18,7 @@ impl RetryPolicy {\n"
    " impl RetryPolicy {\n"
    "     /// Returns the delay before the next attempt.\n"
    "     pub fn delay(&self, attempt: u32) -> Duration {\n"
    "-        self.base_delay * attempt\n"
    "+        self.base_delay * 2_u32.pow(attempt.min(6))\n"
    "     }\n"
    " \n"
    "     /// Whether another request may be attempted.\n"
    "@@ -29,4 +29,4 @@ impl RetryPolicy {\n"
    " \n"
    " pub fn describe(policy: &RetryPolicy) -> String {\n"
    "-    format!(\"{} attempts\", policy.max_attempts)\n"
    "+    format!(\"Up to {} attempts\", policy.max_attempts)\n"
    " }\n";

static const char* kAdded =
    "diff --git a/src/retry.rs b/src/retry.rs\n"
    "new file mode 100644\n"
    "--- /dev/null\n"
    "+++ b/src/retry.rs\n"
    "@@ -0,0 +1,3 @@\n"
    "+pub fn retry_delay(attempt: u32) -> u64 {\n"
    "+    100 * 2_u64.pow(attempt.min(6))\n"
    "+}\n";

static const char* kDeleted =
    "diff --git a/src/legacy_retry.rs b/src/legacy_retry.rs\n"
    "deleted file mode 100644\n"
    "--- a/src/legacy_retry.rs\n"
    "+++ /dev/null\n"
    "@@ -1,3 +0,0 @@\n"
    "-pub fn retry_delay(_: u32) -> u64 {\n"
    "-    1000\n"
    "-}\n";

static const char* kUnicode =
    "--- a/src/greeting.rs\n"
    "+++ b/src/greeting.rs\n"
    "@@ -1,6 +1,6 @@\n"
    "-// Greeting for every visitor\n"
    "+// 为每位访客生成问候\n"
    " fn greeting(name: &str) -> String {\n"
    "-\tformat!(\"Hello, {name} \U0001F44B\")\n"
    "+\tformat!(\"你好，{name} \U0001F30F\")\n"
    " }\n"
    " \n"
    "-// café: e\u0301, 中文，مرحبا\n"
    "+// café: é, 中文，مرحبا\n";

static const char* kRenamed =
    "diff --git a/src/retry.rs b/src/retry_policy.rs\n"
    "similarity index 100%\n"
    "rename from src/retry.rs\n"
    "rename to src/retry_policy.rs\n";

static const char* kNoNewline =
    "--- a/config/review.conf\n"
    "+++ b/config/review.conf\n"
    "@@ -1,2 +1,2 @@\n"
    " enabled=true\n"
    "-limit=20\n"
    "+limit=100\n"
    "\\ No newline at end of file\n";

static const char* kMode =
    "diff --git a/scripts/review.sh b/scripts/review.sh\n"
    "old mode 100644\n"
    "new mode 100755\n";

static const char* kMerge =
    "impl Default for RetryPolicy {\n"
    "    fn default() -> Self {\n"
    "        Self {\n"
    "<<<<<<< HEAD\n"
    "            max_attempts: 5,\n"
    "            base_delay: Duration::from_millis(100),\n"
    "=======\n"
    "            max_attempts: 3,\n"
    "            base_delay: Duration::from_millis(250),\n"
    ">>>>>>> retry-backoff\n"
    "        }\n"
    "    }\n"
    "}\n";

static const char* kSource =
    "use std::time::Duration;\n"
    "\n"
    "/// How often a failed request is retried.\n"
    "pub struct RetryPolicy {\n"
    "    pub max_attempts: u32,\n"
    "    pub base_delay: Duration,\n"
    "}\n"
    "\n"
    "impl RetryPolicy {\n"
    "    /// Returns the delay before the next attempt.\n"
    "    pub fn delay(&self, attempt: u32) -> Duration {\n"
    "        self.base_delay * 2_u32.pow(attempt.min(6))\n"
    "    }\n"
    "}\n";

static const char* kExamples[] = {
    "Pull request", "Added file",     "Deleted file", "Unicode",
    "Long lines",   "Large file",     "Renamed file", "Missing final newline",
    "File mode",    "Merge conflict", "Source file",
};

struct DiffStory {
    StoryToolbarState toolbar;
    Entity<component::DiffState> state;
    bool loaded = false;
    int example = 0;
    // diff_story.rs defaults: tint on, hover and wrap off, words, hunk
    // header, signs.
    bool background = true;
    bool hover = false;
    bool wrap = false;
    int inlineIx = 0;
    int separator = 0;
    int indicator = 0;
    bool commentResolved = false;
    component::DiffAnnotation note = {};
    // A diagnostic on the changed delay line, and the marker that opens it.
    component::DiffDecoration diagnostic = {};
    component::DiffGutterMarker overflow = {};
    bool diagnosticReady = false;

    static component::DiffState* State(DiffStory* self, Ctx* cx) {
        return self->state.Get(cx);
    }
    static void LoadExample(DiffStory* self, Ctx* cx);
    static void OnUnified(DiffStory* self, Ctx* cx, const ClickEvent*) {
        if (component::DiffState* st = State(self, cx)) {
            st->SetMode(component::DiffMode::Unified, cx);
        }
        Notify(cx);
    }
    static void OnSplit(DiffStory* self, Ctx* cx, const ClickEvent*) {
        if (component::DiffState* st = State(self, cx)) {
            st->SetMode(component::DiffMode::Split, cx);
        }
        Notify(cx);
    }
    static void OnPrevious(DiffStory* self, Ctx* cx, const ClickEvent*) {
        if (component::DiffState* st = State(self, cx)) st->PreviousChange(cx);
    }
    static void OnNext(DiffStory* self, Ctx* cx, const ClickEvent*) {
        if (component::DiffState* st = State(self, cx)) st->NextChange(cx);
    }
    static void OnContextThree(DiffStory* self, Ctx* cx, const ClickEvent*) {
        if (component::DiffState* st = State(self, cx)) {
            st->SetContextLines(true, 3, cx);
        }
        Notify(cx);
    }
    static void OnContextAll(DiffStory* self, Ctx* cx, const ClickEvent*) {
        if (component::DiffState* st = State(self, cx)) {
            st->SetContextLines(false, 0, cx);
        }
        Notify(cx);
    }
    static void OnExpand(DiffStory* self, Ctx* cx, const ClickEvent*) {
        if (component::DiffState* st = State(self, cx)) st->ExpandUnchanged(cx);
        Notify(cx);
    }
    static void OnCollapse(DiffStory* self, Ctx* cx, const ClickEvent*) {
        if (component::DiffState* st = State(self, cx)) {
            st->CollapseUnchanged(cx);
        }
        Notify(cx);
    }
    static void OnInline(DiffStory* self, Ctx* cx, const ClickEvent*,
                         int64_t ix) {
        self->inlineIx = (int)ix;
        if (component::DiffState* st = State(self, cx)) {
            bool on = ix != 2;
            component::DiffInlineUnit unit =
                ix == 1 ? component::DiffInlineUnit::Character
                        : component::DiffInlineUnit::Word;
            st->SetInlineUnit(on, unit, cx);
        }
        Notify(cx);
    }
    static void OnSeparator(DiffStory* self, Ctx* cx, const ClickEvent*,
                            int64_t ix) {
        self->separator = (int)ix;
        Notify(cx);
    }
    static void OnIndicator(DiffStory* self, Ctx* cx, const ClickEvent*,
                            int64_t ix) {
        self->indicator = (int)ix;
        Notify(cx);
    }
    static void OnExample(DiffStory* self, Ctx* cx, const ClickEvent*,
                          int64_t ix) {
        if (ix < 0 || ix > 10) ix = 0;
        self->example = (int)ix;
        LoadExample(self, cx);
        Notify(cx);
    }
    static void OnTint(DiffStory* self, Ctx* cx, const ClickEvent*) {
        self->background = !self->background;
        Notify(cx);
    }
    static void OnHover(DiffStory* self, Ctx* cx, const ClickEvent*) {
        self->hover = !self->hover;
        Notify(cx);
    }
    static void OnWrap(DiffStory* self, Ctx* cx, const ClickEvent*) {
        self->wrap = !self->wrap;
        Notify(cx);
    }
    static void OnResolve(DiffStory* self, Ctx* cx, const ClickEvent*) {
        self->commentResolved = !self->commentResolved;
        Notify(cx);
    }
    static void OnOverflow(DiffStory*, Ctx* cx, const ClickEvent*) {
        WindowPushNotification(
            cx, StrL("2_u32.pow(attempt) overflows for attempts above 31"));
    }
    static El* RenderComment(Ctx* cx, const component::DiffAnnotation*,
                             void* user);
    static El* RenderOverflow(Ctx* cx,
                              const component::DiffGutterMarker* marker,
                              void* user);
    static El* Render(DiffStory* self, Ctx* cx);
};

El* DiffStory::RenderOverflow(Ctx* cx,
                              const component::DiffGutterMarker* marker,
                              void*) {
    return component::Button::New(cx, marker->Id())
        ->Ghost()
        ->WithSize(UiSize::XSmall)
        ->Icon(IconName::TriangleAlert)
        ->Tooltip(marker->Label())
        ->OnClick(Listen(cx, &DiffStory::OnOverflow))
        ->IntoEl();
}

El* DiffStory::RenderComment(Ctx* cx, const component::DiffAnnotation*,
                             void* user) {
    DiffStory* self = (DiffStory*)user;
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* who = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    who->Child(TextEl(a, StrL("Alex"))->Font(14)->Medium());
    who->Child(TextEl(a, self->commentResolved ? StrL("Resolved")
                                               : StrL("Review comment"))
                   ->Font(12)
                   ->Fg(th.mutedFg));
    El* head =
        Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween()->Gap(8);
    head->Child(who);
    head->Child(
        component::Button::New(cx, StrL("retry-delay-review"))
            ->Ghost()
            ->WithSize(UiSize::XSmall)
            ->Label(self->commentResolved ? StrL("Reopen") : StrL("Resolve"))
            ->OnClick(Listen(cx, &DiffStory::OnResolve))
            ->IntoEl());
    El* col = Div(a)->FlexCol()->W(kFill)->Gap(8)->Child(head);
    if (!self->commentResolved) {
        col->Child(TextEl(a, StrL("Should callers be able to configure the "
                                  "6.4-second delay cap?"))
                       ->Font(14));
    }
    return col;
}

static void AppendRepeat(StrBuilder& out, const char* piece, int n) {
    for (int i = 0; i < n; i++) out.Append(Str(piece));
}

static Str ExamplePatch(int example) {
    StrBuilder b;
    if (example == 1) {
        b.Append(Str(kAdded));
    } else if (example == 2) {
        b.Append(Str(kDeleted));
    } else if (example == 3) {
        b.Append(Str(kUnicode));
    } else if (example == 4) {
        const char* prefix =
            "const ENDPOINT: &str = \"https://api.example.com/v1/changes?";
        b.Append(
            Str("--- a/src/endpoint.rs\n+++ b/src/endpoint.rs\n@@ -1 +1 "
                "@@\n-"));
        b.Append(Str(prefix));
        AppendRepeat(b, "scope=repository&", 24);
        b.Append(Str("&limit=20\";\n+"));
        b.Append(Str(prefix));
        AppendRepeat(b, "scope=repository&", 24);
        b.Append(Str("&limit=100\";\n"));
    } else if (example == 5) {
        b.Append(
            Str("--- a/config/features.conf\n+++ b/config/features.conf\n"
                "@@ -1,5000 +1,5000 @@\n"));
        for (int line = 1; line <= 5000; line++) {
            char num[4] = {
                (char)('0' + (line / 1000) % 10),
                (char)('0' + (line / 100) % 10),
                (char)('0' + (line / 10) % 10),
                (char)('0' + line % 10),
            };
            Str digits = Str(num, 4);
            if (line == 40 || line == 4960) {
                b.Append(Str("-entry_"));
                b.Append(digits);
                b.Append(Str(" = enabled\n+entry_"));
                b.Append(digits);
                b.Append(Str(" = disabled\n"));
            } else if (line == 2500) {
                b.Append(Str("-entry_"));
                b.Append(digits);
                b.Append(Str(" = enabled\n+entry_"));
                b.Append(digits);
                b.Append(Str(" = pending\n"));
            } else {
                b.Append(Str(" entry_"));
                b.Append(digits);
                b.Append(Str(" = enabled\n"));
            }
        }
    } else if (example == 6) {
        b.Append(Str(kRenamed));
    } else if (example == 7) {
        b.Append(Str(kNoNewline));
    } else if (example == 8) {
        b.Append(Str(kMode));
    } else {
        b.Append(Str(kReview));
        b.Append(Str(kAdded));
        b.Append(Str(kDeleted));
        b.Append(Str(kMode));
    }
    return b.TakeStr();
}

static void SetOne(component::DiffState* st, component::DiffFile* file,
                   Ctx* cx) {
    if (file) st->SetFiles(&file, 1, cx);
}

void DiffStory::LoadExample(DiffStory* self, Ctx* cx) {
    component::DiffState* st = State(self, cx);
    if (!st) return;
    int example = self->example;
    if (example == 9) {
        component::DiffParseError error = {};
        SetOne(st,
               component::DiffFile::ParseConflicts(StrL("src/retry.rs"),
                                                   Str(kMerge), &error),
               cx);
        return;
    }
    if (example == 10) {
        SetOne(
            st,
            component::DiffFile::Unchanged(StrL("src/retry.rs"), Str(kSource)),
            cx);
        return;
    }
    Str patch = ExamplePatch(example);
    Vec<component::DiffFile*> files;
    component::DiffParseError error = {};
    if (component::DiffFile::Parse(patch, &files, &error)) {
        st->SetFiles(files.els, len(files), cx);
    }
    StrFree(patch);
}

static bool PatchHasChanges(component::DiffState* st) {
    int n = 0;
    component::DiffFile** files = st ? st->Files(&n) : nullptr;
    for (int i = 0; files && i < n; i++) {
        if (files[i] && files[i]->HasChanges()) return true;
    }
    return false;
}

static component::PopupMenu* CheckedMenu(
    Ctx* cx, Str id, const char* const* labels, int n, int selected,
    void (*fn)(DiffStory*, Ctx*, const ClickEvent*, int64_t)) {
    component::PopupMenu* menu = component::PopupMenu::New(cx, id);
    for (int i = 0; i < n; i++) {
        menu->MenuWithCheck(Str(labels[i]), selected == i)
            ->OnClick(Listen(cx, fn, i));
    }
    return menu;
}

El* DiffStory::Render(DiffStory* self, Ctx* cx) {
    if (!self->loaded) {
        self->state = EntityNew<component::DiffState>(cx->app);
        component::DiffState* st = self->state.Get(cx);
        st->Bind(cx->app, self->state);
        LoadExample(self, cx);
        self->note = component::DiffAnnotation::Line(
            StrL("retry-delay-review"),
            component::DiffLinePosition::New(
                StrL("src/retry.rs"), component::DiffSide::Modified, 21));
        self->loaded = true;
    }
    component::DiffState* st = self->state.Get(cx);
    component::DiffMode mode = st ? st->Mode() : component::DiffMode::Unified;
    bool hasContext = st ? st->HasContextLines() : true;
    int contextLines = st ? st->ContextLines() : 3;
    bool changes = PatchHasChanges(st);

    El* modes = Div(cx->a)->FlexRow()->ItemsCenter()->Gap(8);
    modes->Child(component::Button::New(cx, StrL("diff-unified"))
                     ->Label(StrL("Unified"))
                     ->Selected(mode == component::DiffMode::Unified)
                     ->OnClick(Listen(cx, &DiffStory::OnUnified))
                     ->IntoEl());
    modes->Child(component::Button::New(cx, StrL("diff-split"))
                     ->Label(StrL("Split"))
                     ->Selected(mode == component::DiffMode::Split)
                     ->OnClick(Listen(cx, &DiffStory::OnSplit))
                     ->IntoEl());

    El* nav = Div(cx->a)->FlexRow()->ItemsCenter()->Gap(8);
    nav->Child(component::Button::New(cx, StrL("diff-previous"))
                   ->Ghost()
                   ->Icon(IconName::ArrowUp)
                   ->Tooltip(StrL("Previous change"))
                   ->Disabled(!changes)
                   ->OnClick(Listen(cx, &DiffStory::OnPrevious))
                   ->IntoEl());
    nav->Child(component::Button::New(cx, StrL("diff-next"))
                   ->Ghost()
                   ->Icon(IconName::ArrowDown)
                   ->Tooltip(StrL("Next change"))
                   ->Disabled(!changes)
                   ->OnClick(Listen(cx, &DiffStory::OnNext))
                   ->IntoEl());

    static const char* kInline[] = {"Words", "Characters", "Whole lines"};
    static const char* kSeps[] = {"Hunk header", "Hidden line count",
                                  "Divider"};
    static const char* kIndicators[] = {"Signs", "Bars", "None"};
    component::PopupMenu* context =
        component::PopupMenu::New(cx, StrL("diff-context"));
    context
        ->MenuWithCheck(StrL("Three context lines"),
                        hasContext && contextLines == 3)
        ->OnClick(Listen(cx, &DiffStory::OnContextThree));
    context->MenuWithCheck(StrL("All supplied context"), !hasContext)
        ->OnClick(Listen(cx, &DiffStory::OnContextAll));
    context->Separator();
    context->Menu(StrL("Expand all"))
        ->OnClick(Listen(cx, &DiffStory::OnExpand));
    context->Menu(StrL("Collapse all"))
        ->OnClick(Listen(cx, &DiffStory::OnCollapse));
    component::PopupMenu* options =
        component::PopupMenu::New(cx, StrL("diff-options-menu"));
    options->Submenu(StrL("Example"),
                     CheckedMenu(cx, StrL("diff-example"), kExamples, 11,
                                 self->example, &DiffStory::OnExample));
    options->Submenu(StrL("Context"), context);
    options->Submenu(StrL("Inline changes"),
                     CheckedMenu(cx, StrL("diff-inline"), kInline, 3,
                                 self->inlineIx, &DiffStory::OnInline));
    options->Submenu(StrL("Hunk separator"),
                     CheckedMenu(cx, StrL("diff-separator"), kSeps, 3,
                                 self->separator, &DiffStory::OnSeparator));
    options->Submenu(StrL("Change indicator"),
                     CheckedMenu(cx, StrL("diff-indicator"), kIndicators, 3,
                                 self->indicator, &DiffStory::OnIndicator));
    options->Separator();
    options->MenuWithCheck(StrL("Tint changed lines"), self->background)
        ->OnClick(Listen(cx, &DiffStory::OnTint));
    options->MenuWithCheck(StrL("Highlight hovered line"), self->hover)
        ->OnClick(Listen(cx, &DiffStory::OnHover));
    options->MenuWithCheck(StrL("Wrap long lines"), self->wrap)
        ->OnClick(Listen(cx, &DiffStory::OnWrap));

    El* row = Div(cx->a)->FlexRow()->W(kFill)->ItemsCenter()->Gap(8);
    row->Child(modes);
    row->Child(nav);
    row->Child(Div(cx->a)->Flex1());
    row->Child(component::DropdownMenu::New(cx, StrL("diff-options-dropdown"))
                   ->AnchorRight()
                   ->Trigger(component::Button::New(cx, StrL("diff-options"))
                                 ->Label(StrL("Options"))
                                 ->IntoEl())
                   ->Menu(options)
                   ->IntoEl());

    component::DiffHunkSeparator sep = component::DiffHunkSeparator::Metadata;
    if (self->separator == 1) sep = component::DiffHunkSeparator::LineInfo;
    if (self->separator == 2) sep = component::DiffHunkSeparator::Simple;
    component::DiffChangeIndicator mark = component::DiffChangeIndicator::Signs;
    if (self->indicator == 1) mark = component::DiffChangeIndicator::Bars;
    if (self->indicator == 2) mark = component::DiffChangeIndicator::None;

    component::Diff* diff =
        component::Diff::New(cx, self->state)
            ->HunkSeparator(sep)
            ->ChangeIndicator(mark)
            ->ChangeBackground(self->background)
            ->HoverHighlight(self->hover ? component::DiffHoverHighlight::Both
                                         : component::DiffHoverHighlight::None)
            ->SoftWrap(self->wrap);
    if (self->example == 0) {
        if (!self->diagnosticReady) {
            component::DiffLinePosition at = component::DiffLinePosition::New(
                StrL("src/retry.rs"), component::DiffSide::Modified, 21);
            self->diagnostic =
                component::DiffDecoration::New(component::DiffLineRange::New(
                    StrL("src/retry.rs"), component::DiffSide::Modified, 21,
                    21));
            self->overflow =
                component::DiffGutterMarker::New(StrL("overflow"), at)
                    .WithLabel(StrL("Possible overflow"));
            self->diagnosticReady = true;
        }
        diff->Decorations(&self->diagnostic, 1)
            ->GutterMarkers(&self->overflow, 1)
            ->RenderGutterMarker(&DiffStory::RenderOverflow, self)
            ->Annotations(&self->note, 1)
            ->RenderAnnotation(&DiffStory::RenderComment, self);
    }
    El* body = Div(cx->a)->FlexCol()->W(kFill)->Gap(8);
    body->Child(TextEl(cx->a, Str(kExamples[self->example]))->Medium());
    body->Child(diff->IntoEl()->H(512));

    El* page = Div(cx->a)->FlexCol()->W(kFill)->Gap(12);
    page->Child(StoryToolbar(cx, self));
    page->Child(row);
    page->Child(body);
    return page;
}

STORY_PAGE(StoryDiff, DiffStory);
