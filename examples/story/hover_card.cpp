#include "Story.h"

struct HoverCardStory {
    float documentationScroll = 0;
    static void OnDocumentationScroll(HoverCardStory* self, Ctx* cx,
                                      const ScrollEvent* ev) {
        self->documentationScroll = ev->offsetY;
        Notify(cx);
    }
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

El* HoverCardStory::Render(HoverCardStory* self, Ctx* cx) {
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
    // h_flex() has no gap. The spaces live in the literals.
    El* richRow = Div(a)->FlexRow()->ItemsCenter();
    richRow->Child(StoryTxt(cx, StrL("Hover over "), 16, th.foreground));
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
        info->Child(Div(a)->PadT(4)->Child(StoryTxt(
            cx, StrL("The author of GPUI Component."), 16, th.foreground)));
        profile->Child(info);
    }
    richRow->Child(component::HoverCard::New(cx)
                       ->Trigger(link)
                       ->Content(profile)
                       ->IntoEl());
    richRow
        ->Child(StoryTxt(cx, StrL(" to see their profile"), 16, th.foreground));
    StorySectionAdd(rich, richRow);
    page->Child(rich);

    El* documentation =
        StorySection(cx, "Long Content",
                     "Bound the body and give the overflow one scroll owner, "
                     "so a long card keeps its footprint.");
    StorySectionBody(documentation)->W(520);
    El* docs = nullptr;
    if (Showing(cx, StrL("long-content"))) {
        El* body = Div(a)->FlexCol()->W(440)->MaxH(180)->Gap(8);
        body->Child(
            StoryTxt(cx,
                     StrL("pub fn open_delay(self, delay: Duration) -> Self"),
                     14, th.foreground)
                ->Mono());
        body->Child(
            StoryTxt(cx,
                     StrL("Sets how long the pointer must rest on the trigger "
                          "before the card opens, and how long the card stays "
                          "open once the pointer leaves."),
                     14, th.mutedFg)
                ->Wrap());
        body->Child(
            Div(a)
                ->Pad(8)
                ->Radius(th.radius)
                ->Bg(th.tokens.muted)
                ->Child(
                    StoryTxt(cx,
                             StrL("let card = HoverCard::new(\"account\")\n    "
                                  ".open_delay(Duration::from_millis(300))\n   "
                                  " .close_delay(Duration::from_millis(150));"),
                             12, th.foreground)
                        ->Mono()));
        body->Child(
            StoryTxt(cx,
                     StrL("The open delay is cancelled when the pointer leaves "
                          "the trigger early, so a pointer passing over the "
                          "trigger never flashes the card. Both delays default "
                          "to the presentation in the theme."),
                     14, th.mutedFg)
                ->Wrap());
        body->Child(
            StoryTxt(
                cx,
                StrL("A long card keeps the same footprint as a short one: "
                     "bound the body and let it scroll. Extending the card "
                     "instead pushes it past the window edge, where the shell "
                     "clips it and the reader cannot reach the rest."),
                14, th.mutedFg)
                ->Wrap());
        body->Child(
            Div(a)
                ->FlexRow()
                ->Gap(4)
                ->Child(StoryTxt(cx, StrL("See"), 14, th.mutedFg))
                ->Child(component::Link::New(cx, StrL("long-content-reference"))
                            ->Text(StrL("API reference"))
                            ->IntoEl()));
        docs = component::ScrollableElement::VerticalScrollbar(
            cx, body, StrL("long-content-scroll"), self->documentationScroll,
            Listen(cx, &OnDocumentationScroll));
    }
    El* docRow = Div(a)->FlexCol()->Gap(8)->ItemsCenter();
    docRow->Child(
        component::HoverCard::New(cx, StrL("long-content"))
            ->Trigger(component::Link::New(cx, StrL("long-content-link"))
                          ->Text(StrL("Hover for documentation"))
                          ->IntoEl())
            ->Content(docs)
            ->IntoEl());
    docRow->Child(StoryTxt(cx, StrL("Scroll inside the card for the rest"), 12,
                           th.mutedFg));
    StorySectionAdd(documentation, docRow);
    page->Child(documentation);

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
        const char* content;
        component::HoverCardAnchor anchor;
    };
    static const AnchorBtn kAnchors[2][3] = {
        {{"tl", "Top Left", "Positioned at Top Left",
          component::HoverCardAnchor::TopLeft},
         {"tc", "Top Center", "Positioned at Top Center",
          component::HoverCardAnchor::TopCenter},
         {"tr", "Top Right", "Positioned at Top Right",
          component::HoverCardAnchor::TopRight}},
        {{"bl", "Bottom Left", "Positioned at Bottom Left",
          component::HoverCardAnchor::BottomLeft},
         {"bc", "Bottom Center", "Positioned at Bottom Center",
          component::HoverCardAnchor::BottomCenter},
         {"br", "Bottom Right", "Positioned at Bottom Right",
          component::HoverCardAnchor::BottomRight}},
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
                    ->Content(on ? PlainCard(cx, Str(kAnchors[r][i].content))
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
