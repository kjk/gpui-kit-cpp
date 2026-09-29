#include "Story.h"

// crates/story/src/stories/marker_story.rs

// section(..).max_w(rems(42.5)) — 680 at the 16px root.
static const float kMarkerSectionMaxW = 680;

struct MarkerStory {
    static El* Render(MarkerStory* self, Ctx* cx);
};

static El* MarkerSection(Ctx* cx, const char* title, const char* desc,
                         float gap) {
    El* section = StorySection(cx, title, desc);
    StorySectionBody(section)->FlexCol()->Gap(gap)->MaxW(kMarkerSectionMaxW);
    return section;
}

static component::MarkerIcon* MarkerIconOf(Ctx* cx, IconName name) {
    return component::MarkerIcon::New(cx)
        ->Child(component::Icon::New(cx, name)->IntoEl());
}

static component::MarkerContent* MarkerTextOf(Ctx* cx, const char* text) {
    return component::MarkerContent::New(cx)->Child(TextEl(cx->a, Str(text)));
}

// The icon slot, if any, comes before the content slot: a Marker renders its
// children in the order they were added.
static component::Marker* MarkerShimmer(Ctx* cx, const char* text,
                                        component::MarkerIcon* icon = nullptr) {
    component::Marker* marker =
        component::Marker::New(cx)
            ->Loading(true)
            ->WithLoadingStyle(component::MarkerLoadingStyle::Shimmer);
    if (icon) {
        marker->Icon(icon);
    }
    return marker->Content(component::MarkerContent::New(cx)->Text(Str(text)));
}

// A Marker refinement over a single field, which is what every
// `.text_color(..)` / `.border_color(..)` in the Rust story comes to.
static component::Marker* MarkerFg(component::Marker* marker, Rgba color) {
    Style s = {};
    s.color = color;
    return marker->Refine(s, StyleFieldColor);
}

