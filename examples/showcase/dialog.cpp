#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static void OpenDlg(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->dialogOpen = true;
    app->input.focused = true;
    Notify(cx);
}

static void CloseDlg(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->dialogOpen = false;
    app->input.focused = false;
    Notify(cx);
}

static void FocusDlgField(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->input.focused = true;
    Notify(cx);
}

El* ShowcaseDialog(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    El* root = Div(a)->FlexCol();
    root->Child(Button::New(cx, StrL("open-dialog"))
                    ->OnClick(Listen(cx, &OpenDlg))
                    ->H(28)
                    ->PadX(12)
                    ->ItemsCenter()
                    ->JustifyCenter()
                    ->Bg(Rgb(0, 0, 0))
                    ->Child(TextEl(a, StrL("Edit profile"))
                                ->Font(12)
                                ->Fg(ExampleRgb(0xffffff))));
    if (!app->dialogOpen) {
        return root;
    }
    // The popup is the panel itself: the dialog host centers it, and it keeps
    // presses on itself from reaching the backdrop.
    El* popup =
        DialogPopup::New(cx)
            ->W(288)
            ->Pad(12)
            ->FlexCol()
            ->Bg(ExampleRgb(0xffffff))
            ->Border(1, ExampleRgb(0xd4d4d4))
            ->Child(DialogTitle::New(cx)->Child(TextEl(a, StrL("Edit profile"))
                                                    ->Font(12)
                                                    ->Fg(ExampleRgb(0x171717))
                                                    ->Semibold()))
            ->Child(DialogDescription::New(cx)->Child(
                TextEl(a,
                       StrL("Update the public details shown on your profile."))
                    ->Font(12)
                    ->Fg(ExampleRgb(0x737373))
                    ->Wrap()
                    ->MaxW(264)))
            ->Child(Div(a)->PadT(12)->Child(TextEl(a, StrL("Display name"))
                                                ->Font(14)
                                                ->Fg(ExampleRgb(0x171717))))
            ->Child(
                InputBase::New(cx, StrL("dialog-name"), true)
                    ->OnClick(Listen(cx, &FocusDlgField))
                    ->FocusId(0)
                    ->W(264)
                    ->H(28)
                    ->PadX(8)
                    ->ItemsCenter()
                    ->Border(1, ExampleRgb(0xd4d4d4))
                    ->Child(Input::New(cx, &app->input, ShowcaseEditorStyle())))
            ->Child(
                Div(a)
                    ->PadT(12)
                    ->FlexRow()
                    ->JustifyEnd()
                    ->Gap(8)
                    // Rust hangs Cancel off DialogClose, which dispatches the
                    // Cancel action. The button inside carries a click id of
                    // its own and the hit test takes the innermost rect, so
                    // it needs the handler too.
                    ->Child(
                        DialogClose::New(cx, HashClickId(StrL("dialog-close")))
                            ->OnClick(Listen(cx, &CloseDlg))
                            ->Child(
                                Button::New(cx, StrL("dialog-cancel"), false,
                                            Listen(cx, &CloseDlg))
                                    ->H(28)
                                    ->PadX(12)
                                    ->ItemsCenter()
                                    ->Border(1, ExampleRgb(0xd4d4d4))
                                    ->Child(TextEl(a, StrL("Cancel"))
                                                ->Font(12)
                                                ->Fg(ExampleRgb(0x171717)))))
                    ->Child(Button::New(cx, StrL("dialog-save"), false,
                                        Listen(cx, &CloseDlg))
                                ->H(28)
                                ->PadX(12)
                                ->ItemsCenter()
                                ->Bg(ExampleRgb(0x171717))
                                ->Child(TextEl(a, StrL("Save changes"))
                                            ->Font(12)
                                            ->Fg(ExampleRgb(0xffffff)))));
    El* backdrop = DialogBackdrop::New(cx)
                       ->Absolute()
                       ->Top(0)
                       ->Left(0)
                       ->W(kFill)
                       ->H(kFill)
                       ->Bg(Rgba8(0, 0, 0, 51))
                       ->Click(HashClickId(StrL("dialog-backdrop")))
                       ->OnClick(Listen(cx, &CloseDlg));
    root->Child(Dialog::New(cx)->Backdrop(backdrop)->Popup(popup)->IntoEl());
    return root;
}

SHOWCASE_PAGE(CompDialog, ShowcaseDialog);
