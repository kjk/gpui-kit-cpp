#include "Story.h"

struct NativeMenuStory {
    // Which trigger has a drawn menu open. Only used where the platform has
    // no menu of its own — elsewhere the OS draws it, over the window and
    // outside it, and this page holds nothing.
    int open = -1;
    // The one row of the demo menu that carries state: "Word Wrap", which
    // starts on and the story's on_click flips.
    bool wordWrap = true;

    static El* Render(NativeMenuStory* self, Ctx* cx);
    static void OnKey(NativeMenuStory* self, Ctx* cx, const KeyEvent* ev);
};

// The rows every trigger on this page opens: a checked row, a greyed one and
// a submenu, which is what the Rust story shows off.
static component::NativeMenu* DemoMenu(Ctx* cx, NativeMenuStory* self) {
    component::NativeMenu* sub =
        component::NativeMenu::New(cx)
            ->MenuWithIcon(StrL("Copy"), IconName::Copy, 10)
            ->Menu(StrL("Cut"), 11)
            ->MenuWithDisabled(StrL("Paste"), true, 12);
    // `Icon::default().data(include_bytes!("../../../assets/.../search.svg"))`:
    // the same lucide file, embedded rather than looked up.
    static const char kSearchSvg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" "
        "viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
        "<circle cx=\"11\" cy=\"11\" r=\"8\"/><path d=\"m21 21-4.3-4.3\"/>"
        "</svg>";
    return component::NativeMenu::New(cx)
        ->MenuWithIcon(StrL("New"), IconName::Plus, 1)
        ->MenuWithIcon(StrL("Open..."), IconName::FolderOpen, 2)
        ->MenuWithIcon(StrL("Search (SVG bytes)"),
                       component::Icon::Empty(cx)->Data(Str(kSearchSvg)), 6)
        ->MenuWithCheck(StrL("Word Wrap"), self->wordWrap, 3)
        ->Separator()
        ->Submenu(StrL("Edit"), sub)
        ->Separator()
        ->MenuWithDisabled(StrL("Save"), true, 4)
        ->MenuWithIcon(StrL("Quit"), IconName::X, 5);
}

// on_click: only "Word Wrap" changes anything.
static void OnMenuSelect(NativeMenuStory* self, Ctx* cx, const ClickEvent*,
                         intptr_t id) {
    if (id == 3) {
        self->wordWrap = !self->wordWrap;
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

// A press that opens the menu where the pointer is. The OS menu is not
// clipped to the window, so it opens at the press; where the platform has
// none, the page draws the same rows under the trigger instead.
static void OnTriggerDown(NativeMenuStory* self, Ctx* cx,
                          const MouseDownEvent* ev, intptr_t which) {
    // on_mouse_down(MouseButton::Right, ..): only the secondary press opens
    // it, and the menu is nudged right so the pointer does not land on the
    // first row.
    if (ev->button != MouseButton::Right && which != 2) {
        return;
    }
    component::NativeMenu* menu =
        which == 1 ? EditMenu(cx) : DemoMenu(cx, self);
    menu->OnSelect(Listen(cx, &OnMenuSelect));
    if (menu->Show(ev->x + (which == 2 ? 0.f : 4.f), ev->y)) {
        self->open = -1;
    } else {
        self->open = self->open == (int)which ? -1 : (int)which;
    }
    Notify(cx);
}

static void OnTriggerClick(NativeMenuStory* self, Ctx* cx, const ClickEvent* ev,
                           intptr_t which) {
    MouseDownEvent down = {};
    down.x = ev->x;
    down.y = ev->y;
    OnTriggerDown(self, cx, &down, which);
}

// The same rows drawn, for a platform with no menu of its own — Rust's
// FallbackMenuOverlay, which Root holds and anchors at the press.
static El* FallbackMenu(Ctx* cx, NativeMenuStory* self, int which) {
    return (which == 1 ? EditMenu(cx) : DemoMenu(cx, self))
        ->IntoPopupMenu(StoryFmt(cx, "native-fallback-%d", which))
        ->IntoEl();
}

// trigger(): a 96px box with muted centered text, with the drawn menu under
// it on the platforms that need one.
static El* NativeTrigger(Ctx* cx, NativeMenuStory* self, const char* label,
                         int which) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* wrap = Div(a)->W(kFill);
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
    wrap->Child(box);
    if (self->open == which) {
        wrap->Child(
            FallbackMenu(cx, self, which)->Absolute()->Top(100)->Left(0));
    }
    return wrap;
}

El* NativeMenuStory::Render(NativeMenuStory* self, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill)->ItemsCenter();

    El* builder =
        StorySection(cx, "Builder API",
                     "Supports disabled items, checked states, and submenus.");
    StorySectionBody(builder)->W(520);
    StorySectionAdd(builder, NativeTrigger(cx, self, "Right-click here", 0));
    page->Child(builder);

    El* items =
        StorySection(cx, "Menu Items",
                     "Existing GPUI menu definitions can be reused directly.");
    StorySectionBody(items)->W(520);
    StorySectionAdd(items, NativeTrigger(cx, self, "Right-click here", 1));
    page->Child(items);

    El* drop = StorySection(cx, "Dropdown",
                            "A native menu can open from any anchored "
                            "control.");
    StorySectionBody(drop)->W(520);
    El* wrap = Div(a)->FlexCol()->ItemsCenter();
    wrap->Child(component::Button::New(cx, StrL("native-dropdown"))
                    ->Label(StrL("Open Menu"))
                    ->Outline()
                    ->OnClick(ListenerArg(Listen(cx, &OnTriggerClick), 2))
                    ->IntoEl());
    if (self->open == 2) {
        wrap->Child(FallbackMenu(cx, self, 2)->Absolute()->Top(36)->Left(0));
    }
    StorySectionAdd(drop, wrap);
    page->Child(drop);
    return page;
}

// Esc closes what this page has open, like an overlay dismiss.
void NativeMenuStory::OnKey(NativeMenuStory* self, Ctx* cx,
                            const KeyEvent* ev) {
    if (ev->vk != KeyEscape) {
        return;
    }
    self->open = -1;
    Notify(cx);
}

STORY_PAGE_KEYS(StoryNativeMenu, NativeMenuStory);
