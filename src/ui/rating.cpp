#include "ui/rating.h"

namespace gpui {

namespace component {

void RatingState::OnStarHover(RatingState* self, Ctx* cx, const HoverEvent* ev,
                              intptr_t ix) {
    // Rust sets this from each star's on_mouse_move and clears it from the
    // row's on_hover. A hover here is reported as a leave on the star being
    // left before the enter on the one being reached, so the star can do
    // both ends itself.
    int v = ev->hovered ? (int)ix : 0;
    if (self->hoveredValue == v) {
        return;
    }
    self->hoveredValue = v;
    Notify(cx);
}

void RatingState::OnStarClick(RatingState* self, Ctx* cx, const ClickEvent*,
                              intptr_t ix) {
    // Clicking the star the rating already reaches gives up that star, which
    // is the only way to get back to none.
    int v = self->value >= (int)ix ? (int)ix - 1 : (int)ix;
    self->value = v;
    Notify(cx);
    if (self->onClick.IsValid()) {
        ClickEvent ev = {};
        ListenerCall(cx->app, cx->win, ListenerFill(self->onClick, v), &ev);
    }
}

Rating* Rating::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Rating* r = ArenaNew<Rating>(a);
    r->a = a;
    r->cx = cx;
    r->id = id;
    return r;
}
Rating* Rating::Value(int v) {
    value = v;
    if (value > max) {
        value = max;
    }
    return this;
}
Rating* Rating::Max(int v) {
    max = v;
    if (value > max) {
        value = max;
    }
    return this;
}
Rating* Rating::Disabled(bool v) {
    disabled = v;
    return this;
}
Rating* Rating::Color(Rgba c) {
    color = c;
    hasColor = true;
    return this;
}
Rating* Rating::WithSize(UiSize s) {
    size = s;
    return this;
}
Rating* Rating::OnClick(Listener fn) {
    onClick = fn;
    return this;
}

El* Rating::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Entity<RatingState> st = KeyedEntity<RatingState>(cx, KeyedName(cx, id));
    RatingState* s = st.Get(cx);
    int shown = value;
    if (s) {
        // The caller's value wins whenever it moves, which is how two
        // ratings over one field stay in step.
        if (s->defaultValue != value) {
            s->defaultValue = value;
            s->value = value;
        }
        s->onClick = onClick;
        shown = s->value;
    }
    int hovered = s ? s->hoveredValue : 0;
    Rgba activeC = hasColor ? color : th.yellow;

    El* row = Div(a)->Id(id)->FlexRow()->ItemsCenter();
    for (int i = 1; i <= max; i++) {
        bool filled = i <= shown;
        // A hovered star and everything before it previews the value the
        // pointer is offering, without committing to it.
        bool lit = filled || hovered >= i;
        El* star =
            Div(a)
                ->Pad(Rems(cx, 0.125f))
                ->Shrink0()
                ->Child(IconEl(a, filled ? IconName::StarFill : IconName::Star,
                               UiIconPx(cx, size))
                            ->Fg(lit ? activeC : th.foreground));
        if (!disabled) {
            // `div().id(ix)`: the star is named by its number alone, which
            // the row's own id scopes.
            BindPathClick(star, StrDup(a, fmt("%d", i)),
                          ListenTo(st, &RatingState::OnStarClick, i));
            star->OnHover(ListenTo(st, &RatingState::OnStarHover, i));
        }
        row->Child(star);
    }
    return row;
}

} // namespace component
} // namespace gpui
