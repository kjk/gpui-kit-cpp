/* examples/text_max_lines — TextView::max_lines over the same Markdown and
   controls as the upstream example. Dragging the slider changes the body-line
   budget; a straddling glyph line is omitted whole, while a tall image keeps
   the part that fits. TextViewState::IsClamped decides when Show more appears.
 */

#include "gpui.h"

#include <math.h>

using namespace gpui;

static const int kDefaultMaxLines = 5;

static const char* kLongMarkdown = R"MD(### Quarterly summary

**Revenue** grew by *18%* quarter over quarter, driven by the desktop client
rollout and the new [market data](https://longbridge.com) subscriptions —
legacy plans are ~~discontinued~~ and folded into `pro`.

> The clip must land on a whole line: however you drag the slider, no line of
> glyphs is ever cut in half.

Inline image mix: PNG avatars <img src="https://avatars.githubusercontent.com/u/5518" alt="Jason Lee avatar" width="32" height="32" /> and <img src="https://avatars.githubusercontent.com/u/28998859" alt="GitHub avatar" width="32" height="32" /> stay inside the same text flow, and an SVG badge ![Rust](https://rust-lang.org/static/images/rust-logo-blk.svg) wraps with the text around it.

- Desktop DAU is up **24%**
  - macOS **+31%**, Windows *+19%*
  - Linux ships via `install.sh` now
- The `max_lines` preview lands in this release
- Churn stayed flat at 2.1%

| Segment | QoQ    | Note                 |
| ------- | ------ | -------------------- |
| Desktop | +24%   | new dock layout      |
| Mobile  | +9%    | steady               |
| Web     | -3%    | migrating to desktop |

![Img](https://miro.medium.com/v2/resize:fit:1400/format:webp/1*WgEz5f3n3lD7MfC7NeQGOA.jpeg)

---

```rust
fn main() {
    println!("hidden until expanded");
}
```

Text lines are kept whole; the photo above is cut on the box edge instead, so
the preview never holds blank space it could have filled.)MD";

static const char* kShortMarkdown =
    "A **short** note that fits inside the cap, so it renders at its natural "
    "height and no button appears.";

struct MaxLinesExample {
    Entity<component::TextViewState> longText = {};
    Entity<component::TextViewState> shortText = {};
    SliderState slider =
        SliderStateNew(1, 60, SliderSingle(kDefaultMaxLines), 1);
    float scrollY = 0;
    int maxLines = kDefaultMaxLines;
    bool expanded = false;

    static void OnSlider(MaxLinesExample* self, Ctx* cx,
                         const SliderEvent* event) {
        if (event->kind != SliderEventKind::Change) return;
        self->maxLines = (int)lroundf(event->value.Start());
        Notify(cx);
    }

    static void OnScroll(MaxLinesExample* self, Ctx* cx,
                         const ScrollEvent* event) {
        self->scrollY = event->offsetY;
        Notify(cx);
    }

    static void OnToggle(MaxLinesExample* self, Ctx* cx, const ClickEvent*) {
        self->expanded = !self->expanded;
        Notify(cx);
    }

    static El* Section(Ctx* cx, Str caption, El* body) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        return Div(a)
            ->FlexCol()
            ->W(kFill)
            ->Gap(8)
            ->Child(TextEl(a, caption)->Font(12)->Fg(th.mutedFg))
            ->Child(Div(a)
                        ->FlexCol()
                        ->W(kFill)
                        ->Pad(16)
                        ->Radius(th.radius)
                        ->Border(1, th.border)
                        ->Child(body));
    }

    static El* Render(MaxLinesExample* self, Ctx* cx) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        component::TextViewState* longState = self->longText.Get(cx);
        bool clamped = longState && longState->IsClamped();

        El* header =
            Div(a)
                ->FlexCol()
                ->W(kFill)
                ->PadX(24)
                ->PadT(24)
                ->PadB(16)
                ->Gap(4)
                ->Child(
                    TextEl(a, StrL("Clamped previews"))->Font(18)->Semibold())
                ->Child(TextEl(a, StrL("Rendered Markdown bounded to a "
                                       "number of whole lines. Drag the "
                                       "slider, or resize the window to "
                                       "reflow the text."))
                            ->Font(14)
                            ->Fg(th.mutedFg)
                            ->Wrap());

        El* controls =
            Div(a)
                ->FlexRow()
                ->W(kFill)
                ->ItemsCenter()
                ->PadX(24)
                ->PadB(16)
                ->Gap(12)
                ->Child(TextEl(a, StrL("Lines"))->Font(14)->Fg(th.mutedFg))
                ->Child(Div(a)->Flex1()->MaxW(320)->Child(
                    component::Slider::New(cx, StrL("line-budget"),
                                           &self->slider)
                        ->OnChange(Listen(cx, &OnSlider))
                        ->IntoEl()))
                ->Child(TextEl(a, StrDup(a, fmt("%d", self->maxLines)))
                            ->Font(14)
                            ->W(24));

        component::TextView* longView =
            component::TextView::New(cx, self->longText)->Selectable();
        if (!self->expanded) longView->MaxLines(self->maxLines);
        El* longBody =
            Div(a)->FlexCol()->W(kFill)->Gap(12)->Child(longView->IntoEl());
        if (clamped || self->expanded) {
            longBody->Child(component::Button::New(cx, StrL("toggle"))
                                ->Ghost()
                                ->WithSize(UiSize::Small)
                                ->Icon(self->expanded ? IconName::ChevronUp
                                                      : IconName::ChevronDown)
                                ->Label(self->expanded ? StrL("Show less")
                                                       : StrL("Show more"))
                                ->OnClick(Listen(cx, &OnToggle))
                                ->IntoEl());
        }

        El* shortBody = component::TextView::New(cx, self->shortText)
                            ->Selectable()
                            ->MaxLines(self->maxLines)
                            ->IntoEl();
        El* content =
            Div(a)
                ->FlexCol()
                ->W(kFill)
                ->Gap(24)
                ->PadX(24)
                ->PadB(24)
                ->Child(Section(cx,
                                self->expanded
                                    ? StrL("Expanded")
                                    : StrL("Clamped to the line budget"),
                                longBody))
                ->Child(
                    Section(cx, StrL("Shorter than the budget"), shortBody));

        return Div(a)
            ->FlexCol()
            ->SizeFull()
            ->Bg(th.background)
            ->Fg(th.foreground)
            ->Child(header)
            ->Child(controls)
            ->Child(Div(a)
                        ->FlexCol()
                        ->Flex1()
                        ->MinH(0)
                        ->W(kFill)
                        ->ScrollY(self->scrollY)
                        ->ScrollId(HashClickId(StrL("max-lines-scroll")))
                        ->OnScroll(Listen(cx, &OnScroll))
                        ->Child(content));
    }
};

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Dark);
    Entity<MaxLinesExample> view = EntityNew<MaxLinesExample>(app);
    MaxLinesExample* self = view.Get(app);
    self->longText =
        component::TextViewState::Markdown(app, Str(kLongMarkdown));
    self->shortText =
        component::TextViewState::Markdown(app, Str(kShortMarkdown));
    Window* win = KitOpenWindow(app, StrL("Text max lines"), 720, 680, view.id,
                                WinOpts{});
    (void)win;
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