El* MarkerStory::Render(MarkerStory*, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->W(kFill)->Gap(16);

    El* variants = MarkerSection(
        cx, "Variants",
        "Choose a plain row, a centered separator, or a bordered boundary.",
        16);
    StorySectionAdd(variants,
                    component::Marker::New(cx)
                        ->Content(MarkerTextOf(cx, "Plain status update"))
                        ->IntoEl());
    StorySectionAdd(variants,
                    component::Marker::New(cx)
                        ->WithVariant(component::MarkerVariant::Separator)
                        ->Content(MarkerTextOf(cx, "Earlier messages"))
                        ->IntoEl());
    StorySectionAdd(variants,
                    component::Marker::New(cx)
                        ->WithVariant(component::MarkerVariant::Border)
                        ->Content(MarkerTextOf(cx, "Unread messages"))
                        ->IntoEl());
    page->Child(variants);

    El* alignment = MarkerSection(cx, "Alignment",
                                  "Center a system notice or trail a delivery "
                                  "state; Separator centers by default.",
                                  12);
    StorySectionAdd(alignment,
                    component::Marker::New(cx)
                        ->Content(MarkerTextOf(cx, "Leading by default"))
                        ->IntoEl());
    StorySectionAdd(
        alignment,
        component::Marker::New(cx)
            ->Alignment(component::MarkerAlignment::Center)
            ->Icon(MarkerIconOf(cx, IconName::Info))
            ->Content(MarkerTextOf(cx, "Messages are end-to-end encrypted"))
            ->IntoEl());
    StorySectionAdd(
        alignment,
        component::Marker::New(cx)
            ->Alignment(component::MarkerAlignment::Center)
            ->Content(component::MarkerContent::New(cx)->Child(
                TextEl(a, StrL("The answer was stopped before it finished. "
                               "Edit the question or ask again, and a long "
                               "notice wraps around its center."))
                    ->Wrap()))
            ->IntoEl());
    StorySectionAdd(
        alignment,
        component::Marker::New(cx)
            ->Alignment(component::MarkerAlignment::Center)
            ->Content(MarkerTextOf(cx, "The message could not be sent."))
            ->Child(component::Button::New(cx, StrL("marker-retry-send"))
                        ->Text()
                        ->WithSize(UiSize::Small)
                        ->Label(StrL("Retry"))
                        ->IntoEl())
            ->IntoEl());
    StorySectionAdd(alignment, component::Marker::New(cx)
                                   ->Alignment(component::MarkerAlignment::End)
                                   ->Content(MarkerTextOf(cx, "Delivered"))
                                   ->IntoEl());
    StorySectionAdd(alignment,
                    component::Marker::New(cx)
                        ->WithVariant(component::MarkerVariant::Separator)
                        ->Alignment(component::MarkerAlignment::End)
                        ->Content(MarkerTextOf(cx, "Today"))
                        ->IntoEl());
    page->Child(alignment);

    El* status = MarkerSection(
        cx, "Status",
        "Compose icons, spinners, and labels without a fixed status model.",
        12);
    StorySectionAdd(status, MarkerFg(component::Marker::New(cx), th.success)
                                ->Icon(MarkerIconOf(cx, IconName::CircleCheck))
                                ->Content(MarkerTextOf(cx, "Online"))
                                ->IntoEl());
    StorySectionAdd(status, component::Marker::New(cx)
                                ->Icon(component::MarkerIcon::New(cx)->Child(
                                    component::Spinner::New(cx)
                                        ->WithSize(UiSize::XSmall)
                                        ->IntoEl()))
                                ->Content(MarkerTextOf(cx, "Alice is typing…"))
                                ->IntoEl());
    StorySectionAdd(status,
                    component::Marker::New(cx)
                        ->Icon(MarkerIconOf(cx, IconName::Bell))
                        ->Content(MarkerTextOf(cx, "Unread notifications"))
                        ->IntoEl());
    StorySectionAdd(status, MarkerFg(component::Marker::New(cx), th.danger)
                                ->Icon(MarkerIconOf(cx, IconName::Info))
                                ->Content(MarkerTextOf(
                                    cx, "Message could not be delivered"))
                                ->IntoEl());
    page->Child(status);

    El* withIcon = MarkerSection(
        cx, "With icon",
        "Icons can communicate sender activity, notices, and saved items.", 12);
    StorySectionAdd(withIcon, component::Marker::New(cx)
                                  ->Icon(MarkerIconOf(cx, IconName::Info))
                                  ->Content(MarkerTextOf(cx,
                                                         "Conversation details "
                                                         "updated"))
                                  ->IntoEl());
    StorySectionAdd(withIcon,
                    component::Marker::New(cx)
                        ->Icon(MarkerIconOf(cx, IconName::Star))
                        ->Content(MarkerTextOf(cx, "Pinned for your team"))
                        ->IntoEl());
    StorySectionAdd(withIcon,
                    component::Marker::New(cx)
                        ->Content(MarkerTextOf(cx, "No icon is required"))
                        ->IntoEl());
    page->Child(withIcon);

    El* loading = MarkerSection(
        cx, "Loading styles",
        "Choose a spinner or a sweeping, ChatGPT-style text shimmer.", 16);
    StorySectionAdd(
        loading, component::Marker::New(cx)
                     ->Loading(true)
                     ->WithLoadingStyle(component::MarkerLoadingStyle::Spinner)
                     ->Content(component::MarkerContent::New(cx)->Text(
                         StrL("shadcn/ui · Loading messages…")))
                     ->IntoEl());
    StorySectionAdd(loading, MarkerShimmer(cx, "ChatGPT · Thinking")->IntoEl());
    StorySectionAdd(loading, MarkerShimmer(cx, "正在探索 4 个文件…",
                                           MarkerIconOf(cx, IconName::Info))
                                 ->IntoEl());
    StorySectionAdd(loading,
                    MarkerShimmer(cx, "Custom color, width, and direction")
                        ->WithShimmerStyle(component::ShimmerStyle::New()
                                               .Duration(3000)
                                               .HighlightColor(th.primary)
                                               .Spread(0.45f)
                                               .Reverse(true))
                        ->IntoEl());
    StorySectionAdd(loading, component::ShimmerText::New(
                                 cx, StrL("Reusable shimmer without a Marker"))
                                 ->Fg(th.mutedFg)
                                 ->IntoEl());
    page->Child(loading);

    El* settings = MarkerSection(
        cx, "Shimmer settings",
        "Customize timing, highlight width, direction, and playback "
        "independently.",
        12);
    StorySectionAdd(
        settings,
        MarkerShimmer(cx, "Faster highlight sweep")
            ->WithShimmerStyle(component::ShimmerStyle::New().Duration(900))
            ->IntoEl());
    StorySectionAdd(
        settings,
        MarkerShimmer(cx, "Wider highlight band")
            ->WithShimmerStyle(component::ShimmerStyle::New().Spread(0.55f))
            ->IntoEl());
    StorySectionAdd(
        settings,
        MarkerShimmer(cx, "Right-to-left sweep")
            ->WithShimmerStyle(component::ShimmerStyle::New().Reverse(true))
            ->IntoEl());
    StorySectionAdd(settings,
                    MarkerShimmer(cx, "Semantic primary highlight")
                        ->WithShimmerStyle(component::ShimmerStyle::New()
                                               .HighlightColor(th.primary))
                        ->IntoEl());
    StorySectionAdd(settings, MarkerShimmer(cx, "Play the highlight once")
                                  ->WithShimmerStyle(
                                      component::ShimmerStyle::New().Once(true))
                                  ->IntoEl());
    StorySectionAdd(
        settings,
        component::Marker::New(cx)
            ->Loading(false)
            ->WithLoadingStyle(component::MarkerLoadingStyle::Shimmer)
            ->Content(component::MarkerContent::New(cx)->Text(StrL("Loading is "
                                                                   "disabled")))
            ->IntoEl());
    page->Child(settings);

    El* separator = MarkerSection(
        cx, "Separator",
        "Place a conversation boundary between two semantic lines.", 16);
    StorySectionAdd(separator,
                    component::Marker::New(cx)
                        ->WithVariant(component::MarkerVariant::Separator)
                        ->Content(MarkerTextOf(cx, "Today"))
                        ->IntoEl());
    Style rule = {};
    rule.bg = Background(RgbaOpacity(th.primary, 0.35f));
    StorySectionAdd(separator,
                    component::Marker::New(cx)
                        ->WithVariant(component::MarkerVariant::Separator)
                        ->SeparatorStyle(rule, StyleFieldBg)
                        ->Icon(MarkerIconOf(cx, IconName::Star))
                        ->Content(MarkerTextOf(cx, "Pinned messages"))
                        ->IntoEl());
    page->Child(separator);

    El* border = MarkerSection(
        cx, "Border", "Use a bottom edge for an unread or section boundary.",
        12);
    StorySectionAdd(border, component::Marker::New(cx)
                                ->WithVariant(component::MarkerVariant::Border)
                                ->Icon(MarkerIconOf(cx, IconName::Info))
                                ->Content(MarkerTextOf(cx, "3 unread messages"))
                                ->IntoEl());
    Style edge = {};
    edge.borderColor = RgbaOpacity(th.primary, 0.4f);
    StorySectionAdd(border, component::Marker::New(cx)
                                ->WithVariant(component::MarkerVariant::Border)
                                ->Refine(edge, StyleFieldBorderColor)
                                ->Content(MarkerTextOf(
                                    cx, "New replies since your last visit"))
                                ->IntoEl());
    page->Child(border);

    El* links = MarkerSection(
        cx, "Links and buttons",
        "Keep external destinations and in-app commands semantically "
        "distinct.",
        12);
    StorySectionAdd(
        links,
        component::Marker::New(cx)
            ->Icon(MarkerIconOf(cx, IconName::Info))
            ->Content(component::MarkerContent::New(cx)->Child(
                component::Link::New(cx, StrL("marker-documentation-link"))
                    ->Href(StrL("https://gpui-kit.com/"))
                    ->Text(StrL("Open the component documentation"))
                    ->IntoEl()))
            ->IntoEl());
    StorySectionAdd(
        links, component::Marker::New(cx)
                   ->Icon(MarkerIconOf(cx, IconName::Star))
                   ->Content(MarkerTextOf(cx, "A saved draft is ready"))
                   ->Child(component::Button::New(cx, StrL("marker-open-draft"))
                               ->Ghost()
                               ->WithSize(UiSize::Small)
                               ->Label(StrL("Open draft"))
                               ->IntoEl())
                   ->IntoEl());
    page->Child(links);

    El* custom = MarkerSection(
        cx, "Custom style",
        "Caller refinements can replace spacing, color, and surface.", 0);
    StorySectionAdd(custom, component::Marker::New(cx)
                                ->Icon(MarkerIconOf(cx, IconName::Star))
                                ->Content(MarkerTextOf(cx, "Pinned message"))
                                ->IntoEl()
                                ->PadX(12)
                                ->PadY(8)
                                ->Radius(th.radius)
                                ->Bg(th.tokens.accent)
                                ->Fg(th.accentFg));
    page->Child(custom);
    return page;
}

STORY_PAGE(StoryMarker, MarkerStory);
