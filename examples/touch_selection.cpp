/* examples/touch_selection — the handles and the edit menu a long press
   leaves in an Input, a Textarea and a TextView.

   A desktop has no long press, so each "Long press" button injects the
   gesture the host would recognize from a finger held on the control.
   The click is still being dispatched, so the gesture is posted and sent
   once that click is over, the way Rust's window.defer does. */

#include "gpui.h"

using namespace gpui;

static const char kParagraph[] =
    "Online content authoring isn't a solved problem. You might go with "
    "an HTML-based editor, and hope it gives you the kind of HTML you "
    "want. Or you might use a text-based markup format, and hope your "
    "users understand how to use it.";

static const char kProse[] =
    "ProseMirror tries to bridge the gap between **rich text** and "
    "structured content, by providing a drop-in editor component "
    "with a rigid semantic document model that can be customized "
    "to fit your application.";

// window.line_height(): the window text style is 1rem at phi.
static float WindowLineHeight(const App* app) {
    float font = RuntimeStyleNow(app).fontSize;
    if (font <= 0) {
        font = 16.f;
    }
    return font * kLineHeight;
}

struct TouchApp {
    InputState input;
    InputState textarea;
    Bounds inputBounds = {};
    Bounds textareaBounds = {};
    Bounds textBounds = {};
    float scroll = 0;
    int pending = 0;
    Str before = {};
    Str after = {};

    ~TouchApp() {
        StrFree(before);
        StrFree(after);
    }

    static void OnPageScroll(TouchApp* self, Ctx* cx, const ScrollEvent* ev) {
        self->scroll = ev->offsetY;
        Notify(cx);
    }

    static void OnFire(TouchApp* self, Ctx* cx, const ClickEvent*) {
        Bounds box = {};
        if (self->pending == 0) {
            box = self->inputBounds;
        } else if (self->pending == 1) {
            box = self->textareaBounds;
        } else {
            box = self->textBounds;
        }
        float firstLine = WindowLineHeight(cx->app) * 0.5f + 12.f;
        Point at = {box.x + 48.f, box.y + firstLine};
        PlatformInput start = InputLongPress(TouchPhase::Started, at, at);
        WindowDispatchInput(cx->win, &start);
        PlatformInput end = InputLongPress(TouchPhase::Ended, at, at);
        WindowDispatchInput(cx->win, &end);
    }

    static void OnPress(TouchApp* self, Ctx* cx, const ClickEvent*,
                        int64_t which) {
        self->pending = (int)which;
        WindowPost(cx->win, Listen(cx, &TouchApp::OnFire));
    }

    static El* Render(TouchApp* self, Ctx* cx);
};

static Str Paragraphs(int n) {
    StrBuilder b;
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            b.Append(StrL("\n\n"));
        }
        b.Append(StrL(kParagraph));
    }
    return b.TakeStr();
}

static El* Measured(Ctx* cx, El* control, Bounds* out) {
    return Div(cx->a)->W(kFill)->BoundsOut(out)->Child(control);
}

static El* Row(Ctx* cx, Str label, El* button, const Theme& th) {
    return Div(cx->a)
        ->FlexRow()
        ->W(kFill)
        ->ItemsCenter()
        ->JustifyBetween()
        ->Child(TextEl(cx->a, label)->Fg(th.mutedFg))
        ->Child(button);
}

static El* PressButton(Ctx* cx, Str id, int which) {
    return component::Button::New(cx, id)
        ->Label(StrL("Long press"))
        ->OnClick(Listen(cx, &TouchApp::OnPress, which))
        ->IntoEl();
}

El* TouchApp::Render(TouchApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);

    El* inputSection =
        Div(a)
            ->FlexCol()
            ->W(kFill)
            ->Gap(8)
            ->Child(Row(cx, StrL("Input"),
                        PressButton(cx, StrL("press-input"), 0), th))
            ->Child(
                Measured(cx,
                         component::Input::New(cx, StrL("input"), &self->input)
                             ->IntoEl(),
                         &self->inputBounds));

    El* areaSection =
        Div(a)
            ->FlexCol()
            ->W(kFill)
            ->Gap(8)
            ->Child(Row(cx, StrL("Textarea"),
                        PressButton(cx, StrL("press-textarea"), 1), th))
            ->Child(Measured(
                cx,
                component::Textarea::New(cx, StrL("textarea"), &self->textarea)
                    ->H(120)
                    ->IntoEl(),
                &self->textareaBounds));

    El* textSection =
        Div(a)
            ->FlexCol()
            ->W(kFill)
            ->Gap(8)
            ->Child(Row(cx, StrL("TextView"),
                        PressButton(cx, StrL("press-text"), 2), th))
            ->Child(component::TextView::New(cx, self->before)
                        ->Selectable()
                        ->IntoEl())
            ->Child(Measured(cx,
                             component::TextView::New(cx, Str(kProse))
                                 ->Selectable()
                                 ->IntoEl(),
                             &self->textBounds))
            ->Child(component::TextView::New(cx, self->after)
                        ->Selectable()
                        ->IntoEl());

    return Div(a)
        ->Id(StrL("page"))
        ->FlexCol()
        ->SizeFull()
        ->ScrollY(self->scroll)
        ->ScrollId(HashClickId(StrL("page")))
        ->OnScroll(Listen(cx, &TouchApp::OnPageScroll))
        ->Pad(24)
        ->Gap(24)
        ->Bg(th.tokens.background)
        ->Child(inputSection)
        ->Child(areaSection)
        ->Child(textSection);
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    Entity<TouchApp> view = EntityNew<TouchApp>(app);
    TouchApp* self = view.Get(app);
    InputSetValue(&self->input,
                  StrL("The quick brown fox jumps over the lazy dog"));
    self->textarea.kind = InputKind::Textarea;
    StrBuilder lines;
    for (int n = 1; n <= 12; n++) {
        if (n > 1) {
            lines.AppendChar('\n');
        }
        lines.Append(Str(fmt(
            "Line %d: online content authoring isn't a solved problem.", n)));
    }
    Str joined = lines.TakeStr();
    InputSetValue(&self->textarea, joined);
    StrFree(joined);
    self->before = Paragraphs(1);
    self->after = Paragraphs(6);
    return KitRunView(StrL("Touch Selection"), 520, 640, view.id, app,
                      WinOpts{});
}
