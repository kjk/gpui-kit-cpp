#include "ui/clipboard.h"
#include "ui/button.h"

namespace gpui {

namespace component {

// How long the checkmark stays up after a copy.
static const int kCopiedMs = 2000;

ClipboardState::~ClipboardState() {
    StrFree(value);
}

void ClipboardState::OnReset(ClipboardState* self, Ctx* cx, const TickEvent*) {
    self->timer = 0;
    self->copied = false;
    Notify(cx);
}

void ClipboardState::OnCopy(ClipboardState* self, Ctx* cx, const ClickEvent*) {
    ClipboardSetText(cx->win, self->value);
    self->copied = true;
    Notify(cx);
    // A second copy inside the two seconds cannot happen — the button drops
    // its click while `copied` — so there is never a countdown to supersede.
    self->timer = WindowSetTimeout(cx->win, kCopiedMs,
                                   Listen(cx, &ClipboardState::OnReset));
    if (self->onCopied.IsValid()) {
        ClipboardEvent ev = {self->value};
        ListenerCall(cx->app, cx->win, self->onCopied, &ev);
    }
}

Clipboard* Clipboard::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Clipboard* c = ArenaNew<Clipboard>(a);
    c->a = a;
    c->cx = cx;
    c->id = id;
    return c;
}
Clipboard* Clipboard::Value(Str v) {
    value = v;
    return this;
}
Clipboard* Clipboard::Tooltip(Str t) {
    tooltipText = t;
    return this;
}
Clipboard* Clipboard::OnCopied(Listener fn) {
    onCopied = fn;
    return this;
}
Clipboard* Clipboard::WithSize(UiSize sizeValue) {
    size = sizeValue;
    return this;
}

El* Clipboard::IntoEl() {
    Entity<ClipboardState> st =
        KeyedEntity<ClipboardState>(cx, KeyedName(cx, id));
    ClipboardState* s = st.Get(cx);
    if (s) {
        // The value and the callback are the caller's every frame, the way
        // HoverCard's delays are.
        if (!base::StrEq(s->value, value)) {
            StrFree(s->value);
            s->value = StrDup(value);
        }
        s->onCopied = onCopied;
    }
    bool copied = s && s->copied;

    // A Clipboard is the ghost icon button and nothing else; `value` is what
    // it copies, not something it shows. The caller renders any label next to
    // it.
    Button* btn = Button::New(cx, id)
                      ->Icon(copied ? IconName::Check : IconName::Copy)
                      ->Ghost()
                      ->WithSize(size);
    if (tooltipText.s) {
        btn->Tooltip(tooltipText);
    }
    if (!copied) {
        btn->OnClick(ListenTo(st, &ClipboardState::OnCopy));
    }
    // clipboard.rs stops the click: a copy button is meant to sit inside
    // anything — a table row, a description list, a nav item — and copying
    // must not also be a press on whatever it was dropped into.
    return btn->IntoEl()->StopClick();
}

} // namespace component
} // namespace gpui
