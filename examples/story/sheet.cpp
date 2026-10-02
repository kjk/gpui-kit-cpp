#include "Story.h"

// The list the sheet holds, which is sheet_story.rs's own.
static const char* const kSheetFoods[] = {
    "Baguette (France)",
    "Baklava (Turkey)",
    "Beef Wellington (UK)",
    "Biryani (India)",
    "Borscht (Ukraine)",
    "Bratwurst (Germany)",
    "Bulgogi (Korea)",
    "Burrito (USA)",
    "Ceviche (Peru)",
    "Chicken Tikka Masala (India)",
    "Churrasco (Brazil)",
    "Couscous (North Africa)",
    "Croissant (France)",
    "Dim Sum (China)",
    "Empanada (Argentina)",
    "Fajitas (Mexico)",
    "Falafel (Middle East)",
    "Feijoada (Brazil)",
    "Fish and Chips (UK)",
    "Fondue (Switzerland)",
    "Goulash (Hungary)",
    "Haggis (Scotland)",
    "Kebab (Middle East)",
    "Kimchi (Korea)",
    "Lasagna (Italy)",
    "Maple Syrup Pancakes (Canada)",
    "Moussaka (Greece)",
    "Pad Thai (Thailand)",
    "Paella (Spain)",
    "Pancakes (USA)",
    "Pasta Carbonara (Italy)",
    "Pavlova (Australia)",
    "Peking Duck (China)",
    "Pho (Vietnam)",
    "Pierogi (Poland)",
    "Pizza (Italy)",
    "Poutine (Canada)",
    "Pretzel (Germany)",
    "Ramen (Japan)",
    "Rendang (Indonesia)",
    "Sashimi (Japan)",
    "Satay (Indonesia)",
    "Shepherd's Pie (Ireland)",
    "Sushi (Japan)",
    "Tacos (Mexico)",
    "Tandoori Chicken (India)",
    "Tortilla (Spain)",
    "Tzatziki (Greece)",
    "Wiener Schnitzel (Austria)",
};
static const int kSheetFoodCount =
    (int)(sizeof(kSheetFoods) / sizeof(kSheetFoods[0]));

enum {
    SheetLeft = 0,
    SheetTop,
    SheetRight,
    SheetBottom,
    SheetScrollable,
    SheetNone = -1
};

enum {
    SheetOptOverlay = ToolbarOptMultiple,
    SheetOptOverlayClosable = ToolbarOptIcon
};

struct SheetStory {
    int open = SheetNone;
    bool overlay = true;
    bool overlayClosable = true;
    InputState focusInput;
    // The three fields the sheet itself holds: a name, a birthday and the
    // query over the food list.
    InputState nameInput;
    InputState foodSearch;
    LocalDate birthday = {};
    bool birthdayOpen = false;
    Entity<ListState> foods = {};
    int confirmedFood = -1;
    // perform_search's matches: indices into kSheetFoods whose name holds
    // the query, case-insensitively. Rebuilt every frame from the field.
    int matches[kSheetFoodCount] = {};
    int nMatches = 0;
    // selected_value: the food the list last confirmed, shown under the page.
    int selectedFood = -1;
    float sheetScrollY = 0;
    StoryToolbarState toolbar;
    bool seeded = false;

    static El* Render(SheetStory* self, Ctx* cx);
};

// The rows the food list builds: the name, and a ghost heart after it.
static component::ListItem* SheetFoodRow(Ctx* cx, void* data, int, int row,
                                         int entry) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    SheetStory* self = (SheetStory*)data;
    El* line =
        Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter()->JustifyBetween();
    line->Child(
        StoryTxt(cx, Str(kSheetFoods[self->matches[row]]), 16, th.foreground));
    line->Child(component::Button::New(cx, StrL("like"))
                    ->Ghost()
                    ->Size(18)
                    ->Icon(IconName::Heart)
                    ->IntoEl());
    return component::ListItem::New(cx, line)
        ->CheckIcon(IconName::Check)
        ->Confirmed(self->confirmedFood == entry);
}

