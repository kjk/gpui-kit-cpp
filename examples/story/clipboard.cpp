#include "Story.h"

struct ClipboardStory {
    // The field whose value the second button copies.
    InputState url;
    bool masked = false;
    bool seeded = false;

    static El* Render(ClipboardStory* self, Ctx* cx);
};

// on_copied: window.push_notification(format!("Copied value: {}", value)).
static void OnCopied(ClipboardStory*, Ctx* cx,
                     const component::ClipboardEvent* ev) {
    StoryPushNotification(cx, StoryFmt(cx, "Copied value: %s", ev->value));
}

static void FocusUrl(ClipboardStory* self, Ctx* cx, const ClickEvent*) {
    cx->win->input = &self->url;
    Notify(cx);
}

El* ClipboardStory::Render(ClipboardStory* self, Ctx* cx) {
    Arena* a = cx->a;
    if (!self->seeded) {
        self->seeded = true;
        InputSetValue(&self->url, StrL("https://github.com"));
    }
    El* page = Div(a)->FlexCol()->JustifyStart()->Gap(12)->W(kFill);
    Listener copied = Listen(cx, &OnCopied);

    El* def = StorySection(cx, "Default",
                           "Copies a value supplied by the application.");
    StorySectionBody(def)->W(480);
    El* defRow = Div(a)->FlexRow()->ItemsCenter();
    // value_fn: the value is the page's own state, read at the frame the
    // click lands in.
    defRow->Child(
        component::Clipboard::New(cx, StrL("clipboard1"))
            ->Label(StrL("Copy value"))
            ->Value(StoryFmt(cx, "masked :%s", self->masked ? "true" : "false"))
            ->OnCopied(copied)
            ->IntoEl());
    StorySectionAdd(def, defRow);
    page->Child(def);

    El* input =
        StorySection(cx, "With Input", "Copies the field's current value.");
    StorySectionBody(input)->W(480);
    StorySectionAdd(
        input, component::Input::New(cx, StrL("url"), &self->url)
                   ->OnFocus(Listen(cx, &FocusUrl))
                   ->Suffix(component::Clipboard::New(cx, StrL("clipboard2"))
                                ->Value(InputValue(&self->url))
                                ->OnCopied(copied)
                                ->IntoEl())
                   ->IntoEl());
    page->Child(input);
    return page;
}

STORY_PAGE(StoryClipboard, ClipboardStory);
