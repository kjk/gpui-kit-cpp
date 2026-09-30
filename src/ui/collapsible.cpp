#include "ui/collapsible.h"

namespace gpui {

namespace component {

Collapsible* Collapsible::New(Ctx* cx) {
    Arena* a = cx->a;
    Collapsible* c = ArenaNew<Collapsible>(a);
    c->a = a;
    c->cx = cx;
    return c;
}

Collapsible* Collapsible::W(float v) {
    width = v;
    return this;
}
Collapsible* Collapsible::Gap(float v) {
    gap = v;
    return this;
}
Collapsible* Collapsible::Open(bool v) {
    open = v;
    return this;
}
Collapsible* Collapsible::MotionId(Str id) {
    motionId = id;
    hasMotion = true;
    return this;
}
Collapsible* Collapsible::Trigger(El* e) {
    trigger = e;
    return this;
}
Collapsible* Collapsible::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}
Collapsible* Collapsible::Content(El* e) {
    content = e;
    return this;
}

El* Collapsible::IntoEl() {
    // The unstyled root has no direction of its own, as Rust's plain div()
    // does not; a collapsible stacks its trigger over its content.
    gpui::Collapsible* base =
        gpui::Collapsible::New(cx)->FlexCol()->Open(open)->Child(trigger);
    for (El* child : children) {
        base->Child(child);
    }
    if (hasMotion) {
        // spring_control, the policy every control that answers a click
        // shares: a trigger clicked twice reverses the reveal from where it
        // had got to rather than restarting it.
        float progress = SpringValue(
            cx, gpui::MotionId(motionId, StrL("reveal")), open ? 1.f : 0.f,
            ThemeNow(cx->app).motion.springControl);
        base->Reveal(motionId, progress);
    }
    El* e = base->Content(content)->IntoEl();
    if (width != 0) {
        e->W(width);
    }
    if (gap != 0) {
        e->Gap(gap);
    }
    return e;
}

} // namespace component
} // namespace gpui
