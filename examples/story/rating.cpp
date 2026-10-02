#include "Story.h"

struct RatingStory {
    int rating = 3;
    StoryToolbarState toolbar;

    static El* Render(RatingStory* self, Ctx* cx);
};

static void SetRating(RatingStory* self, Ctx* cx, const ClickEvent*,
                      int64_t v) {
    self->rating = (int)v;
    Notify(cx);
}

El* RatingStory::Render(RatingStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* def =
        StorySection(cx, "Default", "Select a value directly from the rating.");
    StorySectionBody(def)->W(512);
    El* defaultContent =
        Div(a)
            ->FlexCol()
            ->W(kFill)
            ->Gap(12)
            ->JustifyCenter()
            ->ItemsCenter()
            ->Child(component::Rating::New(cx, StrL("rating-1"))
                        ->WithSize(self->toolbar.size)
                        ->Value(self->rating)
                        ->Max(5)
                        ->OnClick(Listen(cx, &SetRating))
                        ->IntoEl());
    StorySectionAdd(def, Div(a)
                             ->FlexRow()
                             ->FlexWrap()
                             ->W(512)
                             ->Gap(16)
                             ->JustifyCenter()
                             ->ItemsCenter()
                             ->Child(defaultContent));
    page->Child(def);

    El* dis = StorySection(cx, "Disabled", nullptr);
    StorySectionBody(dis)->W(480);
    StorySectionAdd(dis,
                    Div(a)
                        ->FlexRow()
                        ->FlexWrap()
                        ->W(480)
                        ->Gap(16)
                        ->JustifyCenter()
                        ->ItemsCenter()
                        ->Child(component::Rating::New(cx, StrL("rating-2"))
                                    ->WithSize(self->toolbar.size)
                                    ->Value(2)
                                    ->Color(th.green)
                                    ->Max(5)
                                    ->Disabled(true)
                                    ->IntoEl()));
    page->Child(dis);

    El* col = StorySection(cx, "Color", nullptr);
    StorySectionBody(col)->W(480);
    StorySectionAdd(col,
                    Div(a)
                        ->FlexRow()
                        ->FlexWrap()
                        ->W(480)
                        ->Gap(16)
                        ->JustifyCenter()
                        ->ItemsCenter()
                        ->Child(component::Rating::New(cx, StrL("rating-3"))
                                    ->WithSize(self->toolbar.size)
                                    ->Value(self->rating)
                                    ->Color(th.green)
                                    ->Max(5)
                                    ->IntoEl()));
    page->Child(col);
    return page;
}

STORY_PAGE(StoryRating, RatingStory);
