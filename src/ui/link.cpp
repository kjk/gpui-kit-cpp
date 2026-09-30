#include "ui/link.h"

namespace gpui {

namespace component {

Link* Link::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Link* l = ArenaNew<Link>(a);
    l->a = a;
    l->cx = cx;
    l->id = id;
    return l;
}

Link* Link::Href(Str s) {
    href = s;
    return this;
}
Link* Link::Text(Str s) {
    text = s;
    return this;
}
Link* Link::Disabled(bool v) {
    disabled = v;
    return this;
}
Link* Link::OnOpen(Listener fn) {
    onOpen = fn;
    return this;
}
Link* Link::Child(El* e) {
    if (e) children.Append(a, e);
    return this;
}

El* Link::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // gpui_base::Link owns identity, focus and activation; the href is this
    // layer's, which is where Rust's injected open strategy would read it.
    El* e = gpui::Link::New(cx, id, disabled, onOpen);
    // link.rs sets cursor_pointer on the div itself rather than inside the
    // `when(!disabled)` beside it, so a disabled link keeps the hand there
    // too; this keeps that.
    e->Cursor(CursorKind::Pointer);
    if (children.len > 0) {
        // The colour and underline go on the link itself, as link.rs sets
        // them, for the children to take.
        e->Fg(disabled ? th.mutedFg : th.link)->Underline();
        // GPUI's text decoration is part of the inherited text style, so a
        // text child of the link is underlined; an El's underline is its own,
        // so a direct text child is given it here.
        for (El* child : children) {
            if (child->kind == ElKind::Text) child->Underline();
            e->Child(child);
        }
        return e;
    }
    // text_decoration_1(): a link is underlined at rest, not only on hover.
    e->Child(TextEl(a, text.s ? text : href)
                 ->Font(14)
                 ->Underline()
                 ->Fg(disabled ? th.mutedFg : th.link));
    return e;
}

} // namespace component
} // namespace gpui
