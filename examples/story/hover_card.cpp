#include "Story.h"

struct HoverCardStory {
    static El* Render(HoverCardStory* self, Ctx* cx);
};

// The card runs on gpui_base::HoverCardState now, so the page asks whether
// this one is showing rather than reading the hovered element itself. The
// difference is the delays: the card waits before it opens, and stays up long
// enough for the pointer to travel onto it.
static bool Showing(Ctx* cx, Str id) {
    return component::HoverCardOpen(cx, id);
}

// The trigger only needs a name; the card gives it hover of its own.
static El* Trig(El* e, Str id) {
    return e->Id(id);
}

// The Default card: a heading over a muted line, 450 wide.
static El* Card(Ctx* cx, const char* title, const char* body) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* card = Div(a)
                   ->W(450)
                   ->Pad(12)
                   ->FlexCol()
                   ->Gap(4)
                   ->Border(1, th.border)
                   ->Bg(th.tokens.background)
                   ->Radius(th.radius);
    card->Child(StoryTxt(cx, Str(title), 14, th.foreground)->Semibold());
    card->Child(StoryTxt(cx, Str(body), 14, th.mutedFg)->Wrap());
    return card;
}

// The Position cards carry no heading in Rust, just the one line.
static El* PlainCard(Ctx* cx, Str body) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->Pad(12)
        ->Border(1, th.border)
        ->Bg(th.tokens.background)
        ->Radius(th.radius)
        ->Child(StoryTxt(cx, body, 14, th.foreground));
}

El* HoverCardStory::Render(HoverCardStory*, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);

    El* def = StorySection(
        cx, "Default",
        "Shows supporting information without changing the current view.");
    StorySectionBody(def)->W(520);
    bool defOpen = Showing(cx, StrL("hc-default-card"));
    El* defTrig = Trig(StoryTxt(cx, StrL("Hover over me"), 14, th.primary),
                       StrL("hc-default"));
    StorySectionAdd(def,
                    component::HoverCard::New(cx, StrL("hc-default-card"))
                        ->Trigger(defTrig)
                        ->Content(defOpen ? Card(cx, "This is a hover card",
                                                 "You can display rich content "
                                                 "when hovering over a "
                                                 "trigger element.")
                                          : nullptr)
                        ->IntoEl());
    page->Child(def);

    El* rich = StorySection(
        cx, "Rich Content",
        "Cards can contain avatars, typography, and structured details.");
    StorySectionBody(rich)->W(520);
    El* richRow = Div(a)->FlexRow()->ItemsCenter()->Gap(4);
    richRow->Child(StoryTxt(cx, StrL("Hover over"), 16, th.foreground));
    bool richOpen = Showing(cx, StrL("hc-rich-card"));
    El* link = Trig(component::Link::New(cx, StrL("user-profile-link"))
                        ->Text(StrL("@huacnlee"))
                        ->IntoEl(),
                    StrL("hc-rich"));
    El* profile = nullptr;
    if (richOpen) {
        profile = Div(a)
                      ->FlexRow()
                      ->Gap(12)
                      ->W(320)
                      ->Pad(12)
                      ->Border(1, th.border)
                      ->Bg(th.tokens.background)
                      ->Radius(th.radius);
        profile->ItemsStart();
        profile->Child(component::Avatar::New(cx)
                           ->Name(StrL("Jason Lee"))
                           ->WithSize(UiSize::Medium)
                           ->IntoEl());
        El* info = Div(a)->FlexCol()->Gap(4)->LineHeight(1.f);
        info->Child(StoryTxt(cx, StrL("Jason Lee"), 16, th.foreground)
                        ->Semibold());
        info->Child(StoryTxt(cx, StrL("@huacnlee"), 14, th.primary));
        info->Child(Div(a)->PadT(4)->Child(
            StoryTxt(cx, StrL("The author of GPUI Kit."), 16, th.foreground)));
        profile->Child(info);
    }
    richRow->Child(component::HoverCard::New(cx)
                       ->Trigger(link)
                       ->Content(profile)
                       ->IntoEl());
    richRow
        ->Child(StoryTxt(cx, StrL("to see their profile"), 16, th.foreground));
    StorySectionAdd(rich, richRow);
    page->Child(rich);

    El* timing = StorySection(
        cx, "Timing",
        "Open and close delays can match the interaction context.");
    StorySectionBody(timing)->W(520);
    El* timingRow = Div(a)->FlexRow()->Gap(16)->ItemsCenter();
    timingRow
        ->Child(component::HoverCard::New(cx, StrL("fast-card"))
                    ->OpenDelay(200)
                    ->CloseDelay(100)
                    ->Trigger(component::Button::New(cx, StrL("fast"))
                                  ->Label(StrL("Fast Open (200ms)"))
                                  ->Outline()
                                  ->IntoEl())
                    ->Content(Showing(cx, StrL("fast-card"))
                                  ? PlainCard(cx, StrL("This hover card "
                                                       "opens after 200ms"))
                                  : nullptr)
                    ->IntoEl());
    timingRow
        ->Child(component::HoverCard::New(cx, StrL("slow-card"))
                    ->OpenDelay(1000)
                    ->CloseDelay(500)
                    ->Trigger(component::Button::New(cx, StrL("slow"))
                                  ->Label(StrL("Slow Open (1000ms)"))
                                  ->Outline()
                                  ->IntoEl())
                    ->Content(Showing(cx, StrL("slow-card"))
                                  ? PlainCard(cx, StrL("This hover card "
                                                       "opens after 1000ms"))
                                  : nullptr)
                    ->IntoEl());
    StorySectionAdd(timing, timingRow);
    page->Child(timing);

    El* pos = StorySection(cx, "Position",
                           "Content can anchor to each side of its trigger.");
    StorySectionBody(pos)->W(640);
    El* posCol = Div(a)->FlexCol()->Gap(16)->ItemsCenter()->JustifyCenter();
    struct AnchorBtn {
        const char* id;
        const char* label;
        component::HoverCardAnchor anchor;
    };
    static const AnchorBtn kAnchors[2][3] = {
        {{"tl", "Top Left", component::HoverCardAnchor::TopLeft},
         {"tc", "Top Center", component::HoverCardAnchor::TopCenter},
         {"tr", "Top Right", component::HoverCardAnchor::TopRight}},
        {{"bl", "Bottom Left", component::HoverCardAnchor::BottomLeft},
         {"bc", "Bottom Center", component::HoverCardAnchor::BottomCenter},
         {"br", "Bottom Right", component::HoverCardAnchor::BottomRight}},
    };
    for (int r = 0; r < 2; r++) {
        El* row = Div(a)->FlexRow()->Gap(16)->ItemsCenter();
        for (int i = 0; i < 3; i++) {
            Str id = Str(kAnchors[r][i].id);
            bool on = Showing(cx, id);
            row->Child(
                component::HoverCard::New(cx, id)
                    ->Anchor(kAnchors[r][i].anchor)
                    ->Trigger(component::Button::New(cx, id)
                                  ->Label(Str(kAnchors[r][i].label))
                                  ->Outline()
                                  ->IntoEl())
                    ->Content(
                        on ? PlainCard(cx, StoryFmt(cx, "Positioned at %s",
                                                    kAnchors[r][i].label))
                           : nullptr)
                    ->IntoEl());
        }
        posCol->Child(row);
    }
    StorySectionAdd(pos, posCol);
    page->Child(pos);
    return page;
}

STORY_PAGE(StoryHoverCard, HoverCardStory);
