#include "Story.h"

// Kbd::format: a menu shows the shortcut the way the platform spells it,
// rather than a string that only reads right on one of them.
static Str Chord(Ctx* cx, const char* key, bool shift = false, bool alt = false,
                 bool platformShortcut = true) {
    component::Keystroke k;
#if GPUI_OS_MAC
    if (platformShortcut) {
        k.platform = true;
    } else {
        k.ctrl = true;
    }
#else
    // Everywhere else the platform shortcut is Ctrl, so the flag has nothing
    // left to choose between.
    (void)platformShortcut;
    k.ctrl = true;
#endif
    k.shift = shift;
    k.alt = alt;
    k.key = Str(key);
    return component::KbdFormatStr(cx, k);
}

enum class CheckSideState : uint8_t {
    None,
    Left,
    Right
};

struct MenuStory {
    CheckSideState checkSide = CheckSideState::None;
    Str message = {};

    ~MenuStory() { StrFree(message); }
    static El* Render(MenuStory* self, Ctx* cx);
    static void OnKey(MenuStory* self, Ctx* cx, const KeyEvent* ev);
};

static void SetMessage(MenuStory* self, Ctx* cx, Str message) {
    StrFree(self->message);
    self->message = StrDup(message);
    Notify(cx);
}

static void SetClickedMessage(MenuStory* self, Ctx* cx, const char* item) {
    SetMessage(self, cx, fmt("You have clicked %s", Str(item)));
}

static const char* CheckSideName(CheckSideState side) {
    switch (side) {
        case CheckSideState::Left:
            return "Some(Left)";
        case CheckSideState::Right:
            return "Some(Right)";
        default:
            return "None";
    }
}

static Side MenuCheckSide(const MenuStory* self) {
    return self->checkSide == CheckSideState::Right ? Side::Right : Side::Left;
}

static void ToggleCheck(MenuStory* self, Ctx* cx) {
    if (self->checkSide == CheckSideState::Left) {
        self->checkSide = CheckSideState::Right;
    } else if (self->checkSide == CheckSideState::Right) {
        self->checkSide = CheckSideState::None;
    } else {
        self->checkSide = CheckSideState::Left;
    }
    SetMessage(self, cx,
               fmt("You have used check at side: %s",
                   Str(CheckSideName(self->checkSide))));
}

static El* SectionRow(Ctx* cx, float width) {
    return Div(cx->a)
        ->FlexRow()
        ->FlexWrap()
        ->W(width)
        ->Gap(16)
        ->JustifyCenter()
        ->ItemsCenter();
}

static El* SectionColumn(Ctx* cx, float width) {
    return Div(cx->a)->FlexCol()->W(width)->Gap(16);
}

static void OnPopupItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                        intptr_t ix) {
    switch (ix) {
        case 2:
            SetMessage(self, cx, StrL("You have clicked Handle Click"));
            break;
        case 4:
            SetClickedMessage(self, cx, "copy");
            break;
        case 5:
            SetClickedMessage(self, cx, "cut");
            break;
        case 6:
            SetClickedMessage(self, cx, "paste");
            break;
        case 8:
        case 13:
            ToggleCheck(self, cx);
            break;
        case 10:
            SetClickedMessage(self, cx, "search all");
            break;
        case 12:
            SetMessage(self, cx, StrL("You have clicked on custom element"));
            break;
        case 14:
            SetMessage(self, cx, StrL("You have clicked info: 0"));
            break;
    }
}

static component::PopupMenu* LinksMenu(MenuStory*, Ctx* cx) {
    return component::PopupMenu::New(cx, StrL("popup-menu-links"))
        ->Link(StrL("GPUI Kit"), StrL("https://github.com/longbridge/gpui-kit"),
               IconName::Github)
        ->Separator()
        ->Link(StrL("GPUI"), StrL("https://gpui.rs"))
        ->Link(StrL("Zed"), StrL("https://zed.dev"));
}

static component::PopupMenu* OtherLinksMenu(MenuStory* self, Ctx* cx) {
    component::PopupMenu* deeper =
        component::PopupMenu::New(cx, StrL("popup-menu-other-deeper"))
            ->Link(StrL("GPUI"), StrL("https://gpui.rs"));

    component::PopupMenu* nested =
        component::PopupMenu::New(cx, StrL("popup-menu-other-nested"))
            ->Link(StrL("Docs.rs"), StrL("https://docs.rs"))
            ->Separator()
            ->Submenu(StrL("Deeper"), deeper);

    component::PopupMenu* menu =
        component::PopupMenu::New(cx, StrL("popup-menu-other-links"))
            ->Link(StrL("Crates"), StrL("https://crates.io"))
            ->Link(StrL("Rust Docs"), StrL("https://docs.rs"))
            ->Separator()
            ->Submenu(StrL("Nested"), nested);
    (void)self;
    return menu;
}

