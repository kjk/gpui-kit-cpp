#include "Story.h"

// Which popover is open; the default-open one starts that way.
enum {
    PopDefault = 0,
    PopDefaultOpen,
    PopForm,
    PopList,
    PopRightClick,
    PopStyle,
    PopAsync,
    PopCount
};

struct PopoverStory {
    int open = PopDefaultOpen;
    InputState formInput;
    // The List section holds a real List, not a menu: ten rows behind a
    // search field, which is what `List::new(&self.list)` over
    // DropdownListDelegate renders.
    Entity<ListState> list = {};
    InputState listSearch;
    // The async submenu: false while it says Loading..., true once the timer
    // that stands in for Rust's spawned task has fired.
    bool asyncLoaded = false;
    bool asyncTimer = false;
    // The Anchor section's Arrow checkbox, unchecked by default.
    bool arrow = false;
    bool seeded = false;
    // The right-click popover's state, so its Dismiss button can close it.
    Entity<PopoverState> rightPopover = {};

    static El* Render(PopoverStory* self, Ctx* cx);
};

// The right-click popover's Dismiss: push a notification, then
// cx.emit(DismissEvent), which closes the popover it is in.
static void OnRightDismiss(PopoverStory* self, Ctx* cx, const ClickEvent*) {
    if (component::NotificationListState* st = StoryNotifications(cx).Get(cx)) {
        component::Notification item = component::Notification::New();
        item.Message(StrL("You have clicked dismiss via DismissEvent."));
        NotificationPush(st, cx, item);
    }
    PopoverSetOpen(cx, self->rightPopover, false);
    Notify(cx);
}

// render_item: `ListItem::new(ix).child(format!("Item {}", ix.row))`.
static component::ListItem* PopListItem(Ctx* cx, void*, int, int row, int) {
    return component::ListItem::New(
        cx, StoryTxt(cx, StoryFmt(cx, "Item %d", row), 14,
                     ThemeNow(cx->app).foreground));
}

// Kbd::format, as menu.cpp spells it: the platform's own shortcut text.
static Str PopChord(Ctx* cx, const char* key) {
    component::Keystroke k;
#if GPUI_OS_MAC
    k.platform = true;
#else
    k.ctrl = true;
#endif
    k.key = Str(key);
    return component::KbdFormatStr(cx, k);
}

static void AsyncLoaded(PopoverStory* self, Ctx* cx, const TickEvent*) {
    self->asyncLoaded = true;
    Notify(cx);
}

// The menu's rows are loaded a second after it is first opened, which is what
// `cx.spawn_in(..).timer(Duration::from_secs(1))` does around `rebuild`.
static void StartAsyncLoad(PopoverStory* self, Ctx* cx, const ClickEvent*) {
    if (self->asyncTimer) {
        return;
    }
    self->asyncTimer = true;
    WindowSetTimeout(cx->win, 1000, Listen(cx, &AsyncLoaded));
}

static void FocusListSearch(PopoverStory* self, Ctx* cx, const ClickEvent*) {
    self->listSearch.focused = true;
    Notify(cx);
}

static void TogglePop(PopoverStory* self, Ctx* cx, const ClickEvent*,
                      intptr_t which) {
    self->open = self->open == (int)which ? -1 : (int)which;
    Notify(cx);
}
static void ToggleArrow(PopoverStory* self, Ctx* cx, const ClickEvent*,
                        intptr_t checked) {
    self->arrow = checked != 0;
    Notify(cx);
}
static void SubmitForm(PopoverStory* self, Ctx* cx, const ClickEvent*) {
    self->open = -1;
    Notify(cx);
}
static void FocusFormInput(PopoverStory* self, Ctx* cx, const ClickEvent*) {
    self->formInput.focused = true;
    Notify(cx);
}

// The popover surface: p_3 over the background, bordered and rounded.
static El* PopCard(Ctx* cx, float maxW) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* card = Div(a)
                   ->FlexCol()
                   ->Gap(8)
                   ->Pad(12)
                   ->Radius(th.radiusLg)
                   ->Border(1, th.border)
                   ->Bg(th.tokens.background);
    if (maxW > 0) {
        card->MaxW(maxW);
    }
    return card;
}

static El* PopText(Ctx* cx, const char* s) {
    return StoryTxt(cx, Str(s), 14, ThemeNow(cx->app).foreground)->Wrap();
}

static El* PopTrigger(PopoverStory*, Ctx* cx, int which, const char* id,
                      const char* label, Listener toggle) {
    return component::Button::New(cx, Str(id))
        ->Label(Str(label))
        ->Outline()
        ->OnClick(ListenerArg(toggle, which))
        ->IntoEl();
}

