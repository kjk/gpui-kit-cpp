#include "Story.h"

enum {
    IgSearch = 0,
    IgUrl,
    IgAmount,
    IgPassword,
    IgEmail,
    IgDisabled,
    IgReadonly,
    IgNotes,
    IgMessage,
    IgCount
};

struct InputGroupStory {
    InputState fields[IgCount];
    bool seeded = false;
    bool copied = false;

    static void OnCopy(InputGroupStory* self, Ctx* cx, const ClickEvent*);
    static void OnSend(InputGroupStory* self, Ctx* cx, const ClickEvent*);
    static El* Render(InputGroupStory* self, Ctx* cx);
};

void InputGroupStory::OnCopy(InputGroupStory* self, Ctx* cx,
                             const ClickEvent*) {
    self->copied = true;
    Notify(cx);
}

void InputGroupStory::OnSend(InputGroupStory* self, Ctx* cx,
                             const ClickEvent*) {
    StoryPushNotification(cx, InputValue(&self->fields[IgMessage]));
    Notify(cx);
}

static void Seed(InputGroupStory* self) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;
    InputSetPlaceholder(&self->fields[IgSearch], StrL("Search components…"));
    InputSetValue(&self->fields[IgUrl], StrL("gpui-kit.com"));
    InputSetPlaceholder(&self->fields[IgAmount], StrL("0.00"));
    InputSetPlaceholder(&self->fields[IgPassword], StrL("Enter password"));
    self->fields[IgPassword].masked = true;
    InputSetPlaceholder(&self->fields[IgEmail], StrL("you@example.com"));
    InputSetValue(&self->fields[IgDisabled], StrL("Unavailable"));
    InputSetValue(&self->fields[IgReadonly], StrL("https://gpui-kit.com"));
    InputSetValue(&self->fields[IgNotes],
                  StrL("console.log('Hello, GPUI Kit!');"));
    self->fields[IgNotes].kind = InputKind::Textarea;
    InputSetPlaceholder(&self->fields[IgMessage], StrL("Write a message…"));
    self->fields[IgMessage].kind = InputKind::Textarea;
}

