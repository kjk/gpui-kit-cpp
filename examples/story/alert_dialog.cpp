#include "Story.h"

// One entry per section, in the order the Rust story renders them.
enum {
    AlertDefault = 0,
    AlertImperative,
    AlertIcon,
    AlertDestructive,
    AlertNoTitle,
    AlertCustomFooter,
    AlertCustomContent,
    AlertKeyboard,
    AlertConfirm,
    AlertPreventClose,
    AlertCount
};

struct AlertDialogStory {
    int open = -1;

    static El* Render(AlertDialogStory* self, Ctx* cx);
};

static void OpenAlert(AlertDialogStory* self, Ctx* cx, const ClickEvent*,
                      int64_t which) {
    self->open = (int)which;
    Notify(cx);
}
static void CloseAlert(AlertDialogStory* self, Ctx* cx, const ClickEvent*) {
    self->open = -1;
    Notify(cx);
}

// What each alert's on_ok / on_cancel push before they answer. An empty
// message is a callback the Rust story does not install; `false` is one that
// returns false, which keeps the dialog up.
struct AlertReply {
    const char* message;
    bool closes;
};

static AlertReply AlertOkReply(int which) {
    switch (which) {
        case AlertDefault:
            return {"Draft discarded", true};
        case AlertImperative:
            return {"File deleted", true};
        case AlertIcon:
            return {"Thank you for allowing network access", true};
        case AlertDestructive:
            return {"Your account has been deleted", true};
        case AlertCustomFooter:
            return {"Redirecting to login...", true};
        case AlertCustomContent:
            return {"Starting update...", true};
        case AlertPreventClose:
            // Return false to prevent closing.
            return {"Cannot close: Process still running", false};
        default:
            return {nullptr, true};
    }
}

static AlertReply AlertCancelReply(int which) {
    switch (which) {
        case AlertDefault:
            return {"Continuing to edit", true};
        case AlertCustomContent:
            return {"Update postponed", true};
        case AlertPreventClose:
            return {"Waiting...", false};
        default:
            return {nullptr, true};
    }
}

static void AlertReplyRun(AlertDialogStory* self, Ctx* cx, AlertReply r) {
    if (r.message) {
        StoryPushNotification(cx, Str(r.message));
    }
    if (r.closes) {
        self->open = -1;
    }
    Notify(cx);
}

static void OnAlertOk(AlertDialogStory* self, Ctx* cx, const ClickEvent*) {
    AlertReplyRun(self, cx, AlertOkReply(self->open));
}
static void OnAlertCancel(AlertDialogStory* self, Ctx* cx, const ClickEvent*) {
    AlertReplyRun(self, cx, AlertCancelReply(self->open));
}

struct AlertSpec {
    int which;
    const char* section;
    const char* description;
    const char* id;
    const char* label;
    // The trigger is outline; Destructive adds danger.
    bool dangerTrigger;
};

static const AlertSpec kAlerts[] = {
    {AlertDefault, "Default",
     "Compose the header, message, and footer actions.", "info-alert",
     "Discard Draft", false},
    {AlertImperative, "Imperative API",
     "Open an alert directly from the window.", "confirm-alert", "Delete File",
     false},
    {AlertIcon, "Icon", "Add a visual cue above the title.", "icon-alert",
     "Request Permission", false},
    {AlertDestructive, "Destructive",
     "Use a destructive action for irreversible choices.", "destructive-action",
     "Delete Account", true},
    {AlertNoTitle, "Without title", "Render content without a heading.",
     "without-title", "Continue without Title", false},
    {AlertCustomFooter, "Custom footer", "Replace the default action row.",
     "session-timeout", "Show Session Expiry", false},
    {AlertCustomContent, "Custom content",
     "Style header and footer regions independently.", "update",
     "Install Update", false},
    {AlertKeyboard, "Keyboard", "Disable keyboard dismissal when required.",
     "keyboard-disabled", "Review Notice", false},
    {AlertConfirm, "Confirm mode", "Provide standard OK and Cancel actions.",
     "overlay-closable", "Open Confirmation", false},
    {AlertPreventClose, "Prevent close", "Callbacks can keep the dialog open.",
     "prevent-close", "Close During Sync", false},
};

