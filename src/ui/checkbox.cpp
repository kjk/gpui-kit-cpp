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
Checkbox* Checkbox::TooltipShowDelay(int ms) {
    tooltipShowDelayMs = ms;
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
    // indicator_size: rems(0.75 / 0.875 / 1.125 / 1).
    float box = Rems(cx, size == UiSize::XSmall  ? 0.75f
                         : size == UiSize::Small ? 0.875f
                         : size == UiSize::Large ? 1.125f
                                                 : 1.f);
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
        // size_2 / size_2p5 / size_3 / size_3p5: a quarter rem under the box.
        ind->Child(IconEl(a, IconName::Check, box - Rems(cx, 0.25f))
                       ->Fg(tick)
                       ->Opacity(on));
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
                  ->Gap(Rems(cx, 0.5f));
    // The explicit name wins over the visible label, and only the name
    // changes: what is drawn stays the label.
    Str name = accessibilityLabel.s ? accessibilityLabel : label;
    if (name.s) {
        row->AriaLabel(name);
    }
    if (tooltip.s) {
        row->Tip(tooltip);
        if (tooltipShowDelayMs >= 0) {
            row->TipShowDelay(tooltipShowDelayMs);
        }
    }
    row->Child(ind);
    if (w > 0) {
        row->W(w);
    }
    if (label.s || hint.s || child) {
        ind->MarginT(box * 0.125f);
        // v_flex().line_height(relative(1.25)).gap_1(). Rust also puts
        // flex_1 on this column; here that would make every checkbox row
        // claim the whole width of whatever holds it, which lays a row of
        // them out as a column, so the label measures itself instead.
        El* col = Div(a)->FlexCol()->Gap(Rems(cx, 0.25f))->LineHeight(1.25f);
        if (label.s) {
            // input_text_size: the Input/Select ladder. A custom Size sets
            // no size and inherits.
            El* text = TextEl(a, label)
                           ->Fg(disabled ? th.mutedFg : th.foreground)
                           ->Wrap();
            if (size != UiSize::Size) {
                UiInputTextSize(text, size);
            }
            col->Child(text);
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
