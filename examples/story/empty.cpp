#include "Story.h"

using namespace gpui;

struct EmptyStory {
    // The default example's project and the Search example's two fields are
    // independent state, as in empty_story.rs.
    int project = 0; // 0: empty, 1: new project, 2: imported sample
    InputState search;
    InputState pageSearch;
    int dialog = 0;
    bool seeded = false;

    static void Create(EmptyStory* self, Ctx* cx, const ClickEvent*) {
        self->project = 1;
        Notify(cx);
    }
    static void Import(EmptyStory* self, Ctx* cx, const ClickEvent*) {
        self->project = 2;
        Notify(cx);
    }
    static void Reset(EmptyStory* self, Ctx* cx, const ClickEvent*) {
        self->project = 0;
        Notify(cx);
    }
    static void Open(EmptyStory* self, Ctx* cx, const ClickEvent*, intptr_t id) {
        self->dialog = (int)id;
        Notify(cx);
    }
    static void Close(EmptyStory* self, Ctx* cx, const ClickEvent*) {
        self->dialog = 0;
        Notify(cx);
    }
    static void ClearSearch(EmptyStory* self, Ctx* cx, const ClickEvent*) {
        InputSetValue(&self->search, StrL(""));
        Notify(cx);
    }
    static void SearchChanged(EmptyStory*, Ctx* cx, const ClickEvent*) {
        Notify(cx);
    }
    static void Refresh(EmptyStory*, Ctx* cx, const ClickEvent*) {
        StoryPushNotification(cx, StrL("No new notifications"));
    }
    static El* Render(EmptyStory* self, Ctx* cx);
};

static component::EmptyHeader* EmptyHead(Ctx* cx, IconName icon,
                                         const char* title, const char* desc) {
    using namespace component;
    EmptyHeader* h = EmptyHeader::New(cx);
    if (icon != IconName::None) {
        h->Media(EmptyMedia::New(cx)
                     ->WithVariant(EmptyMediaVariant::Icon)
                     ->Child(IconEl(cx->a, icon, 16)));
    }
    h->Title(EmptyTitle::New(cx)->Child(TextEl(cx->a, Str(title))));
    h->Description(EmptyDescription::New(cx)->Child(
        TextEl(cx->a, Str(desc))->Wrap()));
    return h;
}

static El* EmptySection(Ctx* cx, const char* title, const char* desc,
                        El* child) {
    El* section = StorySection(cx, title, desc);
    StorySectionAdd(section, child);
    return section;
}

static El* EmptyAction(Ctx* cx, Str id, Str label, Listener fn,
                       bool outline = false, IconName icon = IconName::None) {
    component::Button* button =
        component::Button::New(cx, id)->Label(label)->OnClick(fn);
    if (outline) button->Outline();
    if (icon != IconName::None) button->Icon(icon);
    return button->IntoEl();
}

