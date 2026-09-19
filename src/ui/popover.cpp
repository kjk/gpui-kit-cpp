#include "ui/popover.h"
#include "base/actions.h"

namespace gpui {

namespace component {

El* PopoverSurface(Ctx* cx, El* e) {
    if (!e) {
        return e;
    }
    const Theme& th = ThemeNow(cx->app);
    // styled.rs popover_shadow: a 1px translucent ring spent as a shadow
    // layer, plus two blurred layers. No border — an opaque border would
    // composite over the fill instead of letting the shadow show through.
    Rgba ring = RgbaOpacity(th.foreground, 0.1f);
    Rgba ink = Rgba8(0, 0, 0, 26);
    BoxShadow shadows[3] = {
        {0, 0, 0, 1.f, ring, false},
        {0, 4.f, 3.f, -1.f, ink, false},
        {0, 2.f, 2.f, -2.f, ink, false},
    };
    return e->Bg(th.tokens.popover)
        ->Fg(th.popoverFg)
        ->Shadows(shadows, 3)
        ->Radius(th.radius);
}

El* DropdownOpen(Ctx* cx, El* surface, uint32_t key) {
    if (!surface) {
        return surface;
    }
    float t = MotionAppear(cx, key, kDropdownEnterMs, EaseOutCubic);
    if (t >= 1.f) {
        return surface;
    }
    // The fade and the slide are the same curve: `t` is already eased, so the
    // surface decelerates into place rather than arriving at a constant rate.
    surface->Opacity(t);
    surface->Top(kDropdownEnterOffset * (1.f - t));
    return surface;
}

El* DropdownPlaceContent(El* content, float gap) {
    if (!content) {
        return content;
    }
    // dropdown_positioner is the side strategy, not Popup's corner strategy:
    // it opens below with Start alignment, flips if needed, and keeps the
    // same 8 px viewport margin.
    content->AnchorBelow(gap)->Left(0)->Fixed()->Deferred()->AnchorFlip();
    content->style.anchorMargin = kPopupWindowMargin;
    return content;
}

Popover* Popover::New(Ctx* cx) {
    Arena* a = cx->a;
    Popover* p = ArenaNew<Popover>(a);
    p->a = a;
    p->cx = cx;
    return p;
}
Popover* Popover::Trigger(El* e) {
    trigger = e;
    return this;
}
Popover* Popover::Content(El* e) {
    content = e;
    return this;
}
Popover* Popover::New(Ctx* cx, Str id) {
    Popover* p = New(cx);
    p->id = id;
    return p;
}
Popover* Popover::Open(bool v) {
    controlled = true;
    open = v;
    return this;
}
Popover* Popover::DefaultOpen(bool v) {
    defaultOpen = v;
    return this;
}
Popover* Popover::OnClose(Listener fn) {
    onClose = fn;
    return this;
}
Popover* Popover::OverlayClosable(bool v) {
    overlayClosable = v;
    return this;
}
Popover* Popover::OnOpenChange(Listener fn) {
    onOpenChange = fn;
    return this;
}
Popover* Popover::Anchor(PopupAnchor v) {
    anchor = v;
    return this;
}
Popover* Popover::Button(MouseButton b) {
    button = b;
    return this;
}

// The keyed state behind one popover id — Rust's
// `window.use_keyed_state(self.id, |cx| PopoverState::new(default_open, cx))`.
static Entity<PopoverState> PopState(Ctx* cx, Str id) {
    return KeyedEntity<PopoverState>(cx, KeyedName(cx, id));
}

bool PopoverOpen(Ctx* cx, Str id) {
    return PopoverIsOpen(cx, PopState(cx, id));
}

El* Popover::IntoEl() {
    Str popId = id.s ? id : StrL("popover");
    Entity<PopoverState> st = PopState(cx, popId);
    PopoverState* s = st.Get(cx);
    // default_open only counts the first time this key is seen, the way it
    // only reaches PopoverState::new once.
    if (s && !s->seeded) {
        s->seeded = true;
        s->open = defaultOpen;
    }
    if (controlled) {
        PopoverSetOpen(cx, st, open);
    }
    bool isOpen = PopoverIsOpen(cx, st);
    El* root = gpui::Popover::New(cx, popId, st, button)
                   ->Anchor(anchor)
                   ->OverlayClosable(overlayClosable)
                   ->OnOpenChange(onOpenChange)
                   ->OnDismiss(onClose)
                   ->Trigger(trigger)
                   ->Content(isOpen ? content : nullptr)
                   ->IntoEl();
    // popover.rs binds escape to Cancel in the "Popover" context and closes
    // on it. A controlled popover's flag is the caller's, so it says what to
    // run; an uncontrolled one closes its own state.
    if (isOpen) {
        CancelBindKeys(cx, root, "Popover", popId,
                       ListenTo(st, &PopoverDismiss));
    }
    return root;
}

} // namespace component
} // namespace gpui
