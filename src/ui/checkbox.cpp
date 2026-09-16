#include "ui/checkbox.h"
#include "base/motion.h"

namespace gpui {

namespace component {

// checkbox.rs no longer names a spring of its own: the tick's fade is the
// theme's `spring_control`, the policy every control that answers a click
// shares. Critically damped, because an opacity that overshoots would clip
// at 1 and come back — a flicker rather than a flourish.

Checkbox* Checkbox::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Checkbox* c = ArenaNew<Checkbox>(a);
    c->a = a;
    c->cx = cx;
    c->id = id;
    return c;
}

Checkbox* Checkbox::Label(Str s) {
    label = s;
    return this;
}
Checkbox* Checkbox::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}
Checkbox* Checkbox::Hint(Str s) {
    hint = s;
    return this;
}
Checkbox* Checkbox::Child(El* e) {
    child = e;
    return this;
}
Checkbox* Checkbox::Checked(bool v) {
    checked = v;
    return this;
}
Checkbox* Checkbox::Disabled(bool v) {
    disabled = v;
    return this;
}
Checkbox* Checkbox::WithSize(UiSize s) {
    size = s;
    return this;
}
Checkbox* Checkbox::W(float v) {
    w = v;
    return this;
}
Checkbox* Checkbox::FocusRing(bool v) {
    focusRing = v;
    return this;
}
Checkbox* Checkbox::Role(AccessibilityRole value) {
    accessibilityRole = value;
    return this;
}
Checkbox* Checkbox::TabIndex(int v) {
    tabIndex = v;
    return this;
}
Checkbox* Checkbox::TabStop(bool v) {
    tabStop = v;
    return this;
}
Checkbox* Checkbox::Tooltip(Str s) {
    tooltip = s;
    return this;
}
Checkbox* Checkbox::OnClick(Listener fn) {
    return OnChange(fn);
}
Checkbox* Checkbox::OnChange(Listener fn) {
    onClick = fn;
    return this;
}

El* Checkbox::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    float box = size == UiSize::Small ? 14.f : 16.f;
    // An unchecked box carries the input border, a checked one the primary
    // color, and a disabled one either at half strength.
    Rgba mark = checked ? th.primary : th.inputBorder;
    if (disabled) {
        mark = RgbaOpacity(mark, 0.5f);
    }
    float radius = th.radius < 4.f ? th.radius : 4.f;
    CheckboxState state =
        checked ? CheckboxState::Checked : CheckboxState::Unchecked;
    El* ind = CheckboxIndicator::New(cx, state, disabled)
                  ->W(box)
                  ->H(box)
                  ->Shrink0()
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->Border(1, mark)
                  ->Radius(radius);
    if (checked) {
        ind->Bg(mark);
    }
    // checkbox.rs fades the tick in over 0.25 s, and back out again when the
    // box is cleared: `this.opacity(if checked { delta } else { 1 - delta })`,
    // which one value going the other way says as well.
    float on = checked ? 1.f : 0.f;
    if (!disabled) {
        // spring_control: a box clicked twice reverses the fade mid-flight,
        // which is where a spring beats a curve restarted from the value it
        // happened to be at.
        on = SpringValue(cx, MotionId(id, StrL("checkbox-tick")), on,
                         th.motion.springControl);
    }
    if (on > 0.01f) {
        Rgba tick = disabled ? RgbaOpacity(th.primaryFg, 0.5f) : th.primaryFg;
        ind->Child(IconEl(a, IconName::Check, box - 4)->Fg(tick)->Opacity(on));
    }
    // gpui_base::Checkbox owns identity, focus and activation. It hands the
    // handler the state the activation produces; the themed checkbox is
    // boolean, and CheckboxState::Unchecked / Checked are 0 and 1, so what
    // the caller reads is the `!checked` Rust passes on.
    // h_flex().gap_2().items_start(): the box lines up with the *first* line
    // of the label, not with the middle of the block. Centring looks the same
    // on a one-line label and drops the box half a description lower on a
    // labelled one, which is what it was doing.
    El* row = gpui::Checkbox::New(cx, id, state, disabled, onClick)
                  ->Role(accessibilityRole)
                  ->TabIndex(tabIndex)
                  ->TabStop(tabStop)
                  ->FocusRing(focusRing)
                  ->FlexRow()
                  ->ItemsStart()
                  ->Gap(8);
    // The explicit name wins over the visible label, and only the name
    // changes: what is drawn stays the label.
    Str name = accessibilityLabel.s ? accessibilityLabel : label;
    if (name.s) {
        row->AriaLabel(name);
    }
    if (tooltip.s) {
        row->Tip(tooltip);
    }
    row->Child(ind);
    if (w > 0) {
        row->W(w);
    }
    if (label.s || hint.s || child) {
        // v_flex().line_height(relative(1.2)).gap_1(). Rust also puts
        // flex_1 on this column; here that would make every checkbox row
        // claim the whole width of whatever holds it, which lays a row of
        // them out as a column, so the label measures itself instead.
        El* col = Div(a)->FlexCol()->Gap(4);
        if (label.s) {
            // line_height(relative(1.)): the label's line box is exactly the
            // font size, so its first line is as tall as the 16px box beside
            // it and the two share a top edge.
            // text_xs / text_sm / text_base / text_lg, a step above the
            // generic control font — the same table component::Radio
            // spells out. This was UiFontPx, which is a step smaller.
            float fontPx = size == UiSize::XSmall  ? 12.f
                           : size == UiSize::Small ? 14.f
                           : size == UiSize::Large ? 18.f
                                                   : 16.f;
            col->Child(TextEl(a, label)
                           ->Font(fontPx)
                           ->LineHeight(1.f)
                           ->Fg(disabled ? th.mutedFg : th.foreground)
                           ->Wrap());
        }
        if (hint.s) {
            col->Child(TextEl(a, hint)
                           ->Font(12)
                           ->LineHeight(1.2f)
                           ->Fg(th.mutedFg)
                           ->Wrap());
        }
        if (child) {
            col->Child(child);
        }
        row->Child(col);
    }
    return row;
}

} // namespace component
} // namespace gpui
