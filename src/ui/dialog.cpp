#include "ui/i18n.h"
#include "ui/dialog.h"
#include "base/motion.h"
#include "ui/button.h"

namespace gpui {

namespace component {

static float DialogEase(float t) {
    // x1 = 1/3, x2 = 2/3 make the bezier's time mapping the identity, which
    // keeps the trajectory this dialog was tuned with before `cubic_bezier`
    // solved for x; vaul's (0.32, 0.72, 0, 1) is far more front-loaded under
    // the CSS-correct solver.
    return CubicBezier(1.f / 3.f, 0.72f, 2.f / 3.f, 1.f, t);
}

static void ApplyDialogButtonVariant(Button* button, ButtonVariant variant) {
    switch (variant) {
        case ButtonVariant::Primary:
            button->Primary();
            break;
        case ButtonVariant::Secondary:
            button->Secondary();
            break;
        case ButtonVariant::Danger:
            button->Danger();
            break;
        case ButtonVariant::Info:
            button->Info();
            break;
        case ButtonVariant::Success:
            button->Success();
            break;
        case ButtonVariant::Warning:
            button->Warning();
            break;
        case ButtonVariant::Ghost:
            button->Ghost();
            break;
        case ButtonVariant::Link:
            button->Link();
            break;
        case ButtonVariant::Text:
            button->Text();
            break;
        case ButtonVariant::Custom:
            // The Rust enum carries ButtonCustomVariant in this case. The
            // dialog's compatibility setter accepts only the discriminator,
            // so a payload-free Custom is the default button rather than a
            // fabricated custom palette.
        case ButtonVariant::Default:
            break;
    }
}

DialogButtonProps* DialogButtonProps::OkText(Str value) {
    okText = value;
    return this;
}
DialogButtonProps* DialogButtonProps::OkVariant(ButtonVariant value) {
    okVariant = value;
    return this;
}
DialogButtonProps* DialogButtonProps::CancelText(Str value) {
    cancelText = value;
    return this;
}
DialogButtonProps* DialogButtonProps::CancelVariant(ButtonVariant value) {
    cancelVariant = value;
    return this;
}
DialogButtonProps* DialogButtonProps::ShowCancel(bool value) {
    showCancel = value;
    return this;
}
DialogButtonProps* DialogButtonProps::OnOk(Listener value) {
    onOk = value;
    return this;
}
DialogButtonProps* DialogButtonProps::OnCancel(Listener value) {
    onCancel = value;
    return this;
}
DialogButtonProps* DialogButtonProps::OnClose(Listener value) {
    onClose = value;
    return this;
}
El* DialogButtonProps::RenderOk(Ctx* cx, Str id, bool outline) const {
    Button* button = Button::New(cx, id)
                         ->Label(okText.s ? okText : Tr("Dialog.ok"))
                         ->OnClickAction(action::Confirm());
    ApplyDialogButtonVariant(button, okVariant);
    if (outline) {
        button->Outline();
    }
    return button->IntoEl();
}
El* DialogButtonProps::RenderCancel(Ctx* cx, Str id) const {
    Button* button =
        Button::New(cx, id)
            ->Label(cancelText.s ? cancelText : Tr("Dialog.cancel"))
            ->OnClickAction(action::Cancel());
    ApplyDialogButtonVariant(button, cancelVariant);
    return button->IntoEl();
}

DialogContent* DialogContent::New(Ctx* cx) {
    DialogContent* part = ArenaNew<DialogContent>(cx->a);
    part->root = Div(cx->a)->FlexCol()->W(kFill)->Flex1()->Radius(
        ThemeNow(cx->app).radiusLg);
    return part;
}
DialogContent* DialogContent::Child(El* child) {
    root->Child(child);
    return this;
}
El* DialogContent::IntoEl() {
    return root;
}

DialogHeader* DialogHeader::New(Ctx* cx) {
    DialogHeader* part = ArenaNew<DialogHeader>(cx->a);
    part->root = Div(cx->a)->FlexCol()->Gap(8);
    return part;
}
DialogHeader* DialogHeader::Child(El* child) {
    root->Child(child);
    return this;
}
El* DialogHeader::IntoEl() {
    return root;
}

DialogTitle* DialogTitle::New(Ctx* cx) {
    DialogTitle* part = ArenaNew<DialogTitle>(cx->a);
    part->root =
        gpui::DialogTitle::New(cx)->Font(16)->Semibold()->LineHeight(1.f);
    return part;
}
DialogTitle* DialogTitle::Child(El* child) {
    root->Child(child);
    return this;
}
El* DialogTitle::IntoEl() {
    return root;
}

DialogDescription* DialogDescription::New(Ctx* cx) {
    DialogDescription* part = ArenaNew<DialogDescription>(cx->a);
    part->root = gpui::DialogDescription::New(cx)
                     ->Font(14)
                     ->Fg(ThemeNow(cx->app).mutedFg);
    return part;
}
DialogDescription* DialogDescription::Child(El* child) {
    root->Child(child);
    return this;
}
El* DialogDescription::IntoEl() {
    return root;
}

DialogFooter* DialogFooter::New(Ctx* cx) {
    DialogFooter* part = ArenaNew<DialogFooter>(cx->a);
    float radius = ThemeNow(cx->app).radiusLg;
    part->root =
        Div(cx->a)->FlexRow()->Gap(8)->JustifyEnd()->LineHeight(1.f)->Corners(
            0, 0, radius, radius);
    return part;
}
DialogFooter* DialogFooter::Child(El* child) {
    root->Child(child);
    return this;
}
El* DialogFooter::IntoEl() {
    return root;
}

DialogClose* DialogClose::New(Ctx* cx) {
    DialogClose* part = ArenaNew<DialogClose>(cx->a);
    part->cx = cx;
    part->slot = gpui::DialogClose::New(cx);
    part->root = Div(cx->a)->W(kFill)->H(kFill)->Child(part->slot);
    return part;
}
DialogClose* DialogClose::Child(El* child) {
    slot->Child(child);
    return this;
}
DialogClose* DialogClose::Trigger(Button* button) {
    if (!button) {
        return this;
    }
    // Rust hands the builder a base button that already carries the name
    // and the Cancel dispatch and styles what comes back; here the styled
    // button is what the caller made, and the activation goes on it before
    // it renders, so a loading one still withholds the click.
    button->AccessibilityLabel(StrL("Close"))->OnClickAction(action::Cancel());
    El* trigger = button->IntoEl();
    El* was = slot;
    slot = gpui::DialogClose::WithTrigger(cx, trigger);
    // Whatever children were added before the trigger stay, after it, the
    // way `.children(self.trigger).children(self.children)` orders them.
    for (El* child = was ? was->first : nullptr; child;) {
        El* next = child->next;
        child->next = nullptr;
        slot->Child(child);
        child = next;
    }
    root = Div(cx->a)->W(kFill)->H(kFill)->Child(slot);
    return this;
}
El* DialogClose::IntoEl() {
    return root;
}

DialogAction* DialogAction::New(Ctx* cx) {
    DialogAction* part = ArenaNew<DialogAction>(cx->a);
    part->root = Div(cx->a)
                     ->W(kFill)
                     ->H(kFill)
                     ->PathClick(StrL("dialog-action"))
                     ->OnClickAction(action::Confirm());
    return part;
}
DialogAction* DialogAction::Child(El* child) {
    root->Child(child);
    return this;
}
El* DialogAction::IntoEl() {
    return root;
}

Dialog* Dialog::New(Ctx* cx) {
    Arena* a = cx->a;
    Dialog* d = ArenaNew<Dialog>(a);
    d->a = a;
    d->cx = cx;
    return d;
}
Dialog* Dialog::Title(Str s) {
    title = s;
    return this;
}
Dialog* Dialog::Description(Str s) {
    description = s;
    return this;
}
Dialog* Dialog::Open(bool v) {
    open = v;
    return this;
}
Dialog* Dialog::Body(El* e) {
    body = e;
    return this;
}
Dialog* Dialog::Surface(El* e) {
    surface = e;
    return this;
}
Dialog* Dialog::W(float px) {
    width = px;
    return this;
}
Dialog* Dialog::H(float px) {
    height = px;
    return this;
}
Dialog* Dialog::Overlay(bool v) {
    overlay = v;
    return this;
}
Dialog* Dialog::OverlayClosable(bool v) {
    overlayClosable = v;
    return this;
}
Dialog* Dialog::Keyboard(bool v) {
    keyboard = v;
    return this;
}
Dialog* Dialog::Layer(int ix) {
    layerIx = ix;
    return this;
}
Dialog* Dialog::Radius(float px) {
    radius = px;
    return this;
}
Dialog* Dialog::Bg(Background color) {
    background = color;
    hasBackground = true;
    return this;
}
Dialog* Dialog::Fg(Rgba color) {
    foreground = color;
    hasForeground = true;
    return this;
}
Dialog* Dialog::Icon(IconName n, Rgba color, float size) {
    icon = n;
    iconColor = color;
    hasIconColor = true;
    iconSize = size;
    return this;
}
Dialog* Dialog::HeaderCentered(bool v) {
    headerCentered = v;
    return this;
}
Dialog* Dialog::OkText(Str s) {
    buttonProps.okText = s;
    return this;
}
Dialog* Dialog::CancelText(Str s) {
    buttonProps.cancelText = s;
    return this;
}
Dialog* Dialog::CancelVariant(ButtonVariant v) {
    buttonProps.cancelVariant = v;
    return this;
}
Dialog* Dialog::OkVariant(ButtonVariant v, bool outline) {
    buttonProps.okVariant = v;
    okOutline = outline;
    return this;
}
Dialog* Dialog::ShowCancel(bool v) {
    buttonProps.showCancel = v;
    return this;
}
Dialog* Dialog::ButtonProps(const DialogButtonProps& value) {
    buttonProps = value;
    return this;
}
Dialog* Dialog::Confirm() {
    buttonProps.showCancel = true;
    return this;
}
Dialog* Dialog::CloseButton(bool v) {
    closeButton = v;
    return this;
}
Dialog* Dialog::Footer(El* e) {
    footer = e;
    return this;
}
Dialog* Dialog::FooterVertical(bool v) {
    footerVertical = v;
    return this;
}
Dialog* Dialog::FooterStretch(bool v) {
    footerStretch = v;
    return this;
}
Dialog* Dialog::FooterMuted(bool v) {
    footerMuted = v;
    return this;
}
Dialog* Dialog::FooterDivider(bool v) {
    footerDivider = v;
    return this;
}
Dialog* Dialog::OnClose(Listener fn) {
    buttonProps.onClose = fn;
    return this;
}
Dialog* Dialog::OnCancel(Listener fn) {
    buttonProps.onCancel = fn;
    return this;
}
Dialog* Dialog::OnOk(Listener fn) {
    buttonProps.onOk = fn;
    return this;
}

// DialogHeader: the icon, title and description, centered as a group once
// there is an icon above them.
El* Dialog::Header() {
    const Theme& th = ThemeNow(cx->app);
    El* head = Div(a)->FlexCol()->W(kFill)->Pad(16)->Gap(8);
    El* ic = nullptr;
    if (icon != IconName::None) {
        ic = IconEl(a, icon, iconSize)->Shrink0();
        if (hasIconColor) {
            ic->Fg(iconColor);
        }
    }
    if (headerCentered) {
        head->ItemsCenter();
        if (ic) {
            head->Child(ic);
        }
        ic = nullptr;
    }
    if (title.s && title.len > 0) {
        El* text = TextEl(a, title)->Font(16)->Semibold()->Fg(th.foreground);
        El* line = text;
        if (ic) {
            line = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->Child(ic)->Child(
                text);
        }
        head->Child(DialogTitle::New(cx)->Child(line)->IntoEl());
    } else if (ic) {
        head->Child(ic);
    }
    if (description.s && description.len > 0) {
        head->Child(DialogDescription::New(cx)
                        ->Child(TextEl(a, description)
                                    ->Font(14)
                                    ->Fg(th.mutedFg)
                                    ->Wrap()
                                    ->W(kFill))
                        ->IntoEl());
    }
    if (body) {
        head->Child(body);
    }
    return head;
}

// An id with the layer index on it, which is what makes two open dialogs two
// sets of controls rather than one shared set.
Str Dialog::LayerId(Str base) const {
    return StrDup(a, fmt("%s-%d", base, layerIx));
}

// DialogFooter: the action row, or whatever the caller put in its place.
El* Dialog::Actions() {
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)->W(kFill)->Pad(16)->Gap(8);
    if (footerVertical) {
        row->FlexCol();
    } else {
        row->FlexRow()->JustifyEnd();
    }
    if (footerMuted) {
        row->Bg(th.tokens.muted);
    }
    if (footerDivider) {
        row->BorderT(1, th.border);
    }
    if (footer) {
        row->Child(footer);
        return row;
    }

