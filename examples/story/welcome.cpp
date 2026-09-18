#include "Story.h"

// crates/story/src/stories/welcome_story.rs is this page in its entirety:
//
//     markdown(include_str!("../../../../README.md"))
//         .px_4()
//         .scrollable(true)
//         .selectable(true)
//
// so this is too — the README goes through component::TextView, the one
// markdown renderer in the tree, and the story shell owns the scrolling and
// the 16px padding. assets/story/README.md is a verbatim copy of
// gpui-kit's README.md at the SHA in cmd/versions.ts; refresh it when
// that SHA moves.
struct WelcomeStory {
    static El* Render(WelcomeStory* self, Ctx* cx);

    // The source outlives the frame arena TextView parses into, so it is held
    // here rather than reloaded from the asset roots on every render.
    bool loaded = false;
    char source[24000] = {};
};

static void LoadReadme(WelcomeStory* self) {
    self->loaded = true;
    TempStr md = AssetsLoadTextTemp(StrL("story/README.md"));
    if (md.s && len(md) > 0) {
        int cap = (int)sizeof(self->source) - 1;
        int n = len(md) < cap ? len(md) : cap;
        memcpy(self->source, md.s, (size_t)n);
        self->source[n] = 0;
        return;
    }
    StrCopyZ(self->source, (int)sizeof(self->source),
             "# README.md is missing\n\nExpected `assets/story/README.md`.");
}

El* WelcomeStory::Render(WelcomeStory* self, Ctx* cx) {
    if (!self->loaded) {
        LoadReadme(self);
    }
    // Body 16px and headings off the 14px TextViewStyle base — h1 28, h2 21 —
    // both of which TextView already defaults to.
    return component::TextView::New(cx, Str(self->source))
        ->Selectable()
        ->IntoEl();
}

STORY_PAGE(StoryWelcome, WelcomeStory);
