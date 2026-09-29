#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static void OpenAlert(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->alertOpen = true;
    Notify(cx);
}

static void CloseAlert(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->alertOpen = false;
    Notify(cx);
}

El* ShowcaseAlertDialog(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    El* root = Div(a)->FlexCol();
    root->Child(Button::New(cx, StrL("open-alert-dialog"))
                    ->OnClick(Listen(cx, &OpenAlert))
                    ->H(28)
                    ->PadX(12)
                    ->ItemsCenter()
                    ->JustifyCenter()
                    ->Bg(Rgb(0, 0, 0))
                    ->Child(TextEl(a, StrL("Delete project"))
                                ->Font(12)
                                ->Fg(ExampleRgb(0xffffff))));
    if (!app->alertOpen) {
        return root;
    }

    // The popup is the panel itself, centered by the dialog host.
    El* popup =
        AlertDialogPopup::New(cx)
            ->W(288)
            ->Pad(12)
            ->FlexCol()
            ->Bg(ExampleRgb(0xffffff))
            ->Border(1, ExampleRgb(0x171717))
            ->Child(AlertDialogTitle::New(cx)
                        ->Child(TextEl(a, StrL("Delete project?"))
                                    ->Font(14)
                                    ->Fg(ExampleRgb(0x171717))))
            ->Child(Div(a)->H(8))
            ->Child(AlertDialogDescription::New(cx)
                        ->Child(TextEl(a, StrL("This permanently deletes Acme "
                                               "Studio and all of its data."))
                                    ->Font(12)
                                    ->Fg(ExampleRgb(0x525252))
                                    ->Wrap()
                                    ->MaxW(264)))
            ->Child(Div(a)->H(12))
            ->Child(Div(a)
                        ->FlexRow()
                        ->W(kFill)
                        ->JustifyEnd()
                        ->Gap(8)
                        ->Child(AlertDialogCancel::New(cx)->Child(
                            Button::New(cx, StrL("cancel-delete"))
                                ->H(28)
                                ->PadX(12)
                                ->ItemsCenter()
                                ->Border(1, ExampleRgb(0xd4d4d4))
                                ->Child(TextEl(a, StrL("Cancel"))
                                            ->Font(12)
                                            ->Fg(ExampleRgb(0x171717)))))
                        ->Child(AlertDialogAction::New(cx)->Child(
                            Button::New(cx, StrL("confirm-delete"))
                                ->H(28)
                                ->PadX(12)
                                ->ItemsCenter()
                                ->Border(1, ExampleRgb(0x171717))
                                ->Bg(ExampleRgb(0x171717))
                                ->Child(TextEl(a, StrL("Delete"))
                                            ->Font(12)
                                            ->Fg(ExampleRgb(0xffffff))))));

    El* backdrop = AlertDialogBackdrop::New(cx)
                       ->Absolute()
                       ->Top(0)
                       ->Left(0)
                       ->W(kFill)
                       ->H(kFill)
                       ->Bg(Rgba8(0, 0, 0, 46))
                       ->Click(HashClickId(StrL("alert-backdrop")));
    // dialog.rs handles the alert's keyboard too — an alert has no bindings
    // of its own, it rides the Dialog context — and the two buttons dispatch
    // Cancel and Confirm rather than carrying a handler each, so this is the
    // one place the page says what they do.
    DialogBindKeys(cx, popup, StrL("showcase-alert"), Listen(cx, &CloseAlert),
                   Listen(cx, &CloseAlert), {});
    root->Child(
        AlertDialog::New(cx)->Backdrop(backdrop)->Popup(popup)->IntoEl());
    return root;
}

SHOWCASE_PAGE(CompAlertDialog, ShowcaseAlertDialog);