static component::PopupMenu* PopupStoryMenu(MenuStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    bool checked = self->checkSide != CheckSideState::None;
    component::PopupMenu* menu =
        component::PopupMenu::New(cx, StrL("popup-menu-main"))
            ->MinW(250)
            // Those actions live in the field's key context, which is where
            // the shortcut beside each row is looked up.
            ->ActionContext("Input")
            ->Link(StrL("About"),
                   StrL("https://github.com/longbridge/gpui-kit"))
            ->CheckSide(MenuCheckSide(self))
            ->Separator()
            ->Menu(StrL("Handle Click"))
            ->Separator()
            // The three rows name the actions rather than a keystroke:
            // the shortcut shown is whatever the keymap has bound to each,
            // and choosing one dispatches it to the focused field the way
            // the chord would.
            ->MenuWithAction(StrL("Copy"), input::Copy())
            ->MenuWithAction(StrL("Cut"), input::Cut())
            ->MenuWithAction(StrL("Paste"), input::Paste())
            ->Separator()
            ->MenuWithCheck(
                StoryFmt(cx, "Check Side %s", CheckSideName(self->checkSide)),
                checked)
            ->Kbd(Chord(cx, "t", true, true, false))
            ->Separator()
            ->MenuWithAction(StrL("Search"), input::Search())
            ->Icon(IconName::Search)
            ->Separator();

    El* custom = Div(a)->FlexCol();
    custom->Child(StoryTxt(cx, StrL("Custom Element"), 14, th.foreground));
    custom->Child(StoryTxt(cx, StrL("This is sub-title"), 12, th.mutedFg));
    menu->Element(custom);

    El* checkedElement = Div(a)->FlexRow()->Gap(4)->ItemsCenter();
    checkedElement
        ->Child(StoryTxt(cx, StrL("Custom Element"), 14, th.foreground));
    checkedElement->Child(StoryTxt(cx, StrL("checked"), 12, th.mutedFg));
    menu->Element(checkedElement)->Checked(checked);

    El* iconElement = Div(a)->FlexRow()->Gap(4)->ItemsCenter();
    iconElement->Child(StoryTxt(cx, StrL("Custom"), 14, th.foreground));
    iconElement->Child(StoryTxt(cx, StrL("element"), 14, th.mutedFg));
    menu->Element(iconElement)->Icon(IconName::Info);

    menu->Separator()
        ->Menu(StrL("Disabled Item"))
        ->Disabled(true)
        ->Separator()
        ->Submenu(StrL("Links"), LinksMenu(self, cx))
        ->Separator()
        ->Submenu(StrL("Other Links"), OtherLinksMenu(self, cx));
    PopupMenuState* state = menu->state.Get(cx);
    if (state) {
        state->onConfirm = Listen(cx, &OnPopupItem);
    }
    return menu;
}

static void OnInfo(MenuStory* self, Ctx* cx, int info) {
    SetMessage(self, cx, fmt("You have clicked info: %d", info));
}

static void OnSettingsItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                           intptr_t ix) {
    if (ix == 0) {
        OnInfo(self, cx, 0);
    } else if (ix == 2) {
        OnInfo(self, cx, 1);
    } else if (ix == 3) {
        OnInfo(self, cx, 2);
    }
}

static void OnMoreItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                       intptr_t ix) {
    if (ix == 0) {
        OnInfo(self, cx, 1);
    } else if (ix == 1) {
        OnInfo(self, cx, 2);
    }
}

static void OnEvenMoreItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                           intptr_t ix) {
    OnMoreItem(self, cx, nullptr, ix);
}

static void OnDeepestItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                          intptr_t ix) {
    if (ix == 0) {
        OnInfo(self, cx, 1);
    } else if (ix == 1) {
        OnInfo(self, cx, 2);
    }
}