    // Every id here carries the layer: GPUI scopes an ElementId by its
    // ancestors' and the panel is `.id(layer_ix)`, so two open dialogs give
    // their buttons and their close x distinct identities. Flat hashes here
    // do not, and two dialogs at once shared one hover state — pointing at
    // the top one's close x lit up the one behind it too.
    El* cancel = nullptr;
    if (buttonProps.showCancel) {
        cancel = buttonProps.RenderCancel(cx, LayerId(StrL("dialog-cancel")));
    }
    El* ok = buttonProps.RenderOk(cx, LayerId(StrL("dialog-ok")), okOutline);

    // Stacked, the primary action leads; in a row it sits at the end.
    if (footerVertical) {
        row->Child(ok->W(kFill));
        if (cancel) {
            row->Child(cancel->W(kFill));
        }
        return row;
    }
    if (cancel) {
        row->Child(footerStretch ? cancel->Flex1() : cancel);
    }
    row->Child(footerStretch ? ok->Flex1() : ok);
    return row;
}

El* Dialog::IntoEl(WinSize size) {
    if (!open) {
        return Div(a);
    }
    const Theme& th = ThemeNow(cx->app);
    Edges windowPadding = WindowPaddings(cx->win);
    float viewW = size.dipW - windowPadding.left - windowPadding.right;
    float viewH = size.dipH - windowPadding.top - windowPadding.bottom;
    // The parts carry the padding, so a footer that tints or rules itself
    // reaches the panel's edges (AlertDialog::p_0 in the Rust story).
    El* panel = Div(a)
                    ->W(width)
                    ->FlexCol()
                    ->MinH(96)
                    ->Bg(hasBackground ? background : th.background)
                    ->Border(1, th.border)
                    ->Radius(radius > 0 ? radius : th.radiusLg)
                    ->ClipY();
    if (height > 0) {
        panel->H(height);
    }
    if (hasForeground) {
        panel->Fg(foreground);
    }
    if (surface) {
        panel->Child(surface);
    } else {
        panel->Child(Header());
        panel->Child(Actions());
    }
    if (closeButton) {
        // `DialogClose::new().absolute().top(top).right(right).trigger(..)`:
        // a small ghost icon button with the accessible name "Close" and
        // cancel activation, which is what gives it a title and an AXPress.
        El* x = DialogClose::New(cx)
                    ->Trigger(Button::New(cx, LayerId(StrL("dialog-close-x")))
                                  ->WithSize(UiSize::Small)
                                  ->Ghost()
                                  ->Icon(IconName::Close))
                    ->IntoEl();
        x->Absolute()->Top(8)->Right(8)->W(kAuto)->H(kAuto);
        panel->Child(x);
    }
    // Fixed, not absolute: Rust hangs the dialog off the window Root, so it
    // covers and centers on the window rather than on whatever page element
    // happens to contain it.
    // "fade-in" and "slide-down": the whole layer fades in over a quarter of a
    // second while the panel comes down from the top edge.
    float delta = MotionAppear(
        cx, MotionId(StrL("dialog"), StrDup(a, fmt("%d", layerIx))),
        ANIMATION_DURATION, DialogEase);
    El* backdrop = DialogBackdrop::New(cx)
                       ->Fixed()
                       ->Top(windowPadding.top)
                       ->Left(windowPadding.left)
                       ->W(viewW)
                       ->H(viewH);
    if (overlay) {
        backdrop->Bg(th.tokens.overlay);
    }
    if (overlayClosable) {
        backdrop->PathClick(LayerId(StrL("dialog-backdrop")))
            ->OnClickAction(action::Cancel());
    }
    // DialogProps::margin_top: a tenth of the viewport down from the top,
    // not centered in it.
    El* popup = DialogPopup::New(cx)
                    ->Fixed()
                    ->Top(windowPadding.top)
                    ->Left(windowPadding.left)
                    ->W(viewW)
                    ->H(viewH)
                    ->FlexCol()
                    ->ItemsCenter()
                    ->PadT((viewH * 0.1f + (float)layerIx * 16.f) * delta)
                    ->Child(panel);
    Str trap = StrDup(a, fmt("dialog-%d", layerIx));
    // The escape and enter bindings, on the popup that traps the focus. They
    // run the same two handlers the Cancel and OK buttons carry, which is
    // what Rust's on_action pair does with a ClickEvent::default().
    if (keyboard) {
        DialogBindKeys(cx, popup, trap, buttonProps.onCancel, buttonProps.onOk,
                       buttonProps.onClose);
    }
    El* host = nullptr;
    if (alertHost) {
        host = gpui::AlertDialog::New(cx)
                   ->Trap(trap)
                   ->Backdrop(backdrop)
                   ->Popup(popup)
                   ->IntoEl();
    } else {
        host = gpui::Dialog::New(cx)
                   ->Trap(trap)
                   ->Backdrop(backdrop)
                   ->Popup(popup)
                   ->IntoEl();
    }
    // `.with_animation("fade-in", .., |this, delta| this.opacity(delta))`:
    // the backdrop and the panel fade in together, as one layer.
    return host->Opacity(delta);
}

AlertDialog* AlertDialog::New(Ctx* cx) {
    AlertDialog* alert = ArenaNew<AlertDialog>(cx->a);
    // alert_dialog.rs: alerts never close from the backdrop and do not show
    // the corner close button unless explicitly requested.
    alert->base = Dialog::New(cx)->OverlayClosable(false)->CloseButton(false);
    alert->base->alertHost = true;
    return alert;
}
AlertDialog* AlertDialog::Title(Str value) {
    base->Title(value);
    return this;
}
AlertDialog* AlertDialog::Description(Str value) {
    base->Description(value);
    return this;
}
AlertDialog* AlertDialog::Open(bool value) {
    base->Open(value);
    return this;
}
AlertDialog* AlertDialog::Body(El* value) {
    base->Body(value);
    return this;
}
AlertDialog* AlertDialog::Surface(El* value) {
    base->Surface(value);
    return this;
}
AlertDialog* AlertDialog::W(float value) {
    base->W(value);
    return this;
}
AlertDialog* AlertDialog::H(float value) {
    base->H(value);
    return this;
}
AlertDialog* AlertDialog::Overlay(bool value) {
    base->Overlay(value);
    return this;
}
AlertDialog* AlertDialog::Keyboard(bool value) {
    base->Keyboard(value);
    return this;
}
AlertDialog* AlertDialog::Layer(int value) {
    base->Layer(value);
    return this;
}
AlertDialog* AlertDialog::Radius(float value) {
    base->Radius(value);
    return this;
}
AlertDialog* AlertDialog::Bg(Background value) {
    base->Bg(value);
    return this;
}
AlertDialog* AlertDialog::Fg(Rgba value) {
    base->Fg(value);
    return this;
}
AlertDialog* AlertDialog::Icon(IconName value, Rgba color, float size) {
    base->Icon(value, color, size);
    return this;
}
AlertDialog* AlertDialog::HeaderCentered(bool value) {
    base->HeaderCentered(value);
    return this;
}
AlertDialog* AlertDialog::ButtonProps(const DialogButtonProps& value) {
    base->ButtonProps(value);
    return this;
}
AlertDialog* AlertDialog::OkText(Str value) {
    base->OkText(value);
    return this;
}
AlertDialog* AlertDialog::CancelText(Str value) {
    base->CancelText(value);
    return this;
}
AlertDialog* AlertDialog::CancelVariant(ButtonVariant value) {
    base->CancelVariant(value);
    return this;
}
AlertDialog* AlertDialog::OkVariant(ButtonVariant value, bool outline) {
    base->OkVariant(value, outline);
    return this;
}
AlertDialog* AlertDialog::ShowCancel(bool value) {
    base->ShowCancel(value);
    return this;
}
AlertDialog* AlertDialog::Confirm() {
    base->Confirm();
    return this;
}
AlertDialog* AlertDialog::CloseButton(bool value) {
    base->CloseButton(value);
    return this;
}
AlertDialog* AlertDialog::Footer(El* value) {
    base->Footer(value);
    return this;
}
AlertDialog* AlertDialog::FooterVertical(bool value) {
    base->FooterVertical(value);
    return this;
}
AlertDialog* AlertDialog::FooterStretch(bool value) {
    base->FooterStretch(value);
    return this;
}
AlertDialog* AlertDialog::FooterMuted(bool value) {
    base->FooterMuted(value);
    return this;
}
AlertDialog* AlertDialog::FooterDivider(bool value) {
    base->FooterDivider(value);
    return this;
}
AlertDialog* AlertDialog::OnClose(Listener value) {
    base->OnClose(value);
    return this;
}
AlertDialog* AlertDialog::OnCancel(Listener value) {
    base->OnCancel(value);
    return this;
}
AlertDialog* AlertDialog::OnOk(Listener value) {
    base->OnOk(value);
    return this;
}
El* AlertDialog::IntoEl(WinSize size) {
    return base->IntoEl(size);
}

} // namespace component
} // namespace gpui
