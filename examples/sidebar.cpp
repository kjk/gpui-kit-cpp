#include "gpui.h"

using namespace gpui;

// The three shadcn collapse modes the sidebar answers to.
static const component::SidebarCollapsible kModes[3] = {
    component::SidebarCollapsible::Icon,
    component::SidebarCollapsible::Offcanvas,
    component::SidebarCollapsible::None};

struct SidebarApp {
    static El* Render(SidebarApp* self, Ctx* cx);
    int mode = 0;
    bool collapsed = false;
    int active = 0;
};

static const char* Description(const SidebarApp* app) {
    if (kModes[app->mode] == component::SidebarCollapsible::Offcanvas) {
        return "The sidebar releases its layout width when collapsed and keeps "
               "hidden controls out of keyboard "
               "navigation, matching shadcn's collapsible=\"offcanvas\" "
               "behavior.";
    }
    if (kModes[app->mode] == component::SidebarCollapsible::None) {
        return "The sidebar ignores the collapsed state and remains expanded, "
               "matching shadcn's collapsible=\"none\" "
               "behavior.";
    }
    return "The sidebar collapses to icon width, matching shadcn's "
           "collapsible=\"icon\" behavior.";
}

static void ToggleCollapse(SidebarApp* app, Ctx* cx, const ClickEvent*) {
    app->collapsed = !app->collapsed;
    Notify(cx);
}

static void SetMode(SidebarApp* app, Ctx* cx, const ClickEvent*,
                    intptr_t mode) {
    app->mode = (int)mode;
    Notify(cx);
}

static void SetActive(SidebarApp* app, Ctx* cx, const ClickEvent*,
                      intptr_t ix) {
    app->active = (int)ix;
    Notify(cx);
}

