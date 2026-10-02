#include "base/state_style.h"

namespace gpui {

StateStyle& StateStyle::Bg(Background c) {
    style.bg = c;
    style.hasBg = true;
    set |= StateFieldBg;
    return *this;
}
StateStyle& StateStyle::Fg(Rgba c) {
    style.color = c;
    style.hasColor = true;
    set |= StateFieldFg;
    return *this;
}
StateStyle& StateStyle::Border(float w, Rgba c) {
    style.border = w;
    style.borderColor = c;
    set |= StateFieldBorder;
    return *this;
}
StateStyle& StateStyle::BorderL(float w, Rgba c) {
    style.borderL = w;
    style.borderColor = c;
    set |= StateFieldBorderL;
    return *this;
}
StateStyle& StateStyle::BorderR(float w, Rgba c) {
    style.borderR = w;
    style.borderColor = c;
    set |= StateFieldBorderR;
    return *this;
}
StateStyle& StateStyle::BorderT(float w, Rgba c) {
    style.borderT = w;
    style.borderColor = c;
    set |= StateFieldBorderT;
    return *this;
}
StateStyle& StateStyle::BorderB(float w, Rgba c) {
    style.borderB = w;
    style.borderColor = c;
    set |= StateFieldBorderB;
    return *this;
}
StateStyle& StateStyle::Pad(float x, float y) {
    style.pad = Edges::New(x, x, y, y);
    set |= StateFieldPad;
    return *this;
}
StateStyle& StateStyle::Radius(float v) {
    style.radius = v;
    set |= StateFieldRadius;
    return *this;
}
StateStyle& StateStyle::HoverBg(Background c) {
    style.hoverBg = c;
    style.hasHoverBg = true;
    set |= StateFieldHoverBg;
    return *this;
}
StateStyle& StateStyle::HoverFg(Rgba c) {
    style.hoverFg = c;
    style.hasHoverFg = true;
    set |= StateFieldHoverFg;
    return *this;
}

StateStyle& StateStyle::ActiveBg(Background c) {
    style.activeBg = c;
    style.hasActiveBg = true;
    set |= StateFieldActiveBg;
    return *this;
}

StateStyle& StateStyle::Opacity(float v) {
    style.opacity = v;
    set |= StateFieldOpacity;
    return *this;
}

void StateStyleRefine(StateStyle* into, const StateStyle& over) {
    if (over.Has(StateFieldPad)) {
        into->style.pad = over.style.pad;
        into->set |= StateFieldPad;
    }
    if (over.Has(StateFieldBg)) {
        into->Bg(over.style.bg);
    }
    if (over.Has(StateFieldFg)) {
        into->Fg(over.style.color);
    }
    if (over.Has(StateFieldBorder)) {
        into->Border(over.style.border, over.style.borderColor);
    }
    if (over.Has(StateFieldBorderL)) {
        into->BorderL(over.style.borderL, over.style.borderColor);
    }
    if (over.Has(StateFieldBorderR)) {
        into->BorderR(over.style.borderR, over.style.borderColor);
    }
    if (over.Has(StateFieldBorderT)) {
        into->BorderT(over.style.borderT, over.style.borderColor);
    }
    if (over.Has(StateFieldBorderB)) {
        into->BorderB(over.style.borderB, over.style.borderColor);
    }
    if (over.Has(StateFieldRadius)) {
        into->Radius(over.style.radius);
    }
    if (over.Has(StateFieldHoverBg)) {
        into->HoverBg(over.style.hoverBg);
    }
    if (over.Has(StateFieldHoverFg)) {
        into->HoverFg(over.style.hoverFg);
    }
    if (over.Has(StateFieldActiveBg)) {
        into->ActiveBg(over.style.activeBg);
    }
    if (over.Has(StateFieldOpacity)) {
        into->Opacity(over.style.opacity);
    }
}

StateStyle StateStyleResolve(const StateStyle& instance,
                             const StateStyle* const* states, int n) {
    StateStyle out;
    StateStyleRefine(&out, instance);
    for (int i = 0; i < n; i++) {
        if (states[i]) {
            StateStyleRefine(&out, *states[i]);
        }
    }
    return out;
}

El* ElRefine(El* e, const StateStyle& s) {
    return e ? e->Refine(s.style, s.set) : e;
}

} // namespace gpui
