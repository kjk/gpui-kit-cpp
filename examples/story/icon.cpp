#include "Story.h"

// `include_bytes!("../../../assets/assets/icons/search.svg")` and its two
// companions: the same lucide files, embedded rather than looked up, which is
// what `Icon::Data` is for.
static const char kSearchSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" "
    "viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
    "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<circle cx=\"11\" cy=\"11\" r=\"8\"/><path d=\"m21 21-4.3-4.3\"/></svg>";
static const char kArrowUpSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" "
    "viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
    "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"m5 12 7-7 7 7\"/><path d=\"M12 19V5\"/></svg>";
static const char kLoaderSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" "
    "viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
    "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M21 12a9 9 0 1 1-6.219-8.56\"/></svg>";

struct IconStory {
    // What the last button or menu item did with the embedded icon.
    const char* message = "Choose a button or menu item to try the icon slots.";

    static El* Render(IconStory* self, Ctx* cx);
    static void OnSearch(IconStory* self, Ctx* cx, const ClickEvent*) {
        self->message = "Search selected from the button.";
        Notify(cx);
    }
    static void OnMenuPick(IconStory* self, Ctx* cx, const ClickEvent*,
                           intptr_t) {
        self->message = "Search selected from the menu.";
        Notify(cx);
    }
};

// Icon::default().data(ARROW_SVG).rotate(radians(FRAC_PI_2)).large(): the
// story keeps one as a value and one as an Entity<Icon> view. An Icon here is
// a frame-arena builder, so both are the same build, drawn twice.
static El* IconArrow(Ctx* cx) {
    return component::Icon::Empty(cx)
        ->Data(Str(kArrowUpSvg))
        ->Rotate(0.25f)
        ->Size(UiSize::Large)
        ->IntoEl();
}

// Every section is .w(px(480.)).
static El* IconSection(Ctx* cx, const char* title, const char* desc) {
    El* sec = StorySection(cx, title, desc);
    StorySectionBody(sec)->W(480);
    return sec;
}

