#include "Story.h"

// crates/story/src/stories/live_region_story.rs
struct LiveRegionStory {
    int saved = 0;
    bool failed = false;

    static void Save(LiveRegionStory* self, Ctx* cx, const ClickEvent*) {
        self->saved++;
        self->failed = false;
        Notify(cx);
    }

    static void FailSave(LiveRegionStory* self, Ctx* cx, const ClickEvent*) {
        self->failed = true;
        Notify(cx);
    }

    static El* Render(LiveRegionStory* self, Ctx* cx) {
        Arena* a = cx->a;
        Str status = self->saved == 0 ? StrL("Not saved")
                     : self->saved == 1
                         ? StrL("Saved 1 time")
                         : StoryFmt(cx, "Saved %d times", self->saved);
        Str error = self->failed ? StrL("Could not save: disk full") : Str{};
        El* polite = StorySection(cx, "Polite",
                                  "Focus stays on the button; the status is "
                                  "announced after current speech.");
        El* assertive = StorySection(cx, "Assertive",
                                     "Interrupts current speech. Reserve for "
                                     "errors that block the task.");
        StorySectionAdd(
            polite,
            Div(a)
                ->FlexRow()
                ->ItemsCenter()
                ->Gap(12)
                ->Child(component::Button::New(cx, StrL("live-region-save"))
                            ->Label(StrL("Save"))
                            ->OnClick(Listen(cx, &Save))
                            ->IntoEl())
                ->Child(component::LiveRegion::New(
                            cx, StrL("live-region-status"), status)
                            ->IntoEl()));
        StorySectionAdd(
            assertive,
            Div(a)
                ->FlexRow()
                ->ItemsCenter()
                ->Gap(12)
                ->Child(component::Button::New(cx, StrL("live-region-fail"))
                            ->Label(StrL("Fail Save"))
                            ->OnClick(Listen(cx, &FailSave))
                            ->IntoEl())
                ->Child(component::LiveRegion::New(
                            cx, StrL("live-region-error"), error)
                            ->Assertive()
                            ->IntoEl()));
        return Div(a)
            ->W(kFill)
            ->FlexCol()
            ->ItemsCenter()
            ->Gap(24)
            ->Child(polite)
            ->Child(assertive);
    }
};

STORY_PAGE(StoryLiveRegion, LiveRegionStory);
