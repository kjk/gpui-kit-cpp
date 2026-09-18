#include "gpui.h"

using namespace gpui;

// examples/webview — the gpui-kit example of the same name: an address
// bar over a webview, Enter loads what is in it.
//
// The webview is an OS control sitting over the window (WebView2 on Windows,
// WKWebView on macOS), so it covers whatever is behind its box and does not
// take part in the element tree's painting. That is why it gets a bordered
// box of its own here, exactly as the Rust example gives it one.
struct Example {
    InputState address;
    Entity<WebView> web;
    bool started = false;

    static void OnAddress(Example* self, Ctx* cx, const InputEvent* ev) {
        if (!ev || ev->kind != InputEventKind::PressEnter) {
            return;
        }
        WebView* web = self->web.Get(cx->app);
        if (web) {
            WebViewLoadUrl(web, InputValue(&self->address));
        }
    }

    static El* Render(Example* self, Ctx* cx) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);

        // The webview is made on the first frame, because that is the first
        // time there is a window to parent it into. `started` is set before
        // the call and not after: making one runs the message loop while it
        // waits, so this Render can be entered again before it returns.
        if (!self->started) {
            self->started = true;
            self->address.onChange =
                ListenTo(Entity<Example>{cx->self}, &Example::OnAddress);

            wry::WebViewAttributes attrs;
            attrs.url = InputValue(&self->address);
            self->web = WebViewNew(cx, &attrs);
        }

        El* bar = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->Child(
            component::Input::New(cx, StrL("address"), &self->address)
                ->IntoEl());

        El* frame = Div(a)
                        ->Flex1()
                        ->Border(1, th.border)
                        ->Child(WebViewEl(self->web, cx));

        return Div(a)
            ->FlexCol()
            ->Pad(8)
            ->Gap(12)
            ->SizeFull()
            ->Bg(th.background)
            ->Child(bar)
            ->Child(frame);
    }
};

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Dark);

    Entity<Example> view = EntityNew<Example>(app);
    Example* self = view.Get(app);
    InputSetValue(&self->address, StrL("https://gpui-kit.com"));

    if (!wry::WebViewAvailable()) {
        // Rust has no equivalent — `build_as_child` panics. Saying it out
        // loud is worth more than a window with a hole in it.
        logf(
            "webview: no native webview runtime on this machine; the page will "
            "stay "
            "empty\n");
    }
    return AppRunView(StrL("WebView"), 1024, 768, view.id, app, WinOpts{});
}