static component::PopupMenu* SettingsMenu(MenuStory*, Ctx* cx) {
    component::PopupMenu* deepest =
        component::PopupMenu::New(cx, StrL("context-settings-deepest"))
            ->Menu(StrL("Leaf 1"))
            ->Menu(StrL("Leaf 2"));
    PopupMenuState* state = deepest->state.Get(cx);
    if (state) {
        state->onConfirm = Listen(cx, &OnDeepestItem);
    }

    component::PopupMenu* evenMore =
        component::PopupMenu::New(cx, StrL("context-settings-even-more"))
            ->Menu(StrL("Deep Item 1"))
            ->Menu(StrL("Deep Item 2"))
            ->Separator()
            ->Submenu(StrL("Deepest"), deepest);
    state = evenMore->state.Get(cx);
    if (state) {
        state->onConfirm = Listen(cx, &OnEvenMoreItem);
    }

    component::PopupMenu* more =
        component::PopupMenu::New(cx, StrL("context-settings-more"))
            ->Menu(StrL("More Item 1"))
            ->Menu(StrL("More Item 2"))
            ->Separator()
            ->Submenu(StrL("Even More"), evenMore);
    state = more->state.Get(cx);
    if (state) {
        state->onConfirm = Listen(cx, &OnMoreItem);
    }

    component::PopupMenu* settings =
        component::PopupMenu::New(cx, StrL("context-settings"))
            ->Menu(StrL("Info 0"))
            ->Separator()
            ->Menu(StrL("Item 1"))
            ->Menu(StrL("Item 2"))
            ->Separator()
            ->Submenu(StrL("More"), more);
    state = settings->state.Get(cx);
    if (state) {
        state->onConfirm = Listen(cx, &OnSettingsItem);
    }
    return settings;
}

static void OnContextMainItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                              intptr_t ix) {
    switch (ix) {
        case 2:
            SetClickedMessage(self, cx, "cut");
            break;
        case 3:
            SetClickedMessage(self, cx, "copy");
            break;
        case 4:
            SetClickedMessage(self, cx, "paste");
            break;
        case 7:
            ToggleCheck(self, cx);
            break;
        case 11:
            SetClickedMessage(self, cx, "search all");
            break;
    }
}

static component::PopupMenu* MainContextMenu(MenuStory* self, Ctx* cx) {
    bool checked = self->checkSide != CheckSideState::None;
    component::PopupMenu* menu =
        component::PopupMenu::New(cx, StrL("context-main-menu"))
            ->CheckSide(MenuCheckSide(self))
            ->ExternalLinkIcon(false)
            ->Link(StrL("About"),
                   StrL("https://github.com/longbridge/gpui-kit"))
            ->Separator()
            ->MenuWithKbd(StrL("Cut"), Chord(cx, "x"))
            ->MenuWithKbd(StrL("Copy"), Chord(cx, "c"))
            ->MenuWithKbd(StrL("Paste"), Chord(cx, "v"))
            ->Separator()
            ->Label(StrL("This is a label"))
            ->MenuWithCheck(
                StoryFmt(cx, "Check Side %s", CheckSideName(self->checkSide)),
                checked)
            ->Kbd(Chord(cx, "t", true, true, false))
            ->Separator()
            ->Submenu(StrL("Settings"), SettingsMenu(self, cx))
            ->Separator()
            ->MenuWithKbd(StrL("Search All"), Chord(cx, "f", true))
            ->Separator();
    PopupMenuState* state = menu->state.Get(cx);
    if (state) {
        state->onConfirm = Listen(cx, &OnContextMainItem);
    }
    return menu;
}

static void OnOtherContextItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                               intptr_t ix) {
    if (ix == 2) {
        OnInfo(self, cx, 1);
    }
}

static component::PopupMenu* OtherContextMenu(MenuStory*, Ctx* cx, int area) {
    component::PopupMenu* menu = component::PopupMenu::New(
        cx, StoryFmt(cx, "context-other-menu-%d", area));
    menu->Link(StrL("About"), StrL("https://github.com/longbridge/gpui-kit"))
        ->Separator()
        ->Menu(StrL("Item 1"));
    PopupMenuState* state = menu->state.Get(cx);
    if (state) {
        state->onConfirm = Listen(cx, &OnOtherContextItem);
    }
    return menu;
}

static El* ContextArea(Ctx* cx, Str id, Str title, Str hint,
                       component::PopupMenu* menu) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* box = Div(a)
                  ->W(kFill)
                  ->Pad(16)
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->MinH(80)
                  ->Radius(th.radiusLg)
                  ->Border(2, th.border)
                  ->Dashed();
    if (hint.s) {
        box->FlexCol();
    } else {
        box->FlexRow();
    }
    box->Child(StoryTxt(cx, title, 14, th.foreground));
    if (hint.s) {
        box->Child(StoryTxt(cx, hint, 14, th.mutedFg));
    }
    return component::ContextMenu::New(cx, id)
        ->Child(box)
        ->Menu(menu)
        ->IntoEl();
}

static void OnScrollableItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                             intptr_t ix) {
    for (int item = 0; item < 100; item++) {
        int row = 2 + item + item / 5;
        if (row == ix) {
            OnInfo(self, cx, item);
            return;
        }
    }
}

static void OnShortScrollableItem(MenuStory* self, Ctx* cx, const ClickEvent*,
                                  intptr_t ix) {
    if (ix >= 1 && ix <= 5) {
        OnInfo(self, cx, (int)ix - 1);
    }
}

