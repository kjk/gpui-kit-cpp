#include "ui/checkbox.h"
#include "base/layout_direction.h"
#include "base/motion.h"

namespace gpui {

namespace component {

// checkbox.rs no longer names a spring of its own: the tick's fade is the
// theme's `spring_control`, the policy every control that answers a click
// shares. Critically damped, because an opacity that overshoots would clip
// at 1 and come back — a flicker rather than a flourish.

Checkbox* Checkbox::Indeterminate(bool value) {
    indeterminate = value;
    return this;
}

Checkbox* Checkbox::Child(El* e) {
    child = e;
    return this;
}
Checkbox* Checkbox::W(float v) {
    w = v;
    return this;
}
Checkbox* Checkbox::Role(AccessibilityRole value) {
    accessibilityRole = value;
    return this;
}
El* ChoiceRow(Ctx* cx, El* row, const ChoiceFields& f) {
    row->TabIndex(f.tabIndex)
        ->TabStop(f.tabStop && !f.disabled)
        ->FocusRing(f.focusRing)
        ->FlexRowDir(cx->app)
        ->ItemsStart()
        ->Gap(Rems(cx, 0.5f));
    Str name = f.accessibilityLabel.s ? f.accessibilityLabel : f.label;
    if (name.s) {
        row->AriaLabel(name);
    }
    if (f.tooltip.s) {
        row->Tip(f.tooltip);
        if (f.tooltipShowDelayMs >= 0) {
            row->TipShowDelay(f.tooltipShowDelayMs);
        }
    }
    return row;
}

El* ChoiceLabelColumn(Ctx* cx, El* indicator, float box, Str label, Str hint,
                      UiSize size, bool disabled, bool wrapLabel) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    indicator->MarginT(box * 0.125f);
    El* col = Div(a)->FlexCol()->Gap(Rems(cx, 0.25f))->LineHeight(1.25f);
    if (label.s) {
        // input_text_size: the Input/Select ladder. A custom Size sets no
        // size and inherits.
        El* text = TextEl(a, label)->Fg(disabled ? th.mutedFg : th.foreground);
        if (wrapLabel) {
            text->Wrap();
        }
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
    return col;
}

struct ChoiceMarkState {
    IconName last = IconName::Check;
};

El* ChoiceMark(Ctx* cx, Str id, IconName mark, bool disabled, float size) {
    const Theme& theme = ThemeNow(cx->app);
    float opacity = mark == IconName::None ? 0.f : 1.f;
    IconName drawn = mark;
    if (cx->win) {
        auto* state = FrameKeyedState<ChoiceMarkState>(
            cx, KeyedName(cx, fmt("choice-last-mark-%s", id)));
        if (mark != IconName::None) state->last = mark;
        drawn = state->last;
        if (!disabled) {
            opacity = SpringValue(cx, MotionId(id, StrL("checkbox-tick")),
                                  opacity, theme.motion.springControl);
        }
    }
    if (opacity <= 0.01f) return nullptr;
    Rgba color =
        disabled ? RgbaOpacity(theme.primaryFg, 0.5f) : theme.primaryFg;
    return IconEl(cx->a, drawn, size)->Fg(color)->Opacity(opacity);
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
    CheckboxState state = indeterminate ? CheckboxState::Indeterminate
                          : checked     ? CheckboxState::Checked
                                        : CheckboxState::Unchecked;
    bool filled = state != CheckboxState::Unchecked;
    Rgba mark = filled ? th.primary : th.inputBorder;
    if (disabled) {
        mark = RgbaOpacity(mark, 0.5f);
    }
    float radius = th.radius < 4.f ? th.radius : 4.f;
    El* ind = CheckboxIndicator::New(cx, state, disabled)
                  ->W(box)
                  ->H(box)
                  ->Shrink0()
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->Border(1, mark)
                  ->Radius(radius);
    if (filled) ind->Bg(mark);
    IconName glyph = indeterminate ? IconName::Minus
                     : checked     ? IconName::Check
                                   : IconName::None;
    if (El* icon = ChoiceMark(cx, id, glyph, disabled, box - Rems(cx, 0.25f))) {
        ind->Child(icon);
    }
    // Base returns Checked for a mixed activation and the opposite value
    // for a regular one. Both map directly to the boolean listener payload.
    // h_flex().gap_2().items_start(): the box lines up with the *first* line
    // of the label, not with the middle of the block. Centring looks the same
    // on a one-line label and drops the box half a description lower on a
    // labelled one, which is what it was doing.
    El* row = ChoiceRow(cx,
                        gpui::Checkbox::New(cx, id, state, disabled, onClick)
                            ->Role(accessibilityRole),
                        *this);
    row->Child(ind);
    if (w > 0) {
        row->W(w);
    }
    if (label.s || hint.s || child) {
        // Rust also puts flex_1 on this column; here that would make every
        // checkbox row claim the whole width of whatever holds it, which lays
        // a row of them out as a column, so the label measures itself
        // instead.
        El* col =
            ChoiceLabelColumn(cx, ind, box, label, hint, size, disabled, true);
        if (child) {
            col->Child(child);
        }
        row->Child(col);
    }
    return row;
}

} // namespace component
} // namespace gpui
