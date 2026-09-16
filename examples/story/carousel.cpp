#include "Story.h"

using namespace gpui;

struct CarouselStory {
    Entity<component::CarouselState> state = {};

    static El* Render(CarouselStory* self, Ctx* cx) {
        using namespace component;
        if (!self->state.IsValid()) {
            self->state = CarouselStateNew(cx->app, 4);
        }
        const Theme& theme = ThemeNow(cx->app);
        CarouselContent* content = CarouselContent::New(cx, self->state);
        const char* labels[] = {"First", "Second", "Third", "Fourth"};
        for (int i = 0; i < 4; i++) {
            El* card = Div(cx->a)
                           ->W(kFill)
                           ->H(220)
                           ->Radius(theme.radiusLg)
                           ->Bg(theme.muted)
                           ->ItemsCenter()
                           ->JustifyCenter()
                           ->Child(TextEl(cx->a, Str(labels[i]))->Font(20));
            content->Child(CarouselItem::New(cx, StoryFmt(cx, "slide-%d", i), i,
                                             self->state)
                               ->Child(card)
                               ->IntoEl());
        }
        CarouselPagination* pagination = CarouselPagination::New(cx);
        for (int i = 0; i < 4; i++) {
            pagination
                ->Child(CarouselPaginationItem::New(
                            cx, StoryFmt(cx, "page-%d", i), i, self->state)
                            ->Child(Div(cx->a)->W(8)->H(8)->Radius(4))
                            ->IntoEl());
        }
        El* demo =
            Carousel::New(cx, StrL("story-carousel"), self->state)
                ->Child(content->IntoEl())
                ->Child(
                    Div(cx->a)
                        ->FlexRow()
                        ->Gap(12)
                        ->ItemsCenter()
                        ->JustifyCenter()
                        ->Child(CarouselPrevious::New(cx, self->state)
                                    ->IntoEl())
                        ->Child(pagination->IntoEl())
                        ->Child(CarouselNext::New(cx, self->state)->IntoEl()))
                ->IntoEl();
        El* section = StorySection(
            cx, "Carousel", "Use the controls, pagination, or arrow keys.");
        StorySectionBody(section)->MaxW(560);
        StorySectionAdd(section, demo);
        return Div(cx->a)->FlexCol()->W(kFill)->Child(section);
    }
};

STORY_PAGE(StoryCarousel, CarouselStory);