El* IconStory::Render(IconStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->ItemsCenter()->Gap(24)->W(kFill);

    // section("SVG bytes"): embedded icons share the same sizing, colors and
    // loading behavior as the named ones — small and large, the button slot,
    // a custom loading icon, and a menu item's icon slot.
    El* bytes = IconSection(cx, "SVG bytes",
                            "Embedded icons share the same sizing, colors, and "
                            "loading behavior.");
    El* bytesRow = Div(a)->FlexRow()->Gap(16)->ItemsCenter();
    bytesRow->Child(component::Icon::Empty(cx)
                        ->Data(Str(kSearchSvg))
                        ->Size(UiSize::Small)
                        ->IntoEl());
    bytesRow->Child(component::Icon::Empty(cx)
                        ->Data(Str(kSearchSvg))
                        ->Size(UiSize::Large)
                        ->Color(th.primary)
                        ->IntoEl());
    bytesRow
        ->Child(component::Button::New(cx, StrL("embedded-search"))
                    ->Icon(component::ButtonIcon::New(
                        cx, component::Icon::Empty(cx)->Data(Str(kSearchSvg))))
                    ->Label(StrL("Search"))
                    ->OnClick(Listen(cx, &IconStory::OnSearch))
                    ->IntoEl());
    bytesRow->Child(
        component::Button::New(cx, StrL("embedded-loading"))
            ->Icon(component::ButtonIcon::New(cx, component::Icon::Empty(cx)
                                                      ->Data(Str(kSearchSvg)))
                       ->LoadingIcon(component::Icon::Empty(cx)
                                         ->Data(Str(kLoaderSvg))))
            ->Loading(true)
            ->Label(StrL("Searching"))
            ->IntoEl());
    Str menuId = StrL("embedded-menu-popup");
    Entity<PopupMenuState> menuState = component::PopupMenuStateFor(cx, menuId);
    if (PopupMenuState* ms = menuState.Get(cx)) {
        ms->onConfirm = Listen(cx, &IconStory::OnMenuPick);
    }
    bytesRow->Child(
        component::DropdownMenu::New(cx, StrL("embedded-menu-dropdown"))
            ->Trigger(component::Button::New(cx, StrL("embedded-menu"))
                          ->Label(StrL("Actions"))
                          ->IntoEl())
            ->Menu(component::PopupMenu::New(cx, menuId, menuState)
                       ->Menu(StrL("Search"), component::Icon::Empty(cx)
                                                  ->Data(Str(kSearchSvg))))
            ->IntoEl());
    // v_flex().gap_3(): the row, then the message under it.
    El* bytesCol = Div(a)->FlexCol()->Gap(12);
    bytesCol->Child(bytesRow);
    bytesCol->Child(StoryTxt(cx, Str(self->message), 14, th.mutedFg));
    StorySectionAdd(bytes, bytesCol);
    page->Child(bytes);

    El* cloning = IconSection(cx, "Cloning and views",
                              "Both arrows retain their SVG bytes and 90° "
                              "rotation.");
    StorySectionAdd(cloning, Div(a)
                                 ->FlexRow()
                                 ->Gap(16)
                                 ->ItemsCenter()
                                 ->Child(TextEl(a, StrL("Clone")))
                                 ->Child(IconArrow(cx)));
    StorySectionAdd(cloning, Div(a)
                                 ->FlexRow()
                                 ->Gap(16)
                                 ->ItemsCenter()
                                 ->Child(TextEl(a, StrL("Entity")))
                                 ->Child(IconArrow(cx)));
    page->Child(cloning);

    // The icons are children of the section itself, which wraps them at
    // gap_4; .text_lg() is what sizes them, since an Icon is as big as the
    // text it inherits.
    El* icons = IconSection(
        cx, "Icons", "Common interface symbols from the bundled icon set.");
    StorySectionBody(icons)->Font(18);
    static const IconName kNames[] = {
        IconName::Info,     IconName::Map,   IconName::Bot,  IconName::Github,
        IconName::Calendar, IconName::Globe, IconName::Heart};
    for (IconName n : kNames) {
        StorySectionAdd(icons, IconEl(a, n, 18)->Fg(th.foreground));
    }
    page->Child(icons);

    El* color =
        IconSection(cx, "Color", "Icons inherit semantic foreground colors.");
    StorySectionAdd(color, IconEl(a, IconName::Maximize, 24)->Fg(th.green));
    StorySectionAdd(color, IconEl(a, IconName::Minimize, 24)->Fg(th.red));
    page->Child(color);

    El* btns = IconSection(cx, "Icon Buttons",
                           "Icons can be used as compact button content.");
    El* btnRow = Div(a)->FlexRow()->Gap(16)->ItemsCenter();
    // neutral_500, a red heart-off and a green heart. Each is the button's
    // own ButtonIcon rather than a child, which is what makes the button an
    // icon button — a 32px square. The `.size_6()` the Rust story writes on
    // each Icon does not survive: `.icon()` re-sizes it to the button's own.
    btnRow->Child(component::Button::New(cx, StrL("like1"))
                      ->Ghost()
                      ->Icon(IconName::Heart)
                      ->IconColor(Rgb(0x73, 0x73, 0x73))
                      ->IntoEl());
    btnRow->Child(component::Button::New(cx, StrL("like2"))
                      ->Ghost()
                      ->Icon(IconName::HeartOff)
                      ->IconColor(th.red)
                      ->IntoEl());
    btnRow->Child(component::Button::New(cx, StrL("like3"))
                      ->Ghost()
                      ->Icon(IconName::Heart)
                      ->IconColor(th.green)
                      ->IntoEl());
    StorySectionAdd(btns, btnRow);
    page->Child(btns);

    // Button::size_5().small().px_0(): a 20px square with a label in it.
    El* csz =
        IconSection(cx, "Custom Size",
                    "Explicit dimensions support dense controls and counters.");
    StorySectionAdd(csz, component::Button::New(cx, StrL("button-with-size"))
                             ->Outline()
                             ->Size(20)
                             ->WithSize(UiSize::Small)
                             ->Label(StrL("10"))
                             ->IntoEl());
    page->Child(csz);
    return page;
}

STORY_PAGE(StoryIcon, IconStory);
