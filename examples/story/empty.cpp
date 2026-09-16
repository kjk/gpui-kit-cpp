#include "Story.h"

using namespace gpui;

struct EmptyStory {
    static El* Render(EmptyStory*, Ctx* cx) {
        using namespace component;
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        El* page = Div(a)->FlexCol()->W(kFill)->Gap(20);

        EmptyHeader* defaultHeader =
            EmptyHeader::New(cx)
                ->Media(EmptyMedia::New(cx)
                            ->WithVariant(EmptyMediaVariant::Icon)
                            ->Child(IconEl(a, IconName::Inbox, 16)))
                ->Title(EmptyTitle::New(cx)
                            ->Child(TextEl(a, StrL("No projects yet"))))
                ->Description(EmptyDescription::New(cx)->Child(
                    TextEl(a, StrL("Create a project to start organizing "
                                   "your work."))
                        ->Wrap()));
        EmptyContent* defaultContent =
            EmptyContent::New(cx)
                ->Child(component::Button::New(cx, StrL("empty-create"))
                            ->Primary()
                            ->Icon(IconName::Plus)
                            ->Label(StrL("New project"))
                            ->IntoEl());
        El* section = StorySection(
            cx, "Default", "Compose media, title, description, and actions.");
        StorySectionAdd(section, Empty::New(cx)
                                     ->Header(defaultHeader)
                                     ->Content(defaultContent)
                                     ->IntoEl()
                                     ->MinH(260));
        page->Child(section);

        EmptyHeader* outlineHeader =
            EmptyHeader::New(cx)
                ->Media(EmptyMedia::New(cx)
                            ->WithVariant(EmptyMediaVariant::Icon)
                            ->Child(IconEl(a, IconName::HardDrive, 16)))
                ->Title(EmptyTitle::New(cx)
                            ->Child(TextEl(a, StrL("Nothing downloaded"))))
                ->Description(EmptyDescription::New(cx)->Child(
                    TextEl(a, StrL("Downloaded files will appear here."))));
        section = StorySection(
            cx, "Outline and background",
            "The root is borderless until the caller supplies a surface.");
        StorySectionAdd(section, Empty::New(cx)
                                     ->Header(outlineHeader)
                                     ->IntoEl()
                                     ->MinH(220)
                                     ->Border(1, th.border)
                                     ->Bg(th.secondary));
        page->Child(section);

        AvatarGroup* group =
            AvatarGroup::New(cx)
                ->WithSize(UiSize::Small)
                ->Child(component::Avatar::New(cx)->Name(StrL("Alex Kim")))
                ->Child(component::Avatar::New(cx)->Name(StrL("Taylor Reed")))
                ->Child(component::Avatar::New(cx)->Name(StrL("Sam Lee")));
        EmptyHeader* peopleHeader =
            EmptyHeader::New(cx)
                ->Media(EmptyMedia::New(cx)->Child(group->IntoEl()))
                ->Title(EmptyTitle::New(cx)
                            ->Child(TextEl(a, StrL("No collaborators yet"))))
                ->Description(EmptyDescription::New(cx)->Child(
                    TextEl(a, StrL("Invite people when this project is ready "
                                   "to share."))
                        ->Wrap()));
        section = StorySection(
            cx, "Avatar group",
            "Unframed media keeps the intrinsic width of nested rows.");
        StorySectionAdd(
            section, Empty::New(cx)->Header(peopleHeader)->IntoEl()->MinH(220));
        page->Child(section);

        EmptyHeader* constrainedHeader =
            EmptyHeader::New(cx)
                ->Media(EmptyMedia::New(cx)
                            ->WithVariant(EmptyMediaVariant::Icon)
                            ->Child(IconEl(a, IconName::File, 16)))
                ->Title(EmptyTitle::New(cx)
                            ->Child(TextEl(a, StrL("还没有共享文件"))))
                ->Description(EmptyDescription::New(cx)->Child(
                    TextEl(a, StrL("将文件添加到此项目后，团队成员就能在这里查"
                                   "看、讨论和继续编辑。"))
                        ->Wrap()));
        section = StorySection(
            cx, "Constrained layout",
            "Named parts can be refined independently and long text wraps.");
        El* constrained = Empty::New(cx)
                              ->Header(constrainedHeader)
                              ->IntoEl()
                              ->MaxW(320)
                              ->MinH(220)
                              ->ItemsStart()
                              ->Border(1, th.border);
        if (constrained->first) constrained->first->ItemsStart();
        StorySectionAdd(section, constrained);
        page->Child(section);

        section = StorySection(cx, "Minimal",
                               "Media and content slots are optional.");
        StorySectionAdd(
            section,
            Empty::New(cx)
                ->Header(EmptyHeader::New(cx)
                             ->Title(EmptyTitle::New(cx)->Child(
                                 TextEl(a, StrL("No recent activity"))))
                             ->Description(EmptyDescription::New(cx)->Child(
                                 TextEl(a, StrL("New activity will appear "
                                                "here.")))))
                ->IntoEl()
                ->MinH(180));
        return page->Child(section);
    }
};

STORY_PAGE(StoryEmpty, EmptyStory);
