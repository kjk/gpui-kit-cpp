#include "ui/accordion.h"
#include "base/motion.h"

namespace gpui {

namespace component {

// accordion.rs names no spring of its own now: the panel expands under the
// theme's `spring_control`. Critically damped, so the panel height never
// overshoots the measured content height.

// AccordionItem::render's text_size: rems(0.75) / rems(0.875) / rems(1.).
// Not UiFontPx — the accordion runs its own scale, and Small shares Medium's.
static float AccordionFontPx(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
            return 12;
        case UiSize::Large:
            return 16;
        default:
            return 14;
    }
}

// The trigger's py_1/px_1p5 … py_3/px_4 ladder. The panel uses the same x and
// the same number for its pb, so one table serves both.
static void AccordionPad(UiSize s, float* padY, float* padX) {
    switch (s) {
        case UiSize::XSmall:
            *padY = 4;
            *padX = 6;
            return;
        case UiSize::Small:
            *padY = 6;
            *padX = 8;
            return;
        case UiSize::Large:
            *padY = 12;
            *padX = 16;
            return;
        default:
            *padY = 8;
            *padX = 12;
            return;
    }
}

// The gap between the icon and the title: gap_1 while small, gap_2 above.
static float AccordionTitleGap(UiSize s) {
    return (s == UiSize::XSmall || s == UiSize::Small) ? 4.f : 8.f;
}

// StyleRefinement::refine over the fields AccordionStyle names.
static El* AccordionRefine(El* e, const AccordionStyle& s) {
    if (s.padT >= 0) {
        e->PadT(s.padT);
    }
    if (s.padB >= 0) {
        e->PadB(s.padB);
    }
    if (s.padL >= 0) {
        e->PadL(s.padL);
    }
    if (s.padR >= 0) {
        e->PadR(s.padR);
    }
    if (s.fg.a != 0) {
        e->Fg(s.fg);
    }
    return e;
}

AccordionItem* AccordionItem::New(Ctx* cx) {
    AccordionItem* it = ArenaNew<AccordionItem>(cx->a);
    it->cx = cx;
    return it;
}

AccordionItem* AccordionItem::Title(El* t) {
    title = t;
    return this;
}

AccordionItem* AccordionItem::Title(Str s) {
    title = TextEl(cx->a, s);
    return this;
}

AccordionItem* AccordionItem::Icon(IconName i) {
    icon = i;
    return this;
}

AccordionItem* AccordionItem::Open(bool v) {
    open = v;
    return this;
}

AccordionItem* AccordionItem::Disabled(bool v) {
    disabled = v;
    return this;
}

AccordionItem* AccordionItem::Child(El* c) {
    if (c) {
        children.Append(cx->a, c);
    }
    return this;
}

AccordionItem* AccordionItem::Child(Str s) {
    return Child(TextEl(cx->a, s)->Wrap());
}

AccordionItem* AccordionItem::WithSize(UiSize s) {
    size = s;
    return this;
}

AccordionItem* AccordionItem::TitleStyle(const AccordionStyle& s) {
    titleStyle = s;
    return this;
}

AccordionItem* AccordionItem::ContentStyle(const AccordionStyle& s) {
    contentStyle = s;
    return this;
}

Accordion* Accordion::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Accordion* acc = ArenaNew<Accordion>(a);
    acc->a = a;
    acc->cx = cx;
    acc->id = id;
    return acc;
}

Accordion* Accordion::Multiple(bool v) {
    multiple = v;
    return this;
}
Accordion* Accordion::Bordered(bool v) {
    bordered = v;
    return this;
}
Accordion* Accordion::Disabled(bool v) {
    disabled = v;
    return this;
}
Accordion* Accordion::WithSize(UiSize s) {
    size = s;
    return this;
}
Accordion* Accordion::Item(AccordionItem* it) {
    if (it) {
        items.Append(a, it);
    }
    return this;
}
Accordion* Accordion::OnToggle(Listener fn) {
    onToggle = fn;
    return this;
}

