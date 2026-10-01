#include "base/otp_input.h"

namespace gpui {

static void OtpEmit(OtpState* s, App* app, Window* win, OtpEventKind kind) {
    if (!s || !s->self.IsValid()) {
        return;
    }
    OtpEvent ev = {kind};
    EntityEmit(app, win, s->self, &ev);
    if (s->onChange.IsValid()) ListenerCall(app, win, s->onChange, &ev);
}

char OtpDigitChar(uint32_t c) {
    if (c >= '0' && c <= '9') {
        return (char)c;
    }
    // U+FF10..U+FF19, the full-width digits. Rust subtracts '０' and asks
    // char::from_digit for the plain one.
    if (c >= 0xFF10 && c <= 0xFF19) {
        return (char)('0' + (c - 0xFF10));
    }
    return 0;
}

Str OtpValue(const OtpState* s) {
    if (!s || s->len <= 0 || !s->value.els) {
        return StrL("");
    }
    return Str(s->value.els, s->len);
}

// `len` bytes and the NUL after them.
static void OtpTerminate(OtpState* s) {
    VecResize(s->value, s->len);
    VecAppend(s->value, (char)0);
}

void OtpSetValue(OtpState* s, Str value) {
    if (!s) {
        return;
    }
    VecClear(s->value);
    int n = len(value);
    if (n > 0) {
        VecAppendN(s->value, value.s, n);
    }
    s->len = n > 0 ? n : 0;
    OtpTerminate(s);
}

bool OtpEditValue(OtpState* s, int key, uint32_t ch) {
    if (key == KeyBack) {
        if (s->len == 0) {
            return false;
        }
        s->len--;
        OtpTerminate(s);
        return true;
    }
    // Rust reads the keystroke's own name first — a digit key is called "4" —
    // and falls back to the character it produced. The Key* code for a digit
    // is that ASCII digit, so the same two tries in the same order.
    char digit = OtpDigitChar((uint32_t)key);
    if (!digit) {
        digit = OtpDigitChar(ch);
    }
    if (!digit) {
        return false;
    }
    // Rust returns None once the code is full, so a further digit is dropped
    // rather than shifting the run.
    if (s->len >= s->length) {
        return false;
    }
    VecResize(s->value, s->len);
    VecAppend(s->value, digit);
    s->len++;
    OtpTerminate(s);
    return true;
}

void OtpFocus(OtpState* s, App* app, Window* win) {
    if (s->disabled || s->focused) {
        return;
    }
    s->focused = true;
    BlinkStart(app, win, &s->blink);
    OtpEmit(s, app, win, OtpEventKind::Focus);
}

void OtpBlur(OtpState* s, App* app, Window* win) {
    if (!s->focused) {
        return;
    }
    s->focused = false;
    BlinkStop(app, win, &s->blink);
    OtpEmit(s, app, win, OtpEventKind::Blur);
}

bool OtpCursorVisible(OtpState* s, App* app) {
    return s->focused && BlinkVisible(app, s->blink);
}

El* OtpInput::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    El* e = Div(a);
    if (id.s) {
        // otp_input.rs focuses its handle from the row's own click.
        e->PathId(id)->FocusOnPress();
    }
    return e;
}

void OtpKeyDown(OtpState* self, Ctx* cx, const KeyEvent* ev) {
    if (!self || self->disabled || !self->focused) {
        return;
    }
    if (!OtpEditValue(self, ev->vk, ev->ch)) {
        return;
    }
    // The keystroke was this field's; nothing above it hears it. The caret
    // starts over from full, the way typing into a text field does.
    const_cast<KeyEvent*>(ev)->propagate = false;
    BlinkPause(cx->app, cx->win, &self->blink);
    OtpEmit(self, cx->app, cx->win, OtpEventKind::Change);
    if (self->len == self->length) {
        OtpEmit(self, cx->app, cx->win, OtpEventKind::Complete);
    }
    Notify(cx);
}

void OtpClick(OtpState* self, Ctx* cx, const ClickEvent*) {
    if (!self || self->disabled || self->focused) {
        return;
    }
    OtpFocus(self, cx->app, cx->win);
    Notify(cx);
}

El* OtpInput::New(Ctx* cx, Str id, Entity<OtpState> state) {
    El* e = New(cx, id);
    OtpState* s = state.Get(cx);
    if (s) {
        s->self = state;
    }
    if (!s || !id.s) {
        return e;
    }
    if (!s->focus.IsValid()) {
        s->focus = FocusHandleNew(cx);
    }
    e->TrackFocus(s->focus);
    // focused follows the window, so a click anywhere else blurs the field
    // without every page having to say so.
    bool has = FocusHandleIsFocused(cx->win, s->focus);
    if (has != s->focused) {
        if (has) {
            OtpFocus(s, cx->app, cx->win);
        } else {
            OtpBlur(s, cx->app, cx->win);
        }
    }
    e->OnClick(ListenTo(state, &OtpClick));
    e->OnKeyDown(ListenTo(state, &OtpKeyDown));
    return e;
}
} // namespace gpui
