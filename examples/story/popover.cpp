#include "Story.h"

struct PopoverStory {
    // form_popover_open / list_popover_open: the two controlled popovers.
    // The rest keep their own open state.
    bool formOpen = false;
    bool listOpen = false;
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

// on_open_change for the controlled popovers: the new state is theirs.
static void FormOpenChange(PopoverStory* self, Ctx* cx,
                           const PopoverOpenChangeEvent* ev) {
    self->formOpen = ev && ev->open;
    Notify(cx);
}
static void ListOpenChange(PopoverStory* self, Ctx* cx,
                           const PopoverOpenChangeEvent* ev) {
    self->listOpen = ev && ev->open;
    Notify(cx);
}
static void ToggleArrow(PopoverStory* self, Ctx* cx, const ClickEvent*,
                        int64_t checked) {
    self->arrow = checked != 0;
    Notify(cx);
}
static void SubmitForm(PopoverStory* self, Ctx* cx, const ClickEvent*) {
    self->formOpen = false;
    Notify(cx);
}
static void FocusFormInput(PopoverStory* self, Ctx* cx, const ClickEvent*) {
    self->formInput.focused = true;
    Notify(cx);
}

// A line of the popover's text, in its foreground.
static El* PopText(Ctx* cx, const char* s) {
    return StoryTxt(cx, Str(s), 14, ThemeNow(cx->app).popoverFg)->Wrap();
}

// Button::new(id).outline().label(label), handed over as the Selectable
// trigger so it shows the popover open.
static component::Button* PopTrigger(Ctx* cx, const char* id,
                                     const char* label) {
    return component::Button::New(cx, Str(id))->Label(Str(label))->Outline();
}

// The right-click popover's content closure, run only while it is open.
static El* RightContent(void* user, Ctx* cx) {
    PopoverStory* self = (PopoverStory*)user;
    (void)self;
    El* body = Div(cx->a)->FlexCol()->Gap(8);
    body->Child(PopText(cx, "Hello, this is a Popover on the Bottom Right."));
    body->Child(component::Separator::Horizontal(cx)->IntoEl());
    body->Child(component::Button::New(cx, StrL("info1"))
                    ->Primary()
                    ->Label(StrL("Dismiss"))
                    ->OnClick(Listen(cx, &OnRightDismiss))
                    ->IntoEl()
                    ->W(80));
    return body;
}

// shadow_2xl: GPUI's one layer, 25px down, 50px blur, pulled in 12px, at a
// quarter black.
static void Shadow2xl(El* e, void*) {
    BoxShadow shadow = {0, 25.f, 50.f, -12.f, Rgba8(0, 0, 0, 64), false};
    e->Shadows(&shadow, 1);
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
    // v_flex().size_full().gap_6()
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);

    El* def =
        StorySection(cx, "Default", "Display lightweight contextual content.");
    // .max_w(px(600.)).gap_2().text_sm().w(px(400.)) on the surface.
    Style defStyle;
    defStyle.maxW = 600;
    defStyle.gapX = defStyle.gapY = 8;
    defStyle.fontSize = 14;
    defStyle.width = 400;
    StorySectionAdd(
        def, component::Popover::New(cx, StrL("popover-0"))
                 ->Refine(defStyle, StyleFieldMaxWidth | StyleFieldGap |
                                        StyleFieldFontSize | StyleFieldWidth)
                 ->Trigger(PopTrigger(cx, "btn", "Popover"))
                 ->Child(PopText(cx, "Hello, this is a Popover."))
                 ->Child(component::Separator::Horizontal(cx)->IntoEl())
                 ->Child(PopText(cx,
                                 "You can put any content here, including "
                                 "text,buttons, forms, and more."))
                 ->IntoEl());
    // No .text_sm() on this one, so its text is the theme's own size.
    StorySectionAdd(
        def, component::Popover::New(cx, StrL("default-open-popover"))
                 ->DefaultOpen(true)
                 ->Trigger(PopTrigger(cx, "default-open-btn", "Default Open"))
                 ->Child(StoryTxt(cx,
                                  StrL("This popover is open by default when "
                                       "first rendered."),
                                  16, th.popoverFg)
                             ->Wrap())
                 ->IntoEl());
    page->Child(def);

