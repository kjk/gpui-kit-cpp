#include "Story.h"

struct NativeMenuStory {
    // The one row of the demo menu that carries state: "Word Wrap", which
    // starts on and the story's on_click flips.
    bool wordWrap = true;

    static El* Render(NativeMenuStory* self, Ctx* cx);
};

// demo_menu: plain items, icons (a named one, the Github row that opens
// github.com, and embedded SVG bytes), a disabled item, the checked Word
// Wrap the story toggles, and nested submenus.
enum {
    NmCut = 1,
    NmCopy,
    NmPaste,
    NmGithub,
    NmInbox,
    NmSearch,
    NmDisabled,
    NmWordWrap,
    NmProjectA,
    NmProjectB,
    NmProjectC,
    NmProjectD,
    NmSelectAll,
};

static component::NativeMenu* DemoMenu(Ctx* cx, NativeMenuStory* self) {
    // `Icon::default().data(include_bytes!("../../../assets/.../search.svg"))`:
    // the same lucide file, embedded rather than looked up.
    static const char kSearchSvg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" "
        "viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
        "<path d=\"m21 21-4.34-4.34\"/><circle cx=\"11\" cy=\"11\" r=\"8\"/>"
        "</svg>";
    component::NativeMenu* more = component::NativeMenu::New(cx)
                                      ->Menu(StrL("project-c"), NmProjectC)
                                      ->Menu(StrL("project-d"), NmProjectD);
    component::NativeMenu* recent = component::NativeMenu::New(cx)
                                        ->Menu(StrL("project-a"), NmProjectA)
                                        ->Menu(StrL("project-b"), NmProjectB)
                                        ->Separator()
                                        ->Submenu(StrL("More"), more);
    return component::NativeMenu::New(cx)
        ->Menu(StrL("Cut"), NmCut)
        ->Menu(StrL("Copy"), NmCopy)
        ->Menu(StrL("Paste"), NmPaste)
        ->Separator()
        ->MenuWithIcon(StrL("Github"), IconName::Github, NmGithub)
        ->MenuWithIcon(StrL("Inbox"), IconName::Inbox, NmInbox)
        ->MenuWithIcon(StrL("Search (SVG bytes)"),
                       component::Icon::Empty(cx)->Data(Str(kSearchSvg)),
                       NmSearch)
        ->Separator()
        ->MenuWithDisabled(StrL("Disabled item"), true, NmDisabled)
        ->MenuWithCheck(StrL("Word Wrap"), self->wordWrap, NmWordWrap)
        ->Separator()
        ->Submenu(StrL("Open Recent"), recent)
        ->Separator()
        ->Menu(StrL("Select All"), NmSelectAll);
}

// on_click: only "Word Wrap" changes anything; open_github opens the site.
static void OnMenuSelect(NativeMenuStory* self, Ctx* cx, const ClickEvent*,
                         int64_t id) {
    if (id == NmWordWrap) {
        self->wordWrap = !self->wordWrap;
    } else if (id == NmGithub) {
        OpenUrl(StrL("https://github.com"));
    }
    Notify(cx);
}

// The second section reuses a plain GPUI menu definition: Copy, Paste, a
// separator and a Share submenu.
static component::NativeMenu* EditMenu(Ctx* cx) {
    component::NativeMenu* share = component::NativeMenu::New(cx)
                                       ->Menu(StrL("Email"), 20)
                                       ->Menu(StrL("Message"), 21);
    return component::NativeMenu::New(cx)
        ->Menu(StrL("Copy"), 22)
        ->Menu(StrL("Paste"), 23)
        ->Separator()
        ->Submenu(StrL("Share"), share);
}

// on_mouse_down(MouseButton::Right, ..): the menu opens where the pointer is,
// nudged right so the pointer does not land on the first row. The OS draws
// it where it has a menu of its own; elsewhere Show draws the same rows.
static void OnTriggerDown(NativeMenuStory* self, Ctx* cx,
                          const MouseDownEvent* ev, int64_t which) {
    if (ev->button != MouseButton::Right) {
        return;
    }
    component::NativeMenu* menu =
        which == 1 ? EditMenu(cx) : DemoMenu(cx, self);
    menu->OnSelect(Listen(cx, &OnMenuSelect));
    menu->Show(ev->x + 4.f, ev->y);
}

// The dropdown: `show` takes any window position, so the menu opens at the
// button's bottom-left, 8px below it, like a real dropdown.
static void OnDropdownClick(NativeMenuStory* self, Ctx* cx,
                            const ClickEvent* ev) {
    component::NativeMenu* menu = DemoMenu(cx, self);
    menu->OnSelect(Listen(cx, &OnMenuSelect));
    menu->Show(ev->el.x, ev->el.y + ev->el.h + 8.f);
}

// trigger(): a 96px box with muted centered text.
static El* NativeTrigger(Ctx* cx, const char* label, int which) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* box = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->H(96)
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->Radius(th.radiusLg)
                  ->Border(1, th.border)
                  ->Child(StoryTxt(cx, Str(label), 16, th.mutedFg));
    box->Click(HashClickId(StoryFmt(cx, "native-trigger-%d", which)))
        ->OnMouseDown(ListenerArg(Listen(cx, &OnTriggerDown), which));
    return box;
}

El* NativeMenuStory::Render(NativeMenuStory*, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill)->ItemsCenter();

    El* builder =
        StorySection(cx, "Builder API",
                     "Supports disabled items, checked states, and submenus.");
    StorySectionBody(builder)->W(520);
    StorySectionAdd(builder, NativeTrigger(cx, "Right-click here", 0));
    page->Child(builder);

    El* items =
        StorySection(cx, "Menu Items",
                     "Existing GPUI menu definitions can be reused directly.");
    StorySectionBody(items)->W(520);
    StorySectionAdd(items, NativeTrigger(cx, "Right-click here", 1));
    page->Child(items);

    El* drop = StorySection(cx, "Dropdown",
                            "A native menu can open from any anchored "
                            "control.");
    StorySectionBody(drop)->W(520);
    El* wrap = Div(a)->FlexCol()->ItemsCenter();
    wrap->Child(component::Button::New(cx, StrL("native-dropdown"))
                    ->Label(StrL("Open Menu"))
                    ->Outline()
                    ->OnClick(Listen(cx, &OnDropdownClick))
                    ->IntoEl());
    StorySectionAdd(drop, wrap);
    page->Child(drop);
    return page;
}

STORY_PAGE(StoryNativeMenu, NativeMenuStory);
