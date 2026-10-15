#include "Story.h"

// crates/story/src/stories/diff_story.rs — the pull-request patch, its
// toolbar, and the review comment. The other example patches are not on
// this page yet.

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
    "}\n";

struct DiffStory {
    StoryToolbarState toolbar;
    Entity<component::DiffState> state;
    bool loaded = false;
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

    static component::DiffState* State(DiffStory* self, Ctx* cx) {
        return self->state.Get(cx);
    }
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
    static El* RenderComment(Ctx* cx, const component::DiffAnnotation*,
                             void* user);
    static El* Render(DiffStory* self, Ctx* cx);
};

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
        Vec<component::DiffFile*> files;
        component::DiffParseError error;
        if (component::DiffFile::Parse(Str(kReview), &files, &error)) {
            st->SetFiles(files.els, len(files), cx);
        }
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

    El* page = Div(cx->a)->W(kFill)->Gap(12);
    page->Child(StoryToolbar(cx, self));
    page->Child(row);
    page->Child(TextEl(cx->a, StrL("Pull request")));
    page->Child(component::Diff::New(cx, self->state)
                    ->HunkSeparator(sep)
                    ->ChangeIndicator(mark)
                    ->ChangeBackground(self->background)
                    ->HoverHighlight(self->hover
                                         ? component::DiffHoverHighlight::Both
                                         : component::DiffHoverHighlight::None)
                    ->SoftWrap(self->wrap)
                    ->Annotations(&self->note, 1)
                    ->RenderAnnotation(&DiffStory::RenderComment, self)
                    ->IntoEl()
                    ->H(512));
    return page;
}

STORY_PAGE(StoryDiff, DiffStory);