El* PopoverStory::Render(PopoverStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->list.IsValid()) {
        self->list = EntityNewState<ListState>(cx->app);
    }
    if (self->listSearch.focused) {
        cx->win->input = &self->listSearch;
    }
    if (!self->seeded) {
        self->seeded = true;
        InputSetValue(&self->formInput, StrL("Hello"));
    }
    if (self->formInput.focused) {
        cx->win->input = &self->formInput;
    }
    Listener toggle = Listen(cx, &TogglePop);
    // v_flex().size_full().gap_6()
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);

    El* def =
        StorySection(cx, "Default", "Display lightweight contextual content.");
    El* defCard = PopCard(cx, 600)->W(400);
    defCard->Child(PopText(cx, "Hello, this is a Popover."));
    defCard->Child(component::Separator::Horizontal(cx)->IntoEl());
    defCard->Child(PopText(cx,
                           "You can put any content here, including "
                           "text, buttons, forms, and more."));
    StorySectionAdd(def, component::Popover::New(cx)
                             ->Trigger(PopTrigger(self, cx, PopDefault, "btn",
                                                  "Popover", toggle))
                             ->Content(defCard)
                             ->Open(self->open == PopDefault)
                             ->OnClose(ListenerArg(toggle, PopDefault))
                             ->IntoEl());
    // No .text_sm() on this one in Rust, so its text is the theme's own size.
    El* openCard = PopCard(cx, 600);
    openCard->Child(StoryTxt(cx,
                             StrL("This popover is open by default when "
                                  "first rendered."),
                             16, th.foreground)
                        ->Wrap());
    StorySectionAdd(def, component::Popover::New(cx)
                             ->Trigger(PopTrigger(self, cx, PopDefaultOpen,
                                                  "default-open-btn",
                                                  "Default Open", toggle))
                             ->Content(openCard)
                             ->Open(self->open == PopDefaultOpen)
                             ->OnClose(ListenerArg(toggle, PopDefaultOpen))
                             ->IntoEl());
    page->Child(def);

    El* form = StorySection(cx, "Form",
                            "Keep focus and controlled open state around a "
                            "form.");
    El* formCard = PopCard(cx, 0)->W(280);
    formCard->Child(PopText(cx, "This is a form container."));
    formCard->Child(PopText(cx, "Click submit to dismiss the popover."));
    formCard->Child(
        component::Input::New(cx, StrL("pop-form-input"), &self->formInput)
            ->OnFocus(Listen(cx, &FocusFormInput))
            ->IntoEl());
    formCard->Child(component::Button::New(cx, StrL("submit"))
                        ->Label(StrL("Submit"))
                        ->Primary()
                        ->OnClick(Listen(cx, &SubmitForm))
                        ->IntoEl());
    StorySectionAdd(form, component::Popover::New(cx)
                              ->Trigger(PopTrigger(self, cx, PopForm, "pop",
                                                   "Popup Form", toggle))
                              ->Content(formCard)
                              ->Open(self->open == PopForm)
                              ->OnClose(ListenerArg(toggle, PopForm))
                              ->IntoEl());
    page->Child(form);

    El* list = StorySection(cx, "List",
                            "Place a scrollable selection list in the "
                            "popover.");
    // p_0().text_sm().w_64().h(px(200.)): the surface is the list's own, so
    // the card has no padding of its own.
    El* listCard = Div(a)
                       ->FlexCol()
                       ->W(256)
                       ->H(200)
                       ->Radius(th.radiusLg)
                       ->Border(1, th.border)
                       ->Bg(th.tokens.background)
                       ->ClipY();
    component::List* popList =
        component::List::New(cx, StrL("popover-list"), self->list)
            ->H(198)
            ->Count(10)
            ->Items(self, &PopListItem)
            ->Searchable(&self->listSearch, Listen(cx, &FocusListSearch));
    listCard->Child(popList->IntoEl());
    StorySectionAdd(list,
                    component::Popover::New(cx)
                        ->Trigger(PopTrigger(self, cx, PopList, "pop-list",
                                             "Popup List", toggle))
                        ->Content(listCard)
                        ->Open(self->open == PopList)
                        ->OnClose(ListenerArg(toggle, PopList))
                        ->IntoEl());
    page->Child(list);

    El* right = StorySection(cx, "Right click",
                             "Open from the secondary mouse button.");
    // Popover::mouse_button(Right), and uncontrolled: the popover keeps its
    // own open state and the secondary press on the trigger toggles it, so
    // there is no listener on this trigger at all.
    Str rightId = StrL("btn-right-popover");
    El* rightCard = nullptr;
    if (component::PopoverOpen(cx, rightId)) {
        rightCard = PopCard(cx, 600);
        rightCard->Child(
            PopText(cx, "Hello, this is a Popover on the Bottom Right."));
        rightCard->Child(component::Separator::Horizontal(cx)->IntoEl());
        self->rightPopover = component::PopoverStateOf(cx, rightId);
        rightCard->Child(component::Button::New(cx, StrL("info1"))
                             ->Primary()
                             ->Label(StrL("Dismiss"))
                             ->OnClick(Listen(cx, &OnRightDismiss))
                             ->IntoEl()
                             ->W(80));
    }
    StorySectionAdd(right,
                    component::Popover::New(cx, rightId)
                        ->Button(MouseButton::Right)
                        ->Trigger(component::Button::New(cx, StrL("btn-right"))
                                      ->Label(StrL("Right Click Popover"))
                                      ->Outline()
                                      ->IntoEl())
                        ->Content(rightCard)
                        ->IntoEl());
    page->Child(right);

    El* style = StorySection(cx, "Custom style",
                             "Customize appearance, radius, and shadow.");
    // appearance(false) with a primary background and half the radius.
    El* styleCard = Div(a)
                        ->MaxW(600)
                        ->PadX(8)
                        ->PadY(4)
                        ->Radius(th.radius * 0.5f)
                        ->Bg(th.tokens.primary)
                        ->Child(StoryTxt(cx,
                                         StrL("A styled Popover with custom "
                                              "background and text color."),
                                         14, th.primaryFg)
                                    ->Wrap());
    StorySectionAdd(style,
                    component::Popover::New(cx)
                        ->Trigger(PopTrigger(self, cx, PopStyle, "btn-style",
                                             "Style Popover", toggle))
                        ->Content(styleCard)
                        ->Open(self->open == PopStyle)
                        ->OnClose(ListenerArg(toggle, PopStyle))
                        ->IntoEl());
    page->Child(style);

    // A Button with a dropdown_menu, not a Popover: Copy, a separator, and a
    // submenu whose rows are built a second after the menu opens. Rust
    // spawns a task that calls `PopupMenu::rebuild`; the timer here is
    // `WindowSetTimeout`, started the first time the menu is rendered.
    El* async = StorySection(cx, "Async submenu",
                             "Rebuild submenu content after asynchronous "
                             "loading.");
    component::PopupMenu* sub =
        component::PopupMenu::New(cx, StrL("async-submenu"));
    if (self->asyncLoaded) {
        for (int i = 1; i <= 3; i++) {
            sub->Menu(StoryFmt(cx, "Loaded Item %d", i));
        }
    } else {
        sub->Menu(StrL("Loading..."));
    }
    El* asyncContent = Div(a)->FlexCol()->Gap(8)->ItemsCenter();
    asyncContent
        ->Child(component::DropdownMenu::New(cx, StrL("async-menu-dropdown"))
                    ->Trigger(component::Button::New(cx, StrL("async-menu"))
                                  ->Outline()
                                  ->Label(StrL("Async Menu"))
                                  ->OnClick(Listen(cx, &StartAsyncLoad))
                                  ->IntoEl())
                    ->Menu(component::PopupMenu::New(cx, StrL("async-menu"))
                               // popover_story::init binds cmd-c / ctrl-c to
                               // Copy in the story's own context, which is
                               // where the shortcut beside the row comes from.
                               ->MenuWithKbd(StrL("Copy"), PopChord(cx, "c"))
                               ->Separator()
                               ->Submenu(StrL("Async Submenu"), sub))
                    ->IntoEl());
    StorySectionAdd(async, asyncContent);
    page->Child(async);

    El* anchor = StorySection(cx, "Anchor",
                              "Position content from each edge of the "
                              "trigger.");
    StorySectionSubTitle(anchor,
                         component::Checkbox::New(cx, StrL("anchor-arrow"))
                             ->Label(StrL("Arrow"))
                             ->Checked(self->arrow)
                             ->OnChange(Listen(cx, &ToggleArrow))
                             ->IntoEl());
    // min_h(rems(14.)).v_flex().justify_between(): three rows of small
    // outline triggers, one popover per anchor, each an uncontrolled
    // Popover whose content is "Popover content" on the default surface.
    StorySectionBody(anchor)->W(kFill)->MinH(224)->FlexCol()->JustifyBetween();
    struct AnchorRow {
        int n;
        PopupAnchor anchors[3];
        const char* labels[3];
    };
    AnchorRow rows[3] = {
        {3,
         {PopupAnchor::TopLeft, PopupAnchor::TopCenter, PopupAnchor::TopRight},
         {"TopLeft", "TopCenter", "TopRight"}},
        {2,
         {PopupAnchor::LeftCenter, PopupAnchor::RightCenter},
         {"LeftCenter", "RightCenter"}},
        {3,
         {PopupAnchor::BottomLeft, PopupAnchor::BottomCenter,
          PopupAnchor::BottomRight},
         {"BottomLeft", "BottomCenter", "BottomRight"}},
    };
    for (const AnchorRow& r : rows) {
        El* row = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
        for (int i = 0; i < r.n; i++) {
            Str label = Str(r.labels[i]);
            El* surface = component::PopoverSurface(
                cx, Div(a)->FlexCol()->Pad(12)->Child(StoryTxt(
                        cx, StrL("Popover content"), 14, th.popoverFg)));
            row->Child(component::Popover::New(
                           cx, StoryFmt(cx, "anchor-%s", r.labels[i]))
                           ->Anchor(r.anchors[i])
                           ->Arrow(self->arrow)
                           ->Trigger(component::Button::New(cx, StrL("trigger"))
                                         ->Label(label)
                                         ->WithSize(UiSize::Small)
                                         ->Outline()
                                         ->IntoEl())
                           ->Content(surface)
                           ->IntoEl());
        }
        StorySectionAdd(anchor, row);
    }
    page->Child(anchor);
    return page;
}

STORY_PAGE(StoryPopover, PopoverStory);
