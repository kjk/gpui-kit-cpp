#include "Story.h"

struct RadioStory {
    int delivery = 0;
    int billing = 1;
    StoryToolbarState toolbar;

    static El* Render(RadioStory* self, Ctx* cx);
};

// RadioGroup::on_click(&usize): the index the click landed on.
static void SetDelivery(RadioStory* self, Ctx* cx, const ClickEvent*,
                        int64_t ix) {
    self->delivery = (int)ix;
    Notify(cx);
}
static void SetBilling(RadioStory* self, Ctx* cx, const ClickEvent*,
                       int64_t ix) {
    self->billing = (int)ix;
    Notify(cx);
}

El* RadioStory::Render(RadioStory* self, Ctx* cx) {
    Arena* a = cx->a;
    UiSize size = self->toolbar.size;
    El* page = Div(a)->FlexCol()->ItemsCenter()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* del = StorySection(cx, "Delivery",
                           "Choose one option from a clearly described set.");
    StorySectionBody(del)->W(320)->ItemsCenter();
    static const char* const kDeliveryIds[] = {"standard", "express", "pickup"};
    static const char* const kDeliveryLabels[] = {
        "Standard delivery", "Express delivery", "Store pickup"};
    static const char* const kDeliveryHints[] = {
        "Arrives in 3–5 business days.", "Arrives the next business day.",
        "Unavailable for this order."};
    component::RadioGroup* delivery =
        component::RadioGroup::Vertical(cx, StrL("delivery"))
            ->WithSize(size)
            ->Selected(self->delivery)
            ->OnClick(Listen(cx, &SetDelivery));
    for (int i = 0; i < 3; i++) {
        component::Radio* r = component::Radio::New(cx, Str(kDeliveryIds[i]))
                                  ->Label(Str(kDeliveryLabels[i]))
                                  ->Hint(Str(kDeliveryHints[i]));
        if (i == 2) {
            r->Disabled(true);
        }
        delivery->Child(r);
    }
    StorySectionAdd(del, delivery->IntoEl()->W(320));
    page->Child(del);

    El* bill =
        StorySection(cx, "Billing cycle",
                     "Horizontal groups work for short, related choices.");
    StorySectionBody(bill)->W(320)->ItemsCenter();
    StorySectionAdd(bill,
                    component::RadioGroup::Horizontal(cx, StrL("billing"))
                        ->WithSize(size)
                        ->Child(component::Radio::New(cx, StrL("monthly"))
                                    ->Label(StrL("Monthly")))
                        ->Child(component::Radio::New(cx, StrL("yearly"))
                                    ->Label(StrL("Yearly")))
                        ->Child(component::Radio::New(cx, StrL("lifetime"))
                                    ->Label(StrL("Lifetime")))
                        ->Selected(self->billing)
                        ->OnClick(Listen(cx, &SetBilling))
                        ->IntoEl()
                        ->W(320)
                        ->JustifyBetween());
    page->Child(bill);
    return page;
}

STORY_PAGE(StoryRadio, RadioStory);