    // .p_0().text_sm() on the surface; the form pads itself.
    Style flush;
    flush.pad = {};
    flush.fontSize = 14;
    El* form = StorySection(cx, "Form",
                            "Keep focus and controlled open state around a "
                            "form.");
    El* formBody = Div(a)->FlexCol()->Gap(8)->Pad(12)->W(kFill)->H(kFill);
    formBody->Child(PopText(cx, "This is a form container."));
    formBody->Child(PopText(cx, "Click submit to dismiss the popover."));
    formBody->Child(
        component::Input::New(cx, StrL("pop-form-input"), &self->formInput)
            ->OnFocus(Listen(cx, &FocusFormInput))
            ->IntoEl());
    formBody->Child(component::Button::New(cx, StrL("submit"))
                        ->Label(StrL("Submit"))
                        ->Primary()
                        ->OnClick(Listen(cx, &SubmitForm))
                        ->IntoEl());
    StorySectionAdd(form,
                    component::Popover::New(cx, StrL("popover-form"))
                        ->Refine(flush, StyleFieldPad | StyleFieldFontSize)
                        ->Trigger(PopTrigger(cx, "pop", "Popup Form"))
                        ->Open(self->formOpen)
                        ->OnOpenChange(Listen(cx, &FormOpenChange))
                        ->Child(formBody)
                        ->IntoEl());
    page->Child(form);

    El* list = StorySection(cx, "List",
                            "Place a scrollable selection list in the "
                            "popover.");
    // .p_0().text_sm() and .w_64().h(px(200.)): the list fills the surface.
    Style listStyle = flush;
    listStyle.width = 256;
    listStyle.height = 200;
    component::List* popList =
        component::List::New(cx, StrL("popover-list"), self->list)
            ->Count(10)
            ->Items(self, &PopListItem)
            ->Searchable(&self->listSearch, Listen(cx, &FocusListSearch));
    StorySectionAdd(
        list, component::Popover::New(cx, StrL("popover-list"))
                  ->Refine(listStyle, StyleFieldPad | StyleFieldFontSize |
                                          StyleFieldWidth | StyleFieldHeight)
                  ->Open(self->listOpen)
                  ->OnOpenChange(Listen(cx, &ListOpenChange))
                  ->Trigger(PopTrigger(cx, "pop", "Popup List"))
                  ->Child(popList->IntoEl())
                  ->IntoEl());
    page->Child(list);

    El* right = StorySection(cx, "Right click",
                             "Open from the secondary mouse button.");
    // Popover::mouse_button(Right), and uncontrolled: the popover keeps its
    // own open state and the secondary press on the trigger toggles it, so
    // there is no listener on this trigger at all. Its content closure runs
    // only while it is open.
    Str rightId = StrL("popover-right-click");
    self->rightPopover = component::PopoverStateOf(cx, rightId);
    Style rightStyle;
    rightStyle.maxW = 600;
    StorySectionAdd(right,
                    component::Popover::New(cx, rightId)
                        ->Button(MouseButton::Right)
                        ->Trigger(PopTrigger(cx, "btn", "Right Click Popover"))
                        ->Refine(rightStyle, StyleFieldMaxWidth)
                        ->ContentBuilder(&RightContent, self)
                        ->IntoEl());
    page->Child(right);

    El* style = StorySection(cx, "Custom style",
                             "Customize appearance, radius, and shadow.");
    // appearance(false), then .py_1().px_2().bg(primary)
    // .text_color(primary_foreground).max_w(px(600.)).rounded(radius / 2)
    // .text_sm().shadow_2xl().
    Style custom;
    custom.pad = Edges::New(8, 8, 4, 4);
    custom.hasBg = true;
    custom.bg = th.tokens.primary;
    custom.color = th.primaryFg;
    custom.maxW = 600;
    custom.radius = th.radius * 0.5f;
    custom.fontSize = 14;
    StorySectionAdd(
        style,
        component::Popover::New(cx, StrL("popover-1"))
            ->Trigger(PopTrigger(cx, "btn", "Style Popover"))
            ->Appearance(false)
            ->Refine(custom, StyleFieldPad | StyleFieldBg | StyleFieldColor |
                                 StyleFieldMaxWidth | StyleFieldRadius |
                                 StyleFieldFontSize)
            ->RefineWith(ElRefiner{&Shadow2xl, nullptr})
            ->Child(PopText(cx,
                            "A styled Popover with custom background and "
                            "text color.")
                        ->Fg(th.primaryFg))
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
            row->Child(component::Popover::New(
                           cx, StoryFmt(cx, "anchor-%s", r.labels[i]))
                           ->Anchor(r.anchors[i])
                           ->Arrow(self->arrow)
                           ->Trigger(component::Button::New(cx, StrL("trigger"))
                                         ->Label(label)
                                         ->WithSize(UiSize::Small)
                                         ->Outline())
                           ->Child(StoryTxt(cx, StrL("Popover content"), 16,
                                            th.popoverFg))
                           ->IntoEl());
        }
        StorySectionAdd(anchor, row);
    }
    page->Child(anchor);
    return page;
}

STORY_PAGE(StoryPopover, PopoverStory);