static void SheetToolbarAct(SheetStory* self, Ctx* cx, const ClickEvent*,
                            intptr_t act) {
    if (act == SheetOptOverlay) {
        self->overlay = !self->overlay;
    } else if (act == SheetOptOverlayClosable) {
        self->overlayClosable = !self->overlayClosable;
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    Notify(cx);
}

static void OpenSheet(SheetStory* self, Ctx* cx, const ClickEvent*,
                      intptr_t which) {
    self->open = (int)which;
    Notify(cx);
}
static void CloseSheet(SheetStory* self, Ctx* cx, const ClickEvent*) {
    self->open = SheetNone;
    self->sheetScrollY = 0;
    Notify(cx);
}
static void OnSheetScroll(SheetStory* self, Ctx* cx, const ScrollEvent* ev) {
    self->sheetScrollY = ev->offsetY;
    Notify(cx);
}
static void SheetBlurAll(SheetStory* self) {
    self->focusInput.focused = false;
    self->nameInput.focused = false;
    self->foodSearch.focused = false;
}
static void FocusSheetInput(SheetStory* self, Ctx* cx, const ClickEvent*) {
    SheetBlurAll(self);
    self->focusInput.focused = true;
    Notify(cx);
}
static void FocusSheetName(SheetStory* self, Ctx* cx, const ClickEvent*) {
    SheetBlurAll(self);
    self->nameInput.focused = true;
    Notify(cx);
}
static void FocusSheetSearch(SheetStory* self, Ctx* cx, const ClickEvent*) {
    SheetBlurAll(self);
    self->foodSearch.focused = true;
    Notify(cx);
}
static void ToggleSheetBirthday(SheetStory* self, Ctx* cx, const ClickEvent*) {
    self->birthdayOpen = !self->birthdayOpen;
    Notify(cx);
}
static void PickSheetBirthday(SheetStory* self, Ctx* cx, const ClickEvent*,
                              intptr_t day) {
    self->birthday.day = (int)day;
    self->birthdayOpen = false;
    Notify(cx);
}
// ListDelegate::confirm: the confirmed match becomes the page's
// selected_value.
static void OnFoodEvent(SheetStory* self, Ctx* cx, const ListEvent* ev) {
    ListState* s = self->foods.Get(cx);
    if (!s || ev->kind != ListEventKind::Confirm) {
        return;
    }
    self->confirmedFood = ev->index;
    ListRow row = ListRowAt(s, ListRowOfEntry(s, ev->index));
    if (row.row >= 0 && row.row < self->nMatches) {
        self->selectedFood = self->matches[row.row];
    }
    Notify(cx);
}

// render_empty: a 50px Inbox icon over "No matches found", muted on muted.
static El* SheetFoodsEmpty(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->ItemsCenter()
        ->JustifyCenter()
        ->Pad(12)
        ->Bg(th.muted)
        ->Child(IconEl(a, IconName::Inbox, 50)->Fg(th.mutedFg))
        ->Child(StoryTxt(cx, StrL("No matches found"), 16, th.mutedFg));
}

// The alert the sheet's "Open Confirm Dialog" raises over it.
struct SheetConfirmDialog {
    static void OnOk(SheetConfirmDialog*, Ctx* cx, const ClickEvent*) {
        StoryPushNotification(cx, StrL("You have pressed ok."));
        WindowCloseDialog(cx);
    }
    static void OnCancel(SheetConfirmDialog*, Ctx* cx, const ClickEvent*) {
        StoryPushNotification(cx, StrL("You have pressed cancel."));
        WindowCloseDialog(cx);
    }
    static El* Render(SheetConfirmDialog*, Ctx* cx) {
        const Theme& th = ThemeNow(cx->app);
        return component::AlertDialog::New(cx)
            ->Open(true)
            ->Body(StoryTxt(cx, StrL("Confirm dialog opened from sheet."), 16,
                            th.foreground))
            ->OnOk(Listen(cx, &SheetConfirmDialog::OnOk))
            ->OnCancel(Listen(cx, &SheetConfirmDialog::OnCancel))
            ->OnClose(Listen(cx, &SheetConfirmDialog::OnCancel))
            ->IntoEl(WindowSize(cx->win));
    }
};

static void OpenSheetConfirm(SheetStory*, Ctx* cx, const ClickEvent*) {
    WindowOpenAlertDialog(cx, EntityNew<SheetConfirmDialog>(cx->app));
}

// on_action_test_action.
static void SheetTestAction(SheetStory*, Ctx* cx, const ClickEvent*) {
    StoryPushNotification(cx, StrL("You have clicked the TestAction."));
}

// push_notification("Hello this is message from Sheet.")
static void SheetNotify(SheetStory*, Ctx* cx, const ClickEvent*) {
    StoryPushNotification(cx, StrL("Hello this is message from Sheet."));
}

El* SheetStory::Render(SheetStory* self, Ctx* cx) {
    WinSize size = WindowSize(cx->win);
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        InputSetPlaceholder(&self->focusInput,
                            StrL("For test focus back on dialog close."));
        InputSetPlaceholder(&self->nameInput, StrL("Your Name"));
        InputSetPlaceholder(&self->foodSearch, StrL("Search..."));
        self->foods = EntityNewState<ListState>(cx->app);
        if (ListState* ls = self->foods.Get(cx)) {
            ls->onEvent = Listen(cx, &OnFoodEvent);
        }
    }
    // perform_search: filter the foods by the query, case-insensitively.
    {
        Str query = InputValue(&self->foodSearch);
        self->nMatches = 0;
        for (int i = 0; i < kSheetFoodCount; i++) {
            if (len(query) == 0 || StrContainsI(Str(kSheetFoods[i]), query)) {
                self->matches[self->nMatches++] = i;
            }
        }
    }
    if (self->focusInput.focused) {
        cx->win->input = &self->focusInput;
    } else if (self->nameInput.focused) {
        cx->win->input = &self->nameInput;
    } else if (self->foodSearch.focused) {
        cx->win->input = &self->foodSearch;
    }
    Listener open = Listen(cx, &OpenSheet);
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);
    StoryToolbarOpt opts[2] = {
        {"Overlay", self->overlay, SheetOptOverlay},
        {"Close on overlay click", self->overlayClosable,
         SheetOptOverlayClosable},
    };
    // story_toolbar_group(): this page has no size button.
    page->Child(StoryToolbarOptions(cx, self, opts, 2,
                                    Listen(cx, &SheetToolbarAct), false));

    El* place = StorySection(cx, "Placement",
                             "Open a sheet from any edge of the window.");
    const char* ids[] = {"show-sheet-left", "show-sheet-top",
                         "show-sheet-right", "show-sheet-bottom"};
    const char* labels[] = {"Left Sheet...", "Top Sheet...", "Right Sheet...",
                            "Bottom Sheet..."};
    for (int i = 0; i < 4; i++) {
        StorySectionAdd(place, component::Button::New(cx, Str(ids[i]))
                                   ->Label(Str(labels[i]))
                                   ->Outline()
                                   ->OnClick(ListenerArg(open, i))
                                   ->IntoEl());
    }
    page->Child(place);

    El* scroll = StorySection(cx, "Scrollable Sheet", nullptr);
    StorySectionBody(scroll)->W(480);
    StorySectionAdd(scroll,
                    component::Button::New(cx, StrL("show-scrollable-sheet"))
                        ->Label(StrL("Scrollable Sheet..."))
                        ->Outline()
                        ->OnClick(ListenerArg(open, SheetScrollable))
                        ->IntoEl());
    page->Child(scroll);

    // w_128 with the section gap: the input fills the row and the button
    // wraps under it.
    El* focus = StorySection(cx, "Focus back test", nullptr);
    StorySectionBody(focus)->W(512);
    StorySectionAdd(focus, component::Input::New(cx, StrL("sheet-focus-input"),
                                                 &self->focusInput)
                               ->OnFocus(Listen(cx, &FocusSheetInput))
                               ->IntoEl()
                               ->W(512));
    StorySectionAdd(focus,
                    component::Button::New(cx, StrL("test-action"))
                        ->Label(StrL("Test Action"))
                        ->Outline()
                        ->Tooltip(StrL("This button for test dispatch action, "
                                       "to make sure when Dialog close,\nthis "
                                       "still can handle the action."))
                        ->OnClick(Listen(cx, &SheetTestAction))
                        ->IntoEl());
    page->Child(focus);
    if (self->selectedFood >= 0) {
        page->Child(
            Div(a)
                ->FlexRow()
                ->Gap(4)
                ->Child(
                    StoryTxt(cx, StrL("You have selected:"), 16, th.foreground))
                ->Child(StoryTxt(cx, Str(kSheetFoods[self->selectedFood]), 16,
                                 Rgb(0xff, 0, 0))));
    }

    if (self->open != SheetNone) {
        component::SheetPlacement placement =
            self->open == SheetLeft     ? component::SheetPlacement::Left
            : self->open == SheetTop    ? component::SheetPlacement::Top
            : self->open == SheetBottom ? component::SheetPlacement::Bottom
                                        : component::SheetPlacement::Right;
        El* body = Div(a)->FlexCol()->Gap(12)->W(kFill)->H(kFill);
        El* footer = nullptr;
        if (self->open == SheetScrollable) {
            // One block of text repeated a hundred and fifty times, so the
            // lines sit on their own leading and nothing else.
            body->Gap(0);
            for (int i = 0; i < 150; i++) {
                body->Child(StoryTxt(cx, StrL("This is a scrollable sheet."),
                                     16, th.foreground));
            }
        } else {
            body->Child(
                component::Input::New(cx, StrL("sheet-name"), &self->nameInput)
                    ->OnFocus(Listen(cx, &FocusSheetName))
                    ->IntoEl());
            body->Child(component::DatePicker::New(cx)
                            ->Year(self->birthday.year)
                            ->Month(self->birthday.month)
                            ->Day(self->birthday.day)
                            ->Placeholder(StrL("Date of Birth"))
                            ->W(kFill)
                            ->Open(self->birthdayOpen)
                            ->OnToggle(Listen(cx, &ToggleSheetBirthday))
                            ->OnDay(Listen(cx, &PickSheetBirthday))
                            ->IntoEl());
            body->Child(component::Button::New(cx, StrL("send-notification"))
                            ->Label(StrL("Test Notification"))
                            ->OnClick(Listen(cx, &SheetNotify))
                            ->IntoEl());
            body->Child(
                component::Button::New(cx, StrL("confirm-dialog-from-sheet"))
                    ->Label(StrL("Open Confirm Dialog"))
                    ->OnClick(Listen(cx, &OpenSheetConfirm))
                    ->IntoEl());
            // List::new(&list).border_1().rounded(radius), searchable.
            int counts[1] = {self->nMatches};
            component::List* list =
                component::List::New(cx, StrL("sheet-foods"), self->foods)
                    ->Items(self, &SheetFoodRow)
                    ->Searchable(&self->foodSearch,
                                 Listen(cx, &FocusSheetSearch));
            list->Sections(counts, 1);
            if (self->nMatches == 0) {
                list->Empty(SheetFoodsEmpty(cx));
            }
            // It takes what the sheet has left over its four controls and
            // virtualizes against the box it was laid out in.
            body->Child(list->IntoEl()
                            ->Flex1()
                            ->MinH(0)
                            ->Border(1, th.border)
                            ->Radius(th.radius));
            footer = Div(a)->FlexRow()->Gap(24)->ItemsCenter();
            footer->Child(component::Button::New(cx, StrL("confirm"))
                              ->Primary()
                              ->Label(StrL("Confirm"))
                              ->OnClick(Listen(cx, &CloseSheet))
                              ->IntoEl());
            footer->Child(component::Button::New(cx, StrL("cancel"))
                              ->Label(StrL("Cancel"))
                              ->OnClick(Listen(cx, &CloseSheet))
                              ->IntoEl());
        }
        component::Sheet* sheet =
            component::Sheet::New(cx)
                ->Open(true)
                ->Title(self->open == SheetScrollable ? StrL("Scrollable Sheet")
                                                      : StrL("Sheet Title"))
                ->Placement(placement)
                // open_sheet_at: 400 along a side, 540 top or bottom. The
                // scrollable one asks for nothing and keeps the default.
                ->Size(self->open == SheetScrollable ? 350.f
                       : (placement == component::SheetPlacement::Left ||
                          placement == component::SheetPlacement::Right)
                           ? 400.f
                           : 540.f)
                ->Overlay(self->overlay)
                ->OverlayClosable(self->overlayClosable)
                ->Body(body)
                ->Scroll(7, self->sheetScrollY, Listen(cx, &OnSheetScroll))
                ->OnClose(Listen(cx, &CloseSheet));
        if (footer) {
            sheet->Footer(footer);
        }
        page->Child(sheet->IntoEl(size));
    }
    return page;
}

STORY_PAGE(StorySheet, SheetStory);