El* EmptyStory::Render(EmptyStory* self, Ctx* cx) {
    using namespace component;
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        InputSetPlaceholder(&self->search, StrL("Search projects…"));
        InputSetValue(&self->search, StrL("Archive"));
        InputSetPlaceholder(&self->pageSearch, StrL("Search pages…"));
    }
    El* page = Div(a)->FlexCol()->W(kFill)->Gap(16);

    // Default: the two actions replace the empty state with a project.
    El* def = nullptr;
    if (self->project) {
        def = Div(a)->W(kFill)->MinH(288)->FlexCol()->Gap(16)
                  ->ItemsCenter()->JustifyCenter()
                  ->Child(Div(a)->FlexRow()->Gap(8)->ItemsCenter()
                              ->Child(IconEl(a, IconName::Folder, 16))
                              ->Child(TextEl(a, self->project == 1
                                                   ? StrL("Untitled project")
                                                   : StrL("Starter project"))))
                  ->Child(EmptyAction(cx, StrL("empty-reset-project"),
                                      StrL("Reset example"),
                                      Listen(cx, &Reset), true));
    } else {
        EmptyContent* actions = EmptyContent::New(cx);
        actions->Child(Div(a)->FlexRow()->FlexWrap()->Gap(8)
            ->JustifyCenter()
            ->Child(EmptyAction(cx, StrL("empty-create-project"),
                                StrL("Create project"), Listen(cx, &Create)))
            ->Child(EmptyAction(cx, StrL("empty-import-project"),
                                StrL("Import sample"), Listen(cx, &Import),
                                true)));
        def = Empty::New(cx)
                  ->Header(EmptyHead(cx, IconName::Folder, "No projects yet",
                      "You haven't created any projects yet. Get started by creating your first project."))
                  ->Content(actions)
                  ->Child(component::Link::New(cx, StrL("empty-learn-more"))
                              ->Href(StrL("https://gpui-kit.com/docs/getting-started"))
                              ->Text(StrL("Learn more"))->IntoEl())
                  ->IntoEl()->MinH(288);
    }
    page->Child(EmptySection(
        cx, "Default",
        "A named header, multiple actions, and an extra child below the content. Actions update this example.",
        def));

    page->Child(EmptySection(
        cx, "Outline",
        "Add a border through Styled; the component already supplies the dashed treatment.",
        Empty::New(cx)
            ->Header(EmptyHead(cx, IconName::HardDrive,
                "Cloud storage is empty",
                "Upload files to your cloud storage to access them anywhere."))
            ->Content(EmptyContent::New(cx)->Child(
                EmptyAction(cx, StrL("empty-upload"), StrL("Upload files…"),
                            Listen(cx, &Open, 1), true)))
            ->IntoEl()->Border(1, th.border)));

    page->Child(EmptySection(
        cx, "Background",
        "A semantic background can blend an empty state into its surrounding surface.",
        Empty::New(cx)
            ->Header(EmptyHead(cx, IconName::Bell, "No notifications",
                "You're all caught up. New notifications will appear here."))
            ->Content(EmptyContent::New(cx)->Child(
                EmptyAction(cx, StrL("empty-refresh"), StrL("Refresh"),
                            Listen(cx, &Refresh), true, IconName::RotateCw)))
            ->IntoEl()->MinH(256)->Bg(RgbaOpacity(th.muted, 0.3f))));

    page->Child(EmptySection(
        cx, "Avatar",
        "Default media keeps the Avatar component's own size and appearance.",
        Empty::New(cx)
            ->Header(EmptyHeader::New(cx)
                ->Media(EmptyMedia::New(cx)->Child(
                    component::Avatar::New(cx)->Name(StrL("Alex Morgan"))
                        ->Src(StrL("https://avatars.githubusercontent.com/u/5518?v=4"))
                        ->IntoEl()))
                ->Title(EmptyTitle::New(cx)->Child(TextEl(a, StrL("Alex is offline"))))
                ->Description(EmptyDescription::New(cx)->Child(TextEl(a,
                    StrL("You can leave a message for Alex to read when they're back.")))))
            ->Content(EmptyContent::New(cx)->Child(
                EmptyAction(cx, StrL("empty-message"), StrL("Leave a message…"),
                            Listen(cx, &Open, 2))))
            ->IntoEl()));

    AvatarGroup* group = AvatarGroup::New(cx)
        ->Child(component::Avatar::New(cx)->Name(StrL("Alex Morgan")))
        ->Child(component::Avatar::New(cx)->Name(StrL("Taylor Lee")))
        ->Child(component::Avatar::New(cx)->Name(StrL("Sam Chen")));
    page->Child(EmptySection(
        cx, "Avatar group",
        "Compose AvatarGroup inside the same media slot; no new Empty variant is needed.",
        Empty::New(cx)
            ->Header(EmptyHeader::New(cx)
                ->Media(EmptyMedia::New(cx)->Child(group->IntoEl()))
                ->Title(EmptyTitle::New(cx)->Child(TextEl(a, StrL("No team members"))))
                ->Description(EmptyDescription::New(cx)->Child(TextEl(a,
                    StrL("Invite your team to collaborate on this project.")))))
            ->Content(EmptyContent::New(cx)->Child(
                EmptyAction(cx, StrL("empty-invite"), StrL("Invite members…"),
                            Listen(cx, &Open, 3), false, IconName::Plus)))
            ->IntoEl()));

    const char* projects[] = {"Design system", "Website", "Mobile app"};
    Str query = InputValue(&self->search);
    El* results = Div(a)->FlexCol()->W(kFill)->Gap(8)->MinH(192);
    int found = 0;
    for (const char* name : projects) {
        if (!StrContainsI(Str(name), query)) continue;
        found++;
        results->Child(Div(a)->FlexRow()->Gap(8)->Pad(12)
            ->Child(IconEl(a, IconName::Folder, 16))
            ->Child(TextEl(a, Str(name))));
    }
    if (!found) {
        results->Child(Empty::New(cx)
            ->Header(EmptyHead(cx, IconName::None, "No projects found",
                "Try a different search or clear the query to see all projects."))
            ->Content(EmptyContent::New(cx)->Child(
                EmptyAction(cx, StrL("empty-clear-search"), StrL("Clear search"),
                            Listen(cx, &ClearSearch), true)))
            ->IntoEl());
    }
    El* search = StorySection(
        cx, "Search",
        "The parent owns search state. Clear the query or search for Design, Website, or Mobile to show results.");
    StorySectionBody(search)->FlexCol()->Gap(16);
    StorySectionAdd(search, component::Input::New(cx, StrL("empty-search"), &self->search)
                                ->Cleanable()->Prefix(IconEl(a, IconName::Search, 16))
                                ->OnChange(Listen(cx, &SearchChanged))->IntoEl());
    StorySectionAdd(search, results);
    page->Child(search);

    page->Child(EmptySection(
        cx, "Custom content",
        "An existing Input can live in EmptyContent, with rich supporting content beneath it.",
        Empty::New(cx)
            ->Header(EmptyHead(cx, IconName::None, "Looking for a project?",
                "The page you're looking for doesn't exist. Try searching for another page."))
            ->Content(EmptyContent::New(cx)
                ->Child(component::Input::New(cx, StrL("empty-page-search"), &self->pageSearch)
                            ->Cleanable()->Prefix(IconEl(a, IconName::Search, 16))
                            ->IntoEl())
                ->Child(Div(a)->FlexRow()->Gap(4)->ItemsCenter()
                    ->Child(TextEl(a, StrL("Need help?")))
                    ->Child(component::Link::New(cx, StrL("empty-documentation"))
                        ->Href(StrL("https://gpui-kit.com/docs/getting-started"))
                        ->Text(StrL("Read the documentation"))->IntoEl())))
            ->IntoEl()));

    El* compact = Empty::New(cx)
        ->Header(EmptyHead(cx, IconName::Inbox, "还没有共享文件",
            "将文件添加到此项目后，团队成员就能在这里查看、讨论和继续编辑。较长的说明也应该完整显示。"))
        ->Content(EmptyContent::New(cx)->Child(
            EmptyAction(cx, StrL("empty-add-file"), StrL("添加文件…"),
                        Listen(cx, &Open, 4), true)))
        ->IntoEl()->MaxW(320)->Pad(16)->ItemsStart()->Border(1, th.border);
    if (compact->first) compact->first->ItemsStart();
    page->Child(EmptySection(
        cx, "Constrained layout",
        "Refine each named slot independently. Long text wraps in this compact, leading-aligned panel.",
        compact));
    page->Child(EmptySection(
        cx, "Minimal", "Media and content slots are optional.",
        Empty::New(cx)
            ->Header(EmptyHead(cx, IconName::None, "No recent activity",
                "New activity will appear here."))
            ->IntoEl()));

    if (self->dialog) {
        const char* titles[] = {"", "Upload files", "Message Alex",
                                "Invite members", "添加文件"};
        const char* descriptions[] = {
            "", "Connect your file picker to this action to choose files for upload.",
            "Your message composer can open here while the empty state stays in the background.",
            "Your invitation form can compose the existing Input and Button components here.",
            "选择要与团队共享的文件。"};
        int i = self->dialog;
        page->Child(component::Dialog::New(cx)->Open(true)->Title(Str(titles[i]))
            ->Body(TextEl(a, Str(descriptions[i]))->Wrap())
            ->OnCancel(Listen(cx, &Close))
            ->OnClose(Listen(cx, &Close))
            ->OnOk(Listen(cx, &Close))
            ->IntoEl(WindowSize(cx->win)));
    }
    return page;
}

STORY_PAGE(StoryEmpty, EmptyStory);
