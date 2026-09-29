#include "gpui.h"

using namespace gpui;

// examples/input — an Input bound to its state. The view subscribes to the
// state's change event and republishes the greeting from the value.
struct Example {
    InputState inputState;
    char displayText[560] = {};
    bool subscribed = false;

    static void OnChange(Example* self, Ctx* cx, const InputEvent*) {
        TempStr text = fmt("Hello, %s!", Str(InputCStr(&self->inputState)));
        StrCopyZ(self->displayText, (int)sizeof(self->displayText), text.s);
        Notify(cx);
    }

    static El* Render(Example* self, Ctx* cx) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        if (!self->subscribed) {
            self->subscribed = true;
            self->inputState.onChange =
                ListenTo(Entity<Example>{cx->self}, &Example::OnChange);
        }
        return Div(a)
            ->FlexCol()
            ->Pad(20)
            ->Gap(8)
            ->SizeFull()
            ->ItemsCenter()
            ->JustifyCenter()
            ->Bg(th.tokens.background)
            // A press on the field focuses it and places the caret;
            // the window does that for any element bound to a state, the
            // way GPUI's focus handle does.
            ->Child(component::Input::New(cx, StrL("input"), &self->inputState)
                        ->IntoEl())
            ->Child(TextEl(a, Str(self->displayText))->Fg(th.foreground));
    }
};

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    Entity<Example> view = EntityNew<Example>(app);
    Example* self = view.Get(app);
    InputSetPlaceholder(&self->inputState, StrL("Enter your name"));

    return KitRunView(StrL("Input C++"), 800, 600, view.id, app, WinOpts{});
}
