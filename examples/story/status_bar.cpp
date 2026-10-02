#include "Story.h"

struct StatusBarStory {
    static El* Render(StatusBarStory* self, Ctx* cx);
};

// Every button on the two real bars answers with a notification, which is
// what `on_click(|_, window, cx| window.push_notification(..))` does.
static void OnBarClick(StatusBarStory*, Ctx* cx, const ClickEvent*,
                       int64_t which);

enum {
    BarBranch = 0,
    BarPosition,
    BarEncoding,
    BarLanguage,
    BarNotifications
};

static void OnBarClick(StatusBarStory*, Ctx* cx, const ClickEvent*,
                       int64_t which) {
    static const char* kMsg[] = {"Switch branch", "Go to Line/Column",
                                 "Select encoding", "Select language",
                                 "3 notifications"};
    StoryPushNotification(cx, Str(kMsg[which]));
}

// h_flex().items_center().gap_1(): an icon and the count beside it.
static El* CountEl(Ctx* cx, IconName n, Rgba fg, Str text) {
    Arena* a = cx->a;
    return Div(a)
        ->FlexRow()
        ->ItemsCenter()
        ->Gap(4)
        ->Child(IconEl(a, n, 12)->Fg(fg))
        ->Child(StoryTxt(cx, text, 12, ThemeNow(cx->app).mutedFg));
}

El* StatusBarStory::Render(StatusBarStory*, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->ItemsCenter()->Gap(24)->W(kFill);

    // Every section is .max_w(px(760.)): it fills a narrower pane and stays
    // 760 wide in a wider one.
    El* editor = StorySection(
        cx, "Editor",
        "Places repository state on the left and document state on the right.");
    StorySectionBody(editor)->MaxW(760);
    StorySectionAdd(
        editor,
        component::StatusBar::New(cx)
            ->Left(component::Button::New(cx, StrL("branch"))
                       ->Ghost()
                       ->WithSize(UiSize::XSmall)
                       ->Icon(IconName::Github)
                       ->Label(StrL("main"))
                       ->Tooltip(StrL("Git branch"))
                       ->OnClick(Listen(cx, &OnBarClick, BarBranch))
                       ->IntoEl())
            ->Left(component::Separator::Vertical(cx)->IntoEl()->H(12))
            ->Left(Div(a)
                       ->FlexRow()
                       ->ItemsCenter()
                       ->Gap(8)
                       ->Child(CountEl(cx, IconName::CircleCheck, th.green,
                                       StrL("0")))
                       ->Child(CountEl(cx, IconName::Info, th.blue, StrL("2"))))
            ->Right(component::Button::New(cx, StrL("position"))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Label(StrL("Ln 12, Col 34"))
                        ->Tooltip(StrL("Go to Line/Column"))
                        ->OnClick(Listen(cx, &OnBarClick, BarPosition))
                        ->IntoEl())
            ->Right(component::Separator::Vertical(cx)->IntoEl()->H(12))
            ->Right(component::Button::New(cx, StrL("encoding"))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Label(StrL("UTF-8"))
                        ->OnClick(Listen(cx, &OnBarClick, BarEncoding))
                        ->IntoEl())
            ->Right(component::Button::New(cx, StrL("language"))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Label(StrL("Rust"))
                        ->OnClick(Listen(cx, &OnBarClick, BarLanguage))
                        ->IntoEl())
            ->IntoEl());
    page->Child(editor);

    El* appSec = StorySection(
        cx, "Application",
        "Combines connectivity, progress, save state, and notifications.");
    StorySectionBody(appSec)->MaxW(760);
    StorySectionAdd(
        appSec,
        component::StatusBar::New(cx)
            ->Left(Div(a)
                       ->FlexRow()
                       ->ItemsCenter()
                       ->Gap(4)
                       ->Child(IconEl(a, IconName::Check, 12)->Fg(th.mutedFg))
                       ->Child(StoryTxt(cx, StrL("Connected"), 12, th.mutedFg)))
            ->Center(
                Div(a)
                    ->FlexRow()
                    ->ItemsCenter()
                    ->Gap(8)
                    ->Child(component::ProgressCircle::New(cx)
                                ->Id(StrL("syncing"))
                                ->Value(45)
                                ->Size(16)
                                ->Label(false)
                                ->IntoEl())
                    ->Child(StoryTxt(cx, StrL("Syncing…"), 12, th.mutedFg)))
            ->Right(StrL("All changes saved"))
            ->Right(component::Button::New(cx, StrL("notifications"))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Icon(IconName::Bell)
                        ->Label(StrL("3"))
                        ->Tooltip(StrL("3 notifications"))
                        ->OnClick(Listen(cx, &OnBarClick, BarNotifications))
                        ->IntoEl())
            ->IntoEl());
    page->Child(appSec);

    // Layout cases for verifying the dynamic centering behavior.
    El* align = StorySection(
        cx, "Alignment",
        "Center content adapts when either side is empty or populated.");
    StorySectionBody(align)->MaxW(760);
    El* alignCol = Div(a)->FlexCol()->Gap(24)->W(kFill);
    alignCol->Child(component::StatusBar::New(cx)
                        ->Center(StrL("Center only → start-aligned"))
                        ->IntoEl());
    alignCol->Child(component::StatusBar::New(cx)
                        ->Left(StrL("Left"))
                        ->Center(StrL("Center → end (only left)"))
                        ->IntoEl());
    alignCol->Child(component::StatusBar::New(cx)
                        ->Center(StrL("Center → start (only right)"))
                        ->Right(StrL("Right"))
                        ->IntoEl());
    alignCol->Child(component::StatusBar::New(cx)
                        ->Left(StrL("Left"))
                        ->Center(StrL("Center → centered (left + right)"))
                        ->Right(StrL("Right"))
                        ->IntoEl());
    alignCol->Child(component::StatusBar::New(cx)
                        ->Left(StrL("Left"))
                        ->Right(StrL("Right"))
                        ->IntoEl());
    StorySectionAdd(align, alignCol);
    page->Child(align);
    return page;
}

STORY_PAGE(StoryStatusBar, StatusBarStory);
