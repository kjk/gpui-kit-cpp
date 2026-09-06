#ifndef GPUI_UI_DIALOG_H_
#define GPUI_UI_DIALOG_H_
/* Themed dialog — crates/ui/src/dialog */

#include "ui/sizing.h"
#include "ui/button.h"

namespace gpui {

namespace component {

// crates/ui/src/dialog/dialog.rs::ANIMATION_DURATION. The runtime motion seam
// uses milliseconds rather than std::time::Duration.
constexpr float ANIMATION_DURATION = 250.f;

// The source keeps the standard action row in one value and lets Dialog and
// AlertDialog share it. Listener is this runtime's stale-safe projection of
// the three retained Rust callbacks.
struct DialogButtonProps {
    Str okText = {};
    ButtonVariant okVariant = ButtonVariant::Primary;
    Str cancelText = {};
    ButtonVariant cancelVariant = ButtonVariant::Default;
    bool showCancel = false;
    Listener onOk = {};
    Listener onCancel = {};
    Listener onClose = {};

    DialogButtonProps* OkText(Str value);
    DialogButtonProps* OkVariant(ButtonVariant value);
    DialogButtonProps* CancelText(Str value);
    DialogButtonProps* CancelVariant(ButtonVariant value);
    DialogButtonProps* ShowCancel(bool value = true);
    DialogButtonProps* OnOk(Listener value);
    DialogButtonProps* OnCancel(Listener value);
    DialogButtonProps* OnClose(Listener value);
    El* RenderOk(Ctx* cx, Str id, bool outline = false) const;
    El* RenderCancel(Ctx* cx, Str id) const;
};

// Declarative dialog parts retain their own element, matching the seven Rust
// files in ui/dialog instead of making every caller rebuild their defaults.
struct DialogContent {
    El* root = nullptr;
    static DialogContent* New(Ctx* cx);
    DialogContent* Child(El* child);
    El* IntoEl();
};

struct DialogHeader {
    El* root = nullptr;
    static DialogHeader* New(Ctx* cx);
    DialogHeader* Child(El* child);
    El* IntoEl();
};

struct DialogTitle {
    El* root = nullptr;
    static DialogTitle* New(Ctx* cx);
    DialogTitle* Child(El* child);
    El* IntoEl();
};

struct DialogDescription {
    El* root = nullptr;
    static DialogDescription* New(Ctx* cx);
    DialogDescription* Child(El* child);
    El* IntoEl();
};

// Rust exposes this as a marker trait. POD elements use an explicit marker
// value because they do not have a component inheritance graph.
struct DialogFooterButton {
    bool cancel = false;
    bool action = false;
    bool IsCancel() const { return cancel; }
    bool IsAction() const { return action; }
};

struct DialogFooter {
    El* root = nullptr;
    static DialogFooter* New(Ctx* cx);
    DialogFooter* Child(El* child);
    El* IntoEl();
};

struct DialogClose {
    Ctx* cx = nullptr;
    El* root = nullptr;
    El* slot = nullptr;
    DialogFooterButton semantic = {true, false};
    static DialogClose* New(Ctx* cx);
    DialogClose* Child(El* child);
    // trigger(build): the supplied button gets the accessible name "Close"
    // and cancel activation, and the wrapper stops handling clicks itself.
    // The caller supplies only presentation — `Small()->Ghost()->Icon(..)`.
    DialogClose* Trigger(Button* button);
    El* IntoEl();
};

struct DialogAction {
    El* root = nullptr;
    DialogFooterButton semantic = {false, true};
    static DialogAction* New(Ctx* cx);
    DialogAction* Child(El* child);
    El* IntoEl();
};

struct Dialog {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str title = {};
    Str description = {};
    bool open = false;
    El* body = nullptr;
    // content() in Rust can replace the title/body/footer composition with a
    // complete DialogContent tree. Surface is that tree; the Dialog still
    // owns the modal host, panel, overlay, and close button.
    El* surface = nullptr;
    DialogButtonProps buttonProps = {};

    // AlertDialog::w.
    float width = 448;
    float height = 0;
    // DialogProps::overlay. The alert story's dialogs never tint the page.
    bool overlay = true;
    bool overlayClosable = true;
    // close_on_escape. Rust hangs the key context off it, so turning it off
    // takes Enter with it: the two bindings live in the one context, and a
    // dialog that does not declare it has neither.
    bool keyboard = true;
    // BaseDialogRoot::AlertDialog: the themed surface is shared, while the
    // modal host keeps its alert-dialog accessibility role and dismissal
    // semantics.
    bool alertHost = false;
    // Root assigns one layer index per active dialog. Each successive layer
    // sits 16px lower and owns a distinct focus trap.
    int layerIx = 0;
    float radius = 0;
    Background background = {};
    Rgba foreground = {};
    bool hasBackground = false;
    bool hasForeground = false;
    // AlertDialog::icon sits inline before the title. A story that builds its
    // own DialogHeader can center the group instead and put a large glyph
    // above it.
    IconName icon = IconName::None;
    Rgba iconColor = {};
    bool hasIconColor = false;
    float iconSize = 16;
    bool headerCentered = false;

