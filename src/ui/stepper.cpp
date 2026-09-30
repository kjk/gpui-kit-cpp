#include "ui/stepper.h"

namespace gpui {

namespace component {

// StepperItem::render: the indicator's box.
static float StepperIconPx(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
            return 8;
        case UiSize::Small:
            return 18;
        case UiSize::Large:
            return 32;
        default:
            return 24;
    }
}

// StepperSeparator::render: how thick the connector is.
static float StepperSepPx(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
            return 1.5f;
        case UiSize::Large:
            return 3;
        default:
            return 2;
    }
}

// input_text_size(size.smaller()) on the trigger.
static float StepperTextPx(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
        case UiSize::Small:
            return 12;
        default:
            return 14;
    }
}

// The gap the connector leaves on each side of an indicator.
static const float kStepperGap = 4;

StepperItem* StepperItem::New(Ctx* cx) {
    Arena* a = cx->a;
    StepperItem* it = ArenaNew<StepperItem>(a);
    it->a = a;
    it->cx = cx;
    return it;
}
StepperItem* StepperItem::Icon(IconName v) {
    icon = v;
    return this;
}
StepperItem* StepperItem::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}
StepperItem* StepperItem::Disabled(bool v) {
    disabled = v;
    return this;
}

// StepperSeparator: absolutely placed, so the line runs from one indicator to
// the next whatever the content under them is as wide as.
static El* StepperSep(Arena* a, const Theme& th, UiSize size, Axis layout,
                      bool textCenter, bool checked) {
    float icon = StepperIconPx(size);
    float wide = StepperSepPx(size);
    El* sep = Div(a)->Absolute()->Bg(checked ? th.primary : th.border);
    if (layout == Axis::Horizontal) {
        sep->H(wide)->Top(icon * 0.5f);
        if (!textCenter) {
            sep->Left(icon + kStepperGap)->Right(kStepperGap);
        } else {
            // Centered steps put the indicator in the middle of the step, so
            // the line spans from this middle to the next one.
            sep->LeftRel(0.5f)->Left(icon * 0.5f + kStepperGap);
            sep->RightRel(-0.5f)->Right(icon * 0.5f + kStepperGap);
        }
    } else {
        sep->W(wide)->Left((icon - wide) * 0.5f);
        sep->Top(icon + kStepperGap)->Bottom(kStepperGap);
    }
    return sep;
}

// StepperTrigger: the indicator and the step's content, and the only part of
// a step that takes a click.
static El* StepperTrigger(Arena* a, const Theme& th, StepperItem* it) {
    float icon = StepperIconPx(it->size);
    float font = StepperTextPx(it->size);
    bool checked = it->step <= it->checkedStep;

    El* ind = Div(a)
                  ->W(icon)
                  ->H(icon)
                  ->Shrink0()
                  ->Radius(icon * 0.5f)
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->Bg(checked ? th.primary : th.secondary);
    if (!it->disabled && !checked) {
        ind->HoverBg(th.secondaryHover);
    }
    Rgba fg = checked ? th.primaryFg : th.secondaryFg;
    // An XSmall indicator is a dot: too small for a number or an icon.
    if (it->size != UiSize::XSmall) {
        if (it->icon != IconName::None) {
            ind->Child(IconEl(a, it->icon, icon * 0.6f)->Fg(fg));
        } else {
            ind->Child(TextEl(a, StrDup(a, fmt("%d", it->step + 1)))
                           ->Font(font)
                           ->LineHeight(1.f)
                           ->Fg(fg));
        }
    }

    El* trig = Div(a);
    if (it->layout == Axis::Horizontal) {
        trig->FlexCol()->Gap(4);
    } else {
        trig->FlexRow()->Gap(8);
    }
    trig->ItemsStart();
    if (it->textCenter) {
        trig->ItemsCenter();
    }
    trig->Font(font);
    trig->Child(ind);
    for (El* child : it->children) {
        trig->Child(child);
    }
    return trig;
}

El* StepperItem::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    El* root = Div(a)
                   ->Id(StrDup(a, fmt("stepper-item-%d", step)))
                   ->Role(AccessibilityRole::ListItem)
                   ->AriaPositionInSet(step + 1)
                   ->AriaDisabled(disabled);
    if (layout == Axis::Horizontal) {
        root->FlexRow();
    } else {
        root->FlexCol();
    }
    if (!isLast) {
        root->Flex1();
    }
    root->ItemsStart();
    if (textCenter) {
        root->Flex1()->JustifyCenter();
    }
    refiner.Apply(root);
    El* trig = StepperTrigger(a, th, this);
    if (!disabled) {
        BindClick(trig, StrDup(a, fmt("trigger-%d", step)),
                  ListenerFill(onClick, step));
    }
    root->Child(trig);
    if (!isLast) {
        // is_passed: the connector behind a step that is already done.
        root->Child(
            StepperSep(a, th, size, layout, textCenter, step < checkedStep));
    }
    return root;
}

Stepper* Stepper::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Stepper* s = ArenaNew<Stepper>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    return s;
}
Stepper* Stepper::Item(StepperItem* item) {
    if (item) {
        items.Append(a, item);
    }
    return this;
}
Stepper* Stepper::SelectedIndex(int i) {
    step = i;
    return this;
}
Stepper* Stepper::Layout(Axis v) {
    layout = v;
    return this;
}
Stepper* Stepper::Vertical() {
    layout = Axis::Vertical;
    return this;
}
Stepper* Stepper::ItemsCenter(bool v) {
    itemsCenter = v;
    return this;
}
Stepper* Stepper::TextCenter(bool v) {
    textCenter = v;
    return this;
}
Stepper* Stepper::Disabled(bool v) {
    disabled = v;
    return this;
}
Stepper* Stepper::WithSize(UiSize s) {
    size = s;
    return this;
}
Stepper* Stepper::W(float px) {
    width = px;
    return this;
}
Stepper* Stepper::OnClick(Listener fn) {
    onClick = fn;
    return this;
}

El* Stepper::IntoEl() {
    El* root = Div(a)->W(width);
    if (layout == Axis::Horizontal) {
        root->FlexRow();
    } else {
        root->FlexCol();
    }
    if (itemsCenter) {
        root->ItemsCenter();
    }
    for (int i = 0; i < items.len; i++) {
        StepperItem* it = items[i];
        it->step = i;
        it->checkedStep = step;
        it->layout = layout;
        it->size = size;
        it->textCenter = textCenter;
        it->isLast = i + 1 == items.len;
        // A stepper-level disabled turns every step off; a step can also say
        // so for itself, which Rust lets override the stepper's.
        it->disabled = it->disabled || disabled;
        it->onClick = onClick;
        root->Child(it->IntoEl());
    }
    return root;
}

} // namespace component
} // namespace gpui
