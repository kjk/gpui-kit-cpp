#include "gpui.h"

using namespace gpui;

// examples/webview — the gpui-kit example of the same name: an address
// bar over a webview, Enter loads what is in it.
//
// The webview is an OS control sitting over the window (WebView2 on Windows,
// WKWebView on macOS, WebKitGTK on Linux), so it covers whatever is behind
// its box and does not take part in the element tree's painting. That is why
// it gets a bordered box of its own here, exactly as the Rust example gives
// it one.
struct Example {
    InputState address;
    Entity<WebView> web;
    bool started = false;
    bool about = false;

    static void OnAddress(Example* self, Ctx* cx, const InputEvent* ev) {
        if (!ev || ev->kind != InputEventKind::PressEnter) {
            return;
        }
        WebView* web = self->web.Get(cx->app);
        if (web) {
            WebViewLoadUrl(web, InputValue(&self->address));
        }
    }

    static void OnBack(Example* self, Ctx* cx, const ClickEvent*) {
        if (WebView* web = self->web.Get(cx->app)) {
            WebViewBack(web);
        }
    }

    static void OnForward(Example* self, Ctx* cx, const ClickEvent*) {
        if (WebView* web = self->web.Get(cx->app)) {
            WebViewForward(web);
        }
    }

    static void OnReload(Example* self, Ctx* cx, const ClickEvent*) {
        WebView* web = self->web.Get(cx->app);
        if (wry::WebView* raw = WebViewRaw(web)) {
            wry::WebViewReload(raw);
        }
    }

    static void OnAbout(Example* self, Ctx* cx, const ClickEvent*) {
        self->about = true;
        Notify(cx);
    }

    static void OnAboutClose(Example* self, Ctx* cx, const ClickEvent*) {
        self->about = false;
        Notify(cx);
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

        El* bar =
            Div(a)
                ->FlexRow()
                ->Gap(8)
                ->ItemsCenter()
                ->Child(component::Button::New(cx, StrL("back"))
                            ->Ghost()
                            ->Icon(IconName::ChevronLeft)
                            ->OnClick(Listen(cx, &Example::OnBack))
                            ->IntoEl())
                ->Child(component::Button::New(cx, StrL("forward"))
                            ->Ghost()
                            ->Icon(IconName::ChevronRight)
                            ->OnClick(Listen(cx, &Example::OnForward))
                            ->IntoEl())
                ->Child(
                    component::Input::New(cx, StrL("address"), &self->address)
                        ->IntoEl())
                ->Child(component::DropdownMenu::New(cx, StrL("webview-more"))
                            ->Trigger(component::Button::New(cx, StrL("more"))
                                          ->Ghost()
                                          ->Icon(IconName::Ellipsis)
                                          ->IntoEl())
                            ->Menu(component::PopupMenu::New(
                                       cx, StrL("webview-more-menu"))
                                       ->Menu(StrL("Reload"))
                                       ->OnClick(Listen(cx, &Example::OnReload))
                                       ->Separator()
                                       ->Menu(StrL("About"))
                                       ->OnClick(Listen(cx, &Example::OnAbout)))
                            ->IntoEl());

        El* frame = Div(a)
                        ->Flex1()
                        ->Border(1, th.border)
                        ->Child(WebViewEl(self->web, cx));

        El* root = Div(a)
                       ->FlexCol()
                       ->Pad(8)
                       ->Gap(12)
                       ->SizeFull()
                       ->Bg(th.background)
                       ->Child(bar)
                       ->Child(frame);
        if (self->about) {
            root->Child(
                component::Dialog::New(cx)
                    ->Open(true)
                    ->Title(StrL("About"))
                    ->Body(TextEl(a, StrL("A WebView embedded in a GPUI Kit "
                                          "window.")))
                    ->OnClose(Listen(cx, &Example::OnAboutClose))
                    ->IntoEl(WindowSize(cx->win)));
        }
        return root;
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
    return KitRunView(StrL("WebView"), 1024, 768, view.id, app, WinOpts{});
}