// Each section opens its own alert; alert_dialog_story.rs builds ten of them
// and no two are alike.
static component::AlertDialog* Alert(AlertDialogStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    Listener close = Listen(cx, &CloseAlert);
    // The alert layer never turns its overlay on, so the page behind keeps
    // its own colors.
    component::AlertDialog* d = component::AlertDialog::New(cx)
                                    ->Open(true)
                                    ->Overlay(false)
                                    ->OnClose(close)
                                    ->OnOk(Listen(cx, &OnAlertOk))
                                    ->OnCancel(Listen(cx, &OnAlertCancel));

    switch (self->open) {
        case AlertDefault:
            // A muted, ruled footer with Cancel beside a danger Discard.
            return d->Confirm()
                ->Title(StrL("Discard unsaved changes?"))
                ->Description(StrL("Your edits since the last save will be "
                                   "permanently lost."))
                ->OkText(StrL("Discard"))
                ->OkVariant(component::ButtonVariant::Danger)
                ->FooterStretch()
                ->FooterMuted()
                ->FooterDivider();
        case AlertImperative:
            // AlertDialog::icon: inline before the title.
            return d->ShowCancel(true)
                ->Icon(IconName::Info, th.danger)
                ->Title(StrL("Delete File"))
                ->Description(StrL("Are you sure you want to delete this file? "
                                   "This action cannot be undone."))
                ->OkText(StrL("Delete"))
                ->OkVariant(component::ButtonVariant::Danger);
        case AlertIcon:
            // A wider alert, a big warning glyph, and stacked full-width
            // actions with Allow above Deny.
            return d->Confirm()
                ->W(320)
                ->HeaderCentered()
                ->Icon(IconName::TriangleAlert, th.warning, 40)
                ->Title(StrL("Network Permission Required"))
                ->Description(
                    StrL("We need your permission to access the network to "
                         "provide better services. Please allow network access "
                         "in your system settings."))
                ->OkText(StrL("Allow"))
                ->CancelText(StrL("Deny"))
                ->FooterVertical();
        case AlertDestructive:
            return d->Confirm()
                ->Title(StrL("Delete Account"))
                ->Description(
                    StrL("This will permanently delete your account and all "
                         "associated data. This action cannot be undone."))
                ->OkText(StrL("Delete"))
                ->OkVariant(component::ButtonVariant::Danger, true)
                ->FooterStretch();
        case AlertNoTitle:
            // confirm(): no heading, just the question and OK / Cancel.
            return d->Confirm()
                ->Body(StoryTxt(cx, StrL("Continue with this action?"), 16,
                                th.foreground)
                           ->Wrap()
                           ->W(kFill));
        case AlertCustomFooter: {
            // footer(): one full-width primary action in place of the row.
            El* signIn = component::Button::New(cx, StrL("sign-in"))
                             ->Label(StrL("Sign in"))
                             ->Primary()
                             // push_notification then close_dialog: the same
                             // pair on_ok does for this section.
                             ->OnClick(Listen(cx, &OnAlertOk))
                             ->IntoEl()
                             ->W(kFill);
            return d->Title(StrL("Session Expired"))
                ->Description(StrL("Your session has expired due to "
                                   "inactivity. Please log in again to "
                                   "continue."))
                ->Footer(signIn);
        }
        case AlertCustomContent:
            return d->Confirm()
                ->Title(StrL("Update Available"))
                ->Description(StrL("A new version (v2.0.0) is available. This "
                                   "update includes new features and bug "
                                   "fixes."))
                ->OkText(StrL("Update"))
                ->CancelText(StrL("Later"))
                ->FooterStretch()
                ->FooterMuted();
        case AlertKeyboard:
            // keyboard(false): Esc is ignored, and there is nothing to
            // cancel. Rust hangs the key context off the flag, so the alert
            // simply does not declare it and neither binding exists.
            return d->Keyboard(false)
                ->Title(StrL("Important Notice"))
                ->Description(StrL("Please read this important notice "
                                   "carefully before proceeding."))
                ->OkText(StrL("Got It"));
        case AlertConfirm:
            return d->Confirm()
                ->Title(StrL("Are you sure?"))
                ->Body(StoryTxt(cx, StrL("Continue with this action?"), 16,
                                th.foreground)
                           ->Wrap()
                           ->W(kFill));
        default: {
            // Both callbacks return false, so neither button dismisses it;
            // only the x in the corner does.
            return d->ShowCancel(true)
                ->Title(StrL("Sync in progress"))
                ->Description(StrL("Your changes are still syncing. The dialog "
                                   "remains open until syncing finishes."))
                ->CloseButton()
                ->OkText(StrL("Close"))
                ->CancelText(StrL("Wait"));
        }
    }
}

El* AlertDialogStory::Render(AlertDialogStory* self, Ctx* cx) {
    WinSize size = WindowSize(cx->win);
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);
    Listener open = Listen(cx, &OpenAlert);

    for (size_t i = 0; i < sizeof(kAlerts) / sizeof(kAlerts[0]); i++) {
        const AlertSpec& s = kAlerts[i];
        El* sec = StorySection(cx, s.section, s.description);
        component::Button* btn = component::Button::New(cx, Str(s.id))
                                     ->Label(Str(s.label))
                                     ->Outline()
                                     ->OnClick(ListenerArg(open, s.which));
        if (s.dangerTrigger) {
            btn->Danger();
        }
        StorySectionAdd(sec, btn->IntoEl());
        page->Child(sec);
    }

    if (self->open >= 0) {
        page->Child(Alert(self, cx)->IntoEl(size));
    }
    return page;
}

STORY_PAGE(StoryAlertDialog, AlertDialogStory);
