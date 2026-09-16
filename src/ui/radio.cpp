#include "ui/radio.h"

namespace gpui {

namespace component {

Radio* Radio::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Radio* r = ArenaNew<Radio>(a);
    r->a = a;
    r->cx = cx;
    r->id = id;
    return r;
}

Radio* Radio::Label(Str s) {
    label = s;
    return this;
}
Radio* Radio::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}
Radio* Radio::Hint(Str s) {
    hint = s;
    return this;
}
Radio* Radio::Checked(bool v) {
    checked = v;
    return this;
}
Radio* Radio::Disabled(bool v) {
    disabled = v;
    return this;
}
Radio* Radio::WithSize(UiSize s) {
    size = s;
    return this;
}
Radio* Radio::FocusRing(bool v) {
    focusRing = v;
    return this;
}
Radio* Radio::TabIndex(int v) {
    tabIndex = v;
    return this;
}
Radio* Radio::TabStop(bool v) {
    tabStop = v;
    return this;
}
Radio* Radio::OnClick(Listener fn) {
    return OnChange(fn);
}
Radio* Radio::OnChange(Listener fn) {
    onClick = fn;
    return this;
}

El* Radio::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // A checked radio is a filled circle with the same check a checkbox
    // carries, not a dot: primary fill and border, white tick.
    float box = size == UiSize::Small    ? 14.f
                : size == UiSize::XSmall ? 12.f
                : size == UiSize::Large  ? 18.f
                                         : 16.f;
    Rgba border = checked ? th.primary : th.inputBorder;
    Rgba fill = checked ? th.primary : th.inputBg;
    if (disabled) {
        border = RgbaOpacity(border, 0.5f);
        fill = RgbaOpacity(fill, 0.5f);
    }
    El* dot = Div(a)
                  ->W(box)
                  ->H(box)
                  ->Radius(th.radiusFull)
                  ->Border(1, border)
                  ->Bg(fill)
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->Shrink0();
    if (checked) {
        Rgba tick = disabled ? RgbaOpacity(th.primaryFg, 0.5f) : th.primaryFg;
        dot->Child(IconEl(a, IconName::Check, box - 5)->Fg(tick));
    }
    // gpui_base::Radio owns identity, focus and activation, and refuses the
    // click to the option that is already picked.
    El* row = gpui::Radio::New(cx, id, checked, disabled, onClick)
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
    row->Child(dot);
    if (label.s || hint.s) {
        // line_height(relative(1.2)) on the column, 1. on the label.
        El* col = Div(a)->FlexCol()->Gap(4);
        if (label.s) {
            // text_xs / text_sm / text_base / text_lg, a step above the
            // generic control font.
            float fontPx = size == UiSize::XSmall  ? 12.f
                           : size == UiSize::Small ? 14.f
                           : size == UiSize::Large ? 18.f
                                                   : 16.f;
            col->Child(TextEl(a, label)->Font(fontPx)->LineHeight(1.f)->Fg(
                disabled ? th.mutedFg : th.foreground));
        }
        if (hint.s) {
            col->Child(TextEl(a, hint)
                           ->Font(12)
                           ->LineHeight(1.2f)
                           ->Fg(th.mutedFg)
                           ->Wrap());
        }
        row->Child(col);
    }
    return row;
}

static RadioGroup* RadioGroupNew(Ctx* cx, Str id, bool horizontal) {
    RadioGroup* g = ArenaNew<RadioGroup>(cx->a);
    g->a = cx->a;
    g->cx = cx;
    g->id = id;
    g->horizontal = horizontal;
    return g;
}
RadioGroup* RadioGroup::New(Ctx* cx, Str id) {
    return RadioGroupNew(cx, id, false);
}
RadioGroup* RadioGroup::Vertical(Ctx* cx, Str id) {
    return New(cx, id);
}
RadioGroup* RadioGroup::Horizontal(Ctx* cx, Str id) {
    return RadioGroupNew(cx, id, true);
}
RadioGroup* RadioGroup::Child(Radio* r) {
    if (r) {
        radios.Append(a, r);
    }
    return this;
}
RadioGroup* RadioGroup::Child(Str label) {
    return Child(Radio::New(cx, label)->Label(label));
}
RadioGroup* RadioGroup::Selected(int ix) {
    selected = ix;
    return this;
}
RadioGroup* RadioGroup::Disabled(bool v) {
    disabled = v;
    return this;
}
RadioGroup* RadioGroup::WithSize(UiSize s) {
    size = s;
    return this;
}
RadioGroup* RadioGroup::OnClick(Listener fn) {
    return OnChange(fn);
}
RadioGroup* RadioGroup::OnChange(Listener fn) {
    onClick = fn;
    return this;
}

El* RadioGroup::IntoEl() {
    // radio.rs puts the flex line inside the BaseRadioGroup rather than on
    // it: the caller's own style — a width, a justification — lands on the
    // group, and the radios stay packed at their gap_3 inside it.
    El* group = gpui::RadioGroup::New(
        cx, id, horizontal ? Axis::Horizontal : Axis::Vertical);
    El* base = Div(cx->a)->Gap(12);
    group->Child(base);
    if (horizontal) {
        base->FlexRow()->W(kFill)->FlexWrap();
    } else {
        base->FlexCol();
    }
    for (int i = 0; i < radios.len; i++) {
        Radio* r = radios[i];
        r->Checked(selected == i)->Disabled(disabled)->WithSize(size);
        if (onClick.IsValid()) {
            r->OnClick(ListenerArg(onClick, i));
        }
        base->Child(
            r->IntoEl()->AriaPositionInSet(i + 1)->AriaSizeOfSet(radios.len));
    }
    return group;
}

} // namespace component
} // namespace gpui