El* InputGroupStory::Render(InputGroupStory* self, Ctx* cx) {
    Seed(self);
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);

    El* search =
        StorySection(cx, "Search", "An inline start addon shares the frame.");
    StorySectionBody(search)->W(520);
    StorySectionAdd(
        search,
        component::InputGroup::New(cx, StrL("search"))
            ->Input(component::Input::New(cx, StrL("search-field"),
                                          &self->fields[IgSearch]))
            ->Addon(component::InputGroupAddon::New(cx, StrL("search-icon"))
                        ->Child(IconEl(a, IconName::Search, 16)))
            ->IntoEl());
    page->Child(search);

    El* url = StorySection(cx, "URL", "Prefix text and a trailing action.");
    StorySectionBody(url)->W(520);
    StorySectionAdd(
        url,
        component::InputGroup::New(cx, StrL("url"))
            ->Input(component::Input::New(cx, StrL("url-field"),
                                          &self->fields[IgUrl]))
            ->Addon(component::InputGroupAddon::New(cx, StrL("url-prefix"))
                        ->Child(component::InputGroupText::New(cx)
                                    ->Child(TextEl(a, StrL("https://")))
                                    ->IntoEl()))
            ->Addon(
                component::InputGroupAddon::New(cx, StrL("url-copy"))
                    ->Align(component::InputGroupAddonAlignment::InlineEnd)
                    ->Child(component::InputGroupButton::New(cx, StrL("copy"))
                                ->Icon(IconName::Copy)
                                ->Tooltip(StrL("Copy"))
                                ->OnClick(Listen(cx, &InputGroupStory::OnCopy))
                                ->IntoEl()))
            ->IntoEl());
    page->Child(url);

    El* amount = StorySection(cx, "Amount", "A compact currency prefix.");
    StorySectionBody(amount)->W(520);
    StorySectionAdd(
        amount,
        component::InputGroup::New(cx, StrL("amount"))
            ->Input(component::Input::New(cx, StrL("amount-field"),
                                          &self->fields[IgAmount]))
            ->Addon(component::InputGroupAddon::New(cx, StrL("amount-prefix"))
                        ->Child(component::InputGroupText::New(cx)
                                    ->Child(TextEl(a, StrL("$")))
                                    ->IntoEl()))
            ->IntoEl());
    page->Child(amount);

    El* states = StorySection(
        cx, "Disabled and read-only",
        "Disabled blocks edits; read-only keeps selection and addon actions.");
    StorySectionBody(states)->W(520);
    El* col = Div(a)->FlexCol()->Gap(12)->W(kFill);
    col->Child(component::InputGroup::New(cx, StrL("disabled"))
                   ->Input(component::Input::New(cx, StrL("disabled-field"),
                                                 &self->fields[IgDisabled]))
                   ->Disabled(true)
                   ->IntoEl());
    col->Child(
        component::InputGroup::New(cx, StrL("readonly"))
            ->Input(component::Input::New(cx, StrL("readonly-field"),
                                          &self->fields[IgReadonly]))
            ->Readonly(true)
            ->Addon(component::InputGroupAddon::New(cx, StrL("readonly-copy"))
                        ->Align(component::InputGroupAddonAlignment::InlineEnd)
                        ->Child(component::InputGroupButton::New(
                                    cx, StrL("ro-copy"))
                                    ->Icon(IconName::Copy)
                                    ->IntoEl()))
            ->IntoEl());
    StorySectionAdd(states, col);
    page->Child(states);

    El* notes =
        StorySection(cx, "Block addons",
                     "A textarea sits between a header and an action row.");
    StorySectionBody(notes)->W(520);
    StorySectionAdd(
        notes,
        component::InputGroup::New(cx, StrL("notes"))
            ->Input(component::Textarea::New(cx, StrL("notes-field"),
                                             &self->fields[IgNotes])
                        ->Rows(4))
            ->Addon(component::InputGroupAddon::New(cx, StrL("notes-header"))
                        ->Align(component::InputGroupAddonAlignment::BlockStart)
                        ->Child(component::InputGroupText::New(cx)
                                    ->Child(TextEl(a, StrL("script.js")))
                                    ->IntoEl()))
            ->Addon(
                component::InputGroupAddon::New(cx, StrL("notes-actions"))
                    ->Align(component::InputGroupAddonAlignment::BlockEnd)
                    ->Child(component::InputGroupButton::New(cx, StrL("run"))
                                ->Label(StrL("Run"))
                                ->IntoEl()))
            ->IntoEl());
    page->Child(notes);

    El* message =
        StorySection(cx, "Message", "Send keeps the caller's retained state.");
    StorySectionBody(message)->W(520);
    StorySectionAdd(
        message,
        component::InputGroup::New(cx, StrL("message"))
            ->Input(component::Textarea::New(cx, StrL("message-field"),
                                             &self->fields[IgMessage])
                        ->Rows(2)
                        ->AriaLabel(StrL("Message")))
            ->Addon(component::InputGroupAddon::New(cx, StrL("message-header"))
                        ->Align(component::InputGroupAddonAlignment::BlockStart)
                        ->Child(component::InputGroupText::New(cx)
                                    ->Child(TextEl(a, StrL("New message")))
                                    ->IntoEl()))
            ->Addon(
                component::InputGroupAddon::New(cx, StrL("message-actions"))
                    ->Align(component::InputGroupAddonAlignment::BlockEnd)
                    ->Child(component::InputGroupButton::New(cx, StrL("send"))
                                ->Label(StrL("Send"))
                                ->WithVariant(component::ButtonVariant::Primary)
                                ->OnClick(Listen(cx, &InputGroupStory::OnSend))
                                ->IntoEl()))
            ->IntoEl());
    page->Child(message);

    return page;
}

STORY_PAGE(StoryInputGroup, InputGroupStory);
