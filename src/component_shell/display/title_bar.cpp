// crates/component-shell/src/shell/display/title_bar.rs

#include "component_shell/families.h"
#include "component_shell/display/common.h"
#include "shell/view.h"
#include "ui/title_bar.h"

namespace gpui::component_shell::display::title_bar {

struct TitleBarPayload {};

struct TitleBarOp {
    shell::ComponentArgument close = {};
};

static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<TitleBarPayload>();
}

static bool RecordClose(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    TitleBarOp* op = build->New<TitleBarOp>();
    op->close = args[0];
    return true;
}

static void RunClose(const shell::ComponentEventBinding* binding,
                     ScriptView* view, Ctx* cx, const void*) {
    binding->callback.InvokeAndReport(
        view->runtime, "TitleBar.on_close_window callback failed", nullptr, 0,
        cx->win, cx->app);
}

static El* Materialize(MaterializeRequest* request) {
    if (!shell::PayloadIs<TitleBarPayload>(request->payload)) {
        request->Fail(StrL("TitleBar received an incompatible payload"));
        return nullptr;
    }
    component::TitleBar* bar = component::TitleBar::New(request->cx);
    EachMethod<TitleBarOp>(request, [&](const TitleBarOp& op) {
        bar->OnCloseWindow(shell::ComponentListener(
            request->cx, &RunClose, request->ResolveCallback(op.close)));
    });
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) bar->Child(children[i]);
    return request->ApplyStyle(bar->IntoEl());
}

static constexpr ArgumentDescriptor kCloseArgs[] = {
    {"callback", SchemaCallback("(cx: Context) => void")}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"TitleBar", {}, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"on_close_window", kCloseArgs,
     "Runs the callback instead of closing the window when the title bar's "
     "own close button is clicked. Only Linux draws that button; elsewhere "
     "the system owns closing.",
     &RecordClose},
};
static constexpr ComponentDescriptor kTitleBar = {
    "TitleBar", kConstructors, kMethods,
    "The window title bar, rendered as the first child of the application's "
    "root view. It draws the window controls, drags and zooms the window, "
    "and lays its children out between the leading edge and the controls; "
    "stop mouse-down propagation on interactive children so pressing them "
    "does not start a window move.",
    &Materialize};

} // namespace gpui::component_shell::display::title_bar

namespace gpui::component_shell {

bool RegisterDisplayTitleBar(shell::ComponentRegistry* registry,
                             shell::RegistryError* error) {
    return registry->Register(&display::title_bar::kTitleBar, error);
}

} // namespace gpui::component_shell