static component::PopupMenu* ScrollableMenu(MenuStory*, Ctx* cx, int count) {
    component::PopupMenu* menu =
        component::PopupMenu::New(
            cx, StoryFmt(cx, "dropdown-menu-scrollable-%d", count))
            ->Scrollable()
            ->MaxH(300)
            ->Label(StrL("Total 100 items"));
    for (int i = 0; i < count; i++) {
        if (count == 100 && i % 5 == 0) {
            menu->Separator();
        }
        menu->Menu(StoryFmt(cx, "Item %d", i));
    }
    menu->MinW(100);
    PopupMenuState* state = menu->state.Get(cx);
    if (state) {
        state->onConfirm = count == 100 ? Listen(cx, &OnScrollableItem)
                                        : Listen(cx, &OnShortScrollableItem);
    }
    return menu;
}

void MenuStory::OnKey(MenuStory* self, Ctx* cx, const KeyEvent* ev) {
    if (!ev->down) {
        return;
    }

    // The menus answer their own keys now: each declares the "PopupMenu" key
    // context and hears the actions bound in it, so the page no longer has to
    // rebuild every menu on every keystroke to offer it the chord.
    if (!ev->ctrl) {
        return;
    }
    if (ev->vk == KeyC && !ev->shift && !ev->alt) {
        SetClickedMessage(self, cx, "copy");
    } else if (ev->vk == KeyV && !ev->shift && !ev->alt) {
        SetClickedMessage(self, cx, "paste");
    } else if (ev->vk == KeyX && !ev->shift && !ev->alt) {
        SetClickedMessage(self, cx, "cut");
    } else if (ev->vk == 'F' && ev->shift && !ev->alt) {
        SetClickedMessage(self, cx, "search all");
    } else if (ev->vk == 'T' && ev->shift && ev->alt) {
        ToggleCheck(self, cx);
    }
}

El* MenuStory::Render(MenuStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->SizeFull()->MinH(400)->ItemsCenter()->Gap(24);

    El* popupSection = StorySection(
        cx, "Popup Menu",
        "Supports actions, links, checks, icons, custom rows, and nested "
        "menus.");
    StorySectionBody(popupSection)->W(640);
    El* popupContent = SectionRow(cx, 640);
    popupContent
        ->Child(component::DropdownMenu::New(cx, StrL("popup-menu-1-dropdown"))
                    ->Trigger(component::Button::New(cx, StrL("popup-menu-1"))
                                  ->Outline()
                                  ->Label(StrL("Edit"))
                                  ->IntoEl())
                    ->Menu(PopupStoryMenu(self, cx))
                    ->IntoEl());
    if (self->message.s) {
        popupContent->Child(StoryTxt(cx, self->message, 14, th.foreground));
    }
    StorySectionAdd(popupSection, popupContent);
    page->Child(popupSection);

    El* contextSection = StorySection(
        cx, "Context Menu",
        "Different regions can provide their own right-click actions.");
    StorySectionBody(contextSection)->W(640)->FlexCol()->Gap(16);
    El* contextContent = SectionColumn(cx, 640);
    contextContent->Child(ContextArea(
        cx, StrL("context-main"), StrL("Right click to open ContextMenu"),
        StrL("You can right click anywhere in this area to open the context "
             "menu."),
        MainContextMenu(self, cx)));
    contextContent->Child(ContextArea(
        cx, StrL("other"), StrL("Here is another area with context menu."),
        Str{}, OtherContextMenu(self, cx, 0)));
    contextContent
        ->Child(ContextArea(cx, StrL("other1"), StrL("ContextMenu area 1"),
                            Str{}, OtherContextMenu(self, cx, 1)));
    StorySectionAdd(contextSection, contextContent);
    page->Child(contextSection);

    El* scrollSection = StorySection(
        cx, "Scrollable",
        "Long menus constrain their height while short menus stay compact.");
    StorySectionBody(scrollSection)->W(640);
    El* scrollContent = SectionRow(cx, 640);
    const int counts[] = {100, 5};
    for (int i = 0; i < 2; i++) {
        int count = counts[i];
        scrollContent->Child(
            component::DropdownMenu::New(
                cx, StoryFmt(cx, "scrollable-dropdown-%d", count))
                ->Trigger(
                    component::Button::New(
                        cx, StoryFmt(cx, "dropdown-menu-scrollable-%d", i + 1))
                        ->Outline()
                        ->Label(
                            StoryFmt(cx, "Scrollable Menu (%d items)", count))
                        ->IntoEl())
                ->Menu(ScrollableMenu(self, cx, count))
                ->AnchorRight()
                ->IntoEl());
    }
    StorySectionAdd(scrollSection, scrollContent);
    page->Child(scrollSection);
    return page;
}

STORY_PAGE_KEYS(StoryMenu, MenuStory);
