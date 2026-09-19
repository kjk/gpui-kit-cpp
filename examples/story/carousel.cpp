#include "Story.h"

using namespace gpui;

struct CarouselStory {
    Entity<component::CarouselState> horizontal = {};
    Entity<component::CarouselState> custom = {};
    Entity<component::CarouselState> multiple = {};
    Entity<component::CarouselState> vertical = {};
    Entity<component::CarouselState> looped = {};
    Entity<component::CarouselState> controlled = {};
    StoryToolbarState toolbar;

    static void GoFirst(CarouselStory* self, Ctx* cx, const ClickEvent*) {
        if (auto* s = self->controlled.Get(cx)) s->SetSelectedIndex(0, cx);
        Notify(cx);
    }
    static void GoLast(CarouselStory* self, Ctx* cx, const ClickEvent*) {
        if (auto* s = self->controlled.Get(cx)) s->SetSelectedIndex(2, cx);
        Notify(cx);
    }
    static El* Render(CarouselStory* self, Ctx* cx);
};

static El* Slide(Ctx* cx, int index, float height, int font) {
    const Theme& th = ThemeNow(cx->app);
    return Div(cx->a)->W(kFill)->H(height)->FlexRow()->ItemsCenter()
        ->JustifyCenter()->Pad(24)->Radius(th.radiusLg)
        ->Border(1, th.border)->Bg(th.background)
        ->Child(TextEl(cx->a, StoryFmt(cx, "%d", index + 1))
                    ->Font((float)font)->Semibold());
}

static El* CarouselExample(Ctx* cx, Entity<component::CarouselState> state,
                           const char* id, int count, float width, float height,
                           int font, UiSize size, bool pagination,
                           bool customControls = false, bool multiple = false) {
    using namespace component;
    CarouselContent* content = CarouselContent::New(cx, state);
    for (int i = 0; i < count; i++) {
        El* item = CarouselItem::New(cx, StoryFmt(cx, "%s-%d", id, i), i, state)
                       ->Child(Slide(cx, i, height, font))->IntoEl();
        if (multiple) item->Basis(width / 3.f);
        content->Child(item);
    }
    El* frame = content->IntoEl()->H(height);
    Carousel* carousel = Carousel::New(cx, Str(id), state);
    carousel->Child(frame);
    CarouselControl* prev = CarouselPrevious::New(cx, state)->WithSize(size);
    CarouselControl* next = CarouselNext::New(cx, state)->WithSize(size);
    if (customControls) {
        prev->AccessibilityLabel(StrL("Previous project"))
            ->Child(TextEl(cx->a, StrL("Back")));
        next->AccessibilityLabel(StrL("Next project"))
            ->Child(TextEl(cx->a, StrL("Forward")));
    }
    carousel->Child(prev->IntoEl()->Absolute()
                        ->Left(customControls ? -54.f : -40.f)
                        ->Top(height * 0.5f - 14.f));
    carousel->Child(next->IntoEl()->Absolute()
                        ->Right(customControls ? -70.f : -40.f)
                        ->Top(height * 0.5f - 14.f));
    if (pagination) {
        CarouselPagination* pages = CarouselPagination::New(cx);
        for (int i = 0; i < count; i++) {
            pages->Child(CarouselPaginationItem::New(
                             cx, StoryFmt(cx, "%s-page-%d", id, i), i, state)
                             ->WithSize(size)
                             ->Child(TextEl(cx->a, StoryFmt(cx, "%d", i + 1)))
                             ->IntoEl());
        }
        carousel->Child(pages->IntoEl());
    }
    return Div(cx->a)->W(width)->Child(carousel->IntoEl());
}

static El* ExampleSection(Ctx* cx, const char* title, const char* desc,
                          El* child) {
    El* section = StorySection(cx, title, desc);
    StorySectionBody(section)->FlexCol()->Gap(12);
    StorySectionAdd(section, Div(cx->a)->W(kFill)->FlexRow()
                                 ->JustifyCenter()->Child(child));
    return section;
}

El* CarouselStory::Render(CarouselStory* self, Ctx* cx) {
    using namespace component;
    if (!self->horizontal.IsValid()) {
        self->horizontal = CarouselStateNew(cx->app, 3);
        self->custom = CarouselStateNew(cx->app, 3);
        self->multiple = CarouselStateNew(cx->app, 5);
        self->vertical = CarouselStateNew(cx->app, 3);
        self->looped = CarouselStateNew(cx->app, 4);
        self->controlled = CarouselStateNew(cx->app, 3);
        if (auto* s = self->vertical.Get(cx)) s->WithAxis(Axis::Vertical);
        if (auto* s = self->looped.Get(cx)) s->WithLooping();
        if (auto* s = self->controlled.Get(cx)) s->WithSelectedIndex(1);
    }
    UiSize size = self->toolbar.size;
    El* page = Div(cx->a)->FlexCol()->W(kFill)->Gap(16);
    page->Child(StoryToolbar(cx, self));
    page->Child(ExampleSection(
        cx, "Basic", "Browse one full-width item at a time with Left and Right.",
        CarouselExample(cx, self->horizontal, "carousel-horizontal", 3, 384,
                        384, 36, size, true)));
    page->Child(ExampleSection(
        cx, "Custom controls",
        "Replace control content and accessibility labels while retaining navigation behavior.",
        CarouselExample(cx, self->custom, "carousel-custom-controls", 3,
                        384, 384, 36, size, false, true)));
    page->Child(ExampleSection(
        cx, "Multiple items",
        "A fractional flex basis shows several items at once. Pair the content's negative margin with matching item padding to tune the gap between them.",
        CarouselExample(cx, self->multiple, "carousel-multiple", 5, 384,
                        128, 24, size, false, false, true)));
    page->Child(ExampleSection(
        cx, "Vertical", "Use Up and Down to navigate a vertical carousel.",
        CarouselExample(cx, self->vertical, "carousel-vertical", 3, 256,
                        192, 30, size, false)));
    page->Child(ExampleSection(
        cx, "Looping", "Looping navigation wraps from the last slide to the first.",
        CarouselExample(cx, self->looped, "carousel-looped", 4, 384,
                        384, 36, size, false)));
    El* controlled = ExampleSection(
        cx, "Controlled",
        "The selected index is owned by application state and can be changed programmatically.",
        CarouselExample(cx, self->controlled, "carousel-controlled", 3,
                        384, 384, 36, size, false));
    auto* controlledState = self->controlled.Get(cx);
    int selected = controlledState ? controlledState->SelectedIndex() + 1 : 1;
    StorySectionAdd(controlled,
        Div(cx->a)->FlexRow()->Gap(8)->ItemsCenter()
            ->Child(TextEl(cx->a, StoryFmt(cx, "Selected slide: %d", selected)))
            ->Child(component::Button::New(cx, StrL("controlled-first"))
                        ->Label(StrL("Go to first"))
                        ->OnClick(Listen(cx, &GoFirst))->IntoEl())
            ->Child(component::Button::New(cx, StrL("controlled-last"))
                        ->Label(StrL("Go to last"))
                        ->OnClick(Listen(cx, &GoLast))->IntoEl()));
    page->Child(controlled);
    return page;
}

STORY_PAGE(StoryCarousel, CarouselStory);