El* SidebarApp::Render(SidebarApp* app, Ctx* cx) {
    Arena* frame = cx->a;
    const Theme& th = ThemeNow(cx->app);
    bool collapsed = app->collapsed;
    bool iconCollapsed =
        collapsed && kModes[app->mode] == component::SidebarCollapsible::Icon;

    El* root = Div(frame)->FlexRow()->SizeFull()->Bg(th.tokens.background);

    // The header is a logo and the workspace it names.
    El* brand = Div(frame)->FlexRow()->Gap(8)->ItemsCenter();
    brand->Child(Div(frame)
                     ->W(32)
                     ->H(32)
                     ->Shrink0()
                     ->Radius(th.radius)
                     ->Bg(th.tokens.sidebarPrimary)
                     ->ItemsCenter()
                     ->JustifyCenter()
                     ->Child(IconEl(frame, IconName::GalleryVerticalEnd, 16)
                                 ->Fg(th.sidebarPrimaryFg)));
    if (!iconCollapsed) {
        brand->Child(Div(frame)
                         ->FlexCol()
                         ->Flex1()
                         ->Child(TextEl(frame, StrL("Acme Inc"))->Font(14))
                         ->Child(TextEl(frame, StrL("Enterprise"))
                                     ->Font(12)
                                     ->Fg(th.mutedFg)));
    }

    Listener select = Listen(cx, &SetActive);
    component::SidebarMenu* menu = component::SidebarMenu::New(cx);
    menu->Child(component::SidebarMenuItem::New(cx, StrL("Dashboard"))
                    ->Icon(IconName::LayoutDashboard)
                    ->Active(app->active == 0)
                    ->OnClick(ListenerArg(select, 0)));
    menu->Child(component::SidebarMenuItem::New(cx, StrL("Inbox"))
                    ->Icon(IconName::Inbox)
                    ->Active(app->active == 1)
                    ->OnClick(ListenerArg(select, 1)));
    menu->Child(component::SidebarMenuItem::New(cx, StrL("Calendar"))
                    ->Icon(IconName::Calendar)
                    ->Active(app->active == 2)
                    ->OnClick(ListenerArg(select, 2)));
    // A submenu: the caret opens it, and so does the item, because it says
    // click_to_toggle.
    menu->Child(
        component::SidebarMenuItem::New(cx, StrL("Projects"))
            ->Icon(IconName::Folder)
            ->DefaultOpen(true)
            ->ClickToToggle(true)
            ->Child(component::SidebarMenuItem::New(cx, StrL("Design"))
                        ->Active(app->active == 3)
                        ->OnClick(ListenerArg(select, 3)))
            ->Child(component::SidebarMenuItem::New(cx, StrL("Engineering"))
                        ->Active(app->active == 4)
                        ->OnClick(ListenerArg(select, 4)))
            ->Child(component::SidebarMenuItem::New(cx, StrL("Marketing"))
                        ->Active(app->active == 5)
                        ->OnClick(ListenerArg(select, 5))));
    menu->Child(component::SidebarMenuItem::New(cx, StrL("Settings"))
                    ->Icon(IconName::Settings)
                    ->Active(app->active == 6)
                    ->OnClick(ListenerArg(select, 6)));

    El* user = Div(frame)->FlexRow()->Gap(8)->ItemsCenter();
    user->Child(IconEl(frame, IconName::CircleUser, 16));
    if (!iconCollapsed) {
        user->Child(TextEl(frame, StrL("Jason Lee"))->Font(14));
    }

    root->Child(
        component::Sidebar::New(cx, StrL("sidebar"))
            ->Collapsible(kModes[app->mode])
            ->Collapsed(collapsed)
            ->Header(component::SidebarHeader::New(cx)->Child(brand))
            ->Footer(component::SidebarFooter::New(cx)->Child(user))
            ->Child(component::SidebarGroup::New(cx, StrL("Application"))
                        ->Child(menu))
            ->IntoEl());

    El* main = Div(frame)->FlexCol()->Flex1()->H(kFill)->Pad(16)->Gap(16);
    El* top = Div(frame)->FlexRow()->ItemsCenter()->Gap(12);
    if (kModes[app->mode] != component::SidebarCollapsible::None) {
        top->Child(component::SidebarToggleButton::New(cx)
                       ->Collapsed(collapsed)
                       ->OnClick(Listen(cx, &ToggleCollapse))
                       ->IntoEl());
    }
    top->Child(TextEl(frame, StrL("Sidebar collapsible modes"))
                   ->Font(16)
                   ->Bold()
                   ->Fg(th.foreground));
    main->Child(top);

    static const char* kModeNames[3] = {"Icon", "Offcanvas", "None"};
    El* modes = Div(frame)->FlexRow()->ItemsCenter()->Gap(8);
    modes->Child(TextEl(frame, StrL("Mode:"))->Font(14)->Fg(th.foreground));
    Listener setMode = Listen(cx, &SetMode);
    for (int i = 0; i < 3; i++) {
        modes->Child(component::Button::New(cx, Str(kModeNames[i]))
                         ->Label(Str(kModeNames[i]))
                         ->WithSize(UiSize::Small)
                         ->Selected(app->mode == i)
                         ->OnClick(ListenerArg(setMode, i))
                         ->IntoEl());
    }
    main->Child(modes);
    main->Child(Div(frame)
                    ->Flex1()
                    ->Radius(th.radius)
                    ->Border(1, th.border)
                    ->Pad(20)
                    ->Child(TextEl(frame, Str(Description(app)))
                                ->Font(14)
                                ->Fg(th.foreground)
                                ->Wrap()));
    root->Child(main);
    return root;
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    Entity<SidebarApp> view = EntityNew<SidebarApp>(app);
    ThemeSet(app, ThemeMode::Light);
    AssetsClear();
    AssetsAddDefaultRoots(Str{});
    AssetsAddRoot(StrL("assets"));
    WinOpts opts = {};
    KitOpenWindow(app, StrL("Sidebar C++"), 900, 620, view.id, opts);
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