El* AccordionItem::IntoEl() {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // The item's own name, on the stack while the item is built: the two
    // transitions below are named among the item's parts rather than
    // spelling the group and the row out again.
    IdScope scope(cx, StrDup(a, fmt("%d", index)));
    float font = AccordionFontPx(size);
    float padY = 0, padX = 0;
    AccordionPad(size, &padY, &padX);
    El* trig = AccordionTrigger::New(
        cx, ElementIdNamed(a, StrL("trigger"), (uint64_t)index), open, disabled,
        onToggle);
    // AccordionTrigger: h_flex justify_between gap_3 font_medium, and the
    // open one paints its title in foreground.
    trig->FlexRow()
        ->ItemsCenter()
        ->JustifyBetween()
        ->Gap(12)
        ->PadX(padX)
        ->PadY(padY)
        ->W(kFill)
        ->Medium();
    if (open) {
        trig->Fg(th.foreground);
    }
    AccordionRefine(trig, titleStyle);
    // flex_1 min_w_0: the title column gives before the chevron does.
    El* left = Div(a)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->Gap(AccordionTitleGap(size))
                   ->Flex1()
                   ->MinW(0);
    if (icon != IconName::None) {
        left->Child(IconEl(a, icon, UiIconPx(size)));
    }
    if (title) {
        left->Child(title);
    }
    trig->Child(left);
    // A disabled item has no chevron at all — Rust skips the whole
    // `when(!disabled)` block, the change handler with it.
    if (!disabled) {
        trig->Child(IconEl(a, IconName::ChevronDown, UiIconPx(UiSize::XSmall))
                        ->Shrink0()
                        ->Fg(th.mutedFg)
                        ->Rotate(open ? 0.5f : 0.f));
    }
    gpui::AccordionItem* it = gpui::AccordionItem::New(cx)->Open(open)->Header(
        gpui::AccordionHeader::New(cx, trig));
    El* panel = gpui::AccordionPanel::New(cx);
    panel->PadX(padX)->PadT(0)->PadB(padY);
    AccordionRefine(panel, contentStyle);
    for (El* child : children) {
        panel->Child(child);
    }
    // MotionReveal: the panel's height is its natural one times the
    // progress, and the box around it clips what does not fit. Upstream
    // had this as AnimatedAccordionPanel here and now takes the Base
    // element, which is the same measure-and-clip written once.
    // spring_control: a header clicked twice retargets the panel while
    // it is still opening, and the spring decelerates into the reversal
    // instead of snapping to a new curve's opening pace.
    float progress = SpringValue(cx, MotionName(cx, StrL("accordion")),
                                 open ? 1.f : 0.f, th.motion.springControl);
    // Mounted while the reveal moves; a settled closed panel unmounts, so
    // its content costs no layout or paint. A settled spring returns
    // exactly 0, and `!=` keeps a bouncy theme spring mounted while it
    // dips below 0. Reduced motion reopens straight to full progress,
    // which needs the reveal's height from a mounted frame.
    // Base's panel renders while open whatever keep_mounted says.
    if (open || progress != 0.f || MotionReduced()) {
        it->KeepMounted(true)
            ->Panel(MotionReveal::New(cx, StrL("content"), progress, panel));
    }
    // The separator belongs to the item, so it lands under the panel and
    // not between the trigger and its body.
    El* itEl = it->IntoEl()->Font(font);
    if (!last) {
        itEl->BorderB(1, th.border);
    }
    // refine_style(&self.style), last, over the item's own look. Rust wraps
    // the item in a `div().flex_1()`; the group here stacks the items
    // directly, as it did before items could render on their own.
    refiner.Apply(itEl);
    return itEl;
}

El* Accordion::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // Items paint tokens.accordion (= background); bordered turns the group
    // into one rounded card instead of a stack of separators.
    El* root = gpui::Accordion::New(cx, id)->FlexCol()->W(kFill)->Bg(
        th.tokens.background);
    if (bordered) {
        root->Border(1, th.border)->Radius(th.radiusLg)->ClipY();
    }
    // The group's name is on the stack while its items are built, so an
    // item is `0`, `1`, `2` inside it.
    IdScope group(cx, id);
    for (int i = 0; i < items.len; i++) {
        AccordionItem* item = items[i];
        item->index = i;
        item->last = i + 1 == items.len;
        item->size = size;
        // Either the group or the item disables it: an enabled group leaves an
        // item's own disabled flag in force.
        item->disabled = disabled || item->disabled;
        item->onToggle = ListenerArg(onToggle, i);
        root->Child(item->IntoEl());
    }
    return root;
}

} // namespace component
} // namespace gpui
