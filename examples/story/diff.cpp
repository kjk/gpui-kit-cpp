#include "Story.h"

// crates/story/src/stories/diff_story.rs — the default pull-request example.
// The options menu and the other example patches are not on this page yet.

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

    static void OnPrevious(DiffStory* self, Ctx* cx, const ClickEvent*) {
        component::DiffState* st = self->state.Get(cx);
        if (st) st->PreviousChange(cx);
    }
    static void OnNext(DiffStory* self, Ctx* cx, const ClickEvent*) {
        component::DiffState* st = self->state.Get(cx);
        if (st) st->NextChange(cx);
    }
    static El* Render(DiffStory* self, Ctx* cx);
};

El* DiffStory::Render(DiffStory* self, Ctx* cx) {
    if (!self->loaded) {
        self->state = EntityNew<component::DiffState>(cx->app);
        component::DiffState* st = self->state.Get(cx);
        st->Bind(cx->app, self->state);
        Vec<component::DiffFile*> files;
        component::DiffParseError error;
        if (component::DiffFile::Parse(Str(kReview), &files, &error))
            st->SetFiles(files.els, len(files), cx);
        self->loaded = true;
    }
    El* row = Div(cx->a)->FlexRow()->ItemsCenter()->Gap(8);
    row->Child(component::Button::New(cx, StrL("diff-previous"))
                   ->Ghost()
                   ->Icon(IconName::ArrowUp)
                   ->Tooltip(StrL("Previous change"))
                   ->OnClick(Listen(cx, &DiffStory::OnPrevious))
                   ->IntoEl());
    row->Child(component::Button::New(cx, StrL("diff-next"))
                   ->Ghost()
                   ->Icon(IconName::ArrowDown)
                   ->Tooltip(StrL("Next change"))
                   ->OnClick(Listen(cx, &DiffStory::OnNext))
                   ->IntoEl());
    row->Child(Div(cx->a)->Flex1());
    row->Child(component::Button::New(cx, StrL("diff-options"))
                   ->Label(StrL("Options"))
                   ->IntoEl());
    El* page = Div(cx->a)->W(kFill)->Gap(12);
    page->Child(StoryToolbar(cx, self));
    page->Child(row);
    page->Child(TextEl(cx->a, StrL("Pull request")));
    page->Child(component::Diff::New(cx, self->state)->IntoEl()->H(512));
    return page;
}

STORY_PAGE(StoryDiff, DiffStory);