    // Compatibility extension used by the story's destructive outline case;
    // the source's DialogButtonProps otherwise owns the action configuration.
    bool okOutline = false;
    // AlertDialog::close_button, the x in the corner.
    bool closeButton = false;

    // DialogFooter. `footer` replaces the action row outright; the flags
    // restyle the row the way the stories refine DialogFooter.
    El* footer = nullptr;
    bool footerVertical = false;
    // The buttons share the row (flex_1) rather than sitting at its end.
    bool footerStretch = false;
    bool footerMuted = false;
    bool footerDivider = false;

    static Dialog* New(Ctx* cx);
    Dialog* Title(Str s);
    Dialog* Description(Str s);
    Dialog* Open(bool v);
    Dialog* Body(El* e);
    Dialog* Surface(El* e);
    Dialog* W(float px);
    Dialog* H(float px);
    Dialog* Overlay(bool v);
    Dialog* OverlayClosable(bool v);
    Dialog* Keyboard(bool v);
    Dialog* Layer(int ix);
    Dialog* Radius(float px);
    Dialog* Bg(Background color);
    Dialog* Fg(Rgba color);
    Dialog* Icon(IconName n, Rgba color, float size = 16);
    Dialog* HeaderCentered(bool v = true);
    Dialog* OkText(Str s);
    Dialog* CancelText(Str s);
    Dialog* CancelVariant(ButtonVariant v);
    Dialog* OkVariant(ButtonVariant v, bool outline = false);
    Dialog* ShowCancel(bool v);
    Dialog* ButtonProps(const DialogButtonProps& value);
    // AlertDialog::confirm(): the standard OK / Cancel pair.
    Dialog* Confirm();
    Dialog* CloseButton(bool v = true);
    Dialog* Footer(El* e);
    Dialog* FooterVertical(bool v = true);
    Dialog* FooterStretch(bool v = true);
    Dialog* FooterMuted(bool v = true);
    Dialog* FooterDivider(bool v = true);
    Dialog* OnClose(Listener fn);
    Dialog* OnCancel(Listener fn);
    Dialog* OnOk(Listener fn);
    El* IntoEl(WinSize size);

  private:
    // `base` with the layer index on it: two open dialogs must not share a
    // hover or a click id, which a flat string would give them.
    Str LayerId(Str base) const;
    El* Header();
    El* Actions();
};

// The source's AlertDialog is a distinct opinionated façade around Dialog,
// not a mode bit on it. Keep that public structure while forwarding the
// compatible rendering vocabulary to the shared surface implementation.
struct AlertDialog {
    Dialog* base = nullptr;

    static AlertDialog* New(Ctx* cx);
    AlertDialog* Title(Str value);
    AlertDialog* Description(Str value);
    AlertDialog* Open(bool value);
    AlertDialog* Body(El* value);
    AlertDialog* Surface(El* value);
    AlertDialog* W(float value);
    AlertDialog* H(float value);
    AlertDialog* Overlay(bool value);
    AlertDialog* Keyboard(bool value);
    AlertDialog* Layer(int value);
    AlertDialog* Radius(float value);
    AlertDialog* Bg(Background value);
    AlertDialog* Fg(Rgba value);
    AlertDialog* Icon(IconName value, Rgba color, float size = 16);
    AlertDialog* HeaderCentered(bool value = true);
    AlertDialog* ButtonProps(const DialogButtonProps& value);
    AlertDialog* OkText(Str value);
    AlertDialog* CancelText(Str value);
    AlertDialog* CancelVariant(ButtonVariant value);
    AlertDialog* OkVariant(ButtonVariant value, bool outline = false);
    AlertDialog* ShowCancel(bool value);
    AlertDialog* Confirm();
    AlertDialog* CloseButton(bool value = true);
    AlertDialog* Footer(El* value);
    AlertDialog* FooterVertical(bool value = true);
    AlertDialog* FooterStretch(bool value = true);
    AlertDialog* FooterMuted(bool value = true);
    AlertDialog* FooterDivider(bool value = true);
    AlertDialog* OnClose(Listener value);
    AlertDialog* OnCancel(Listener value);
    AlertDialog* OnOk(Listener value);
    El* IntoEl(WinSize size);
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_DIALOG_H_
