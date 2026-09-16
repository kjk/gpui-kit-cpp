#include "ui/i18n.h"
#include "ui/tab.h"
#include "ui/styled.h"
#include "base/motion.h"

namespace gpui {

namespace component {

// tab_bar.rs names no spring of its own now: the indicator slides under the
// theme's `spring_move`. Rust keeps the box it is coming from in an
// epoch-stamped state; a spring here remembers where it had got to and how
// fast it was going, so what the bar has to keep is only the box it is going
// to.

float TabHeight(TabVariant v, UiSize size) {
    bool under = v == TabVariant::Underline;
    switch (size) {
        case UiSize::XSmall:
            return under ? 26.f : 20.f;
        case UiSize::Small:
            return under ? 30.f : 24.f;
        case UiSize::Large:
            return under ? 44.f : 36.f;
        default:
            return under ? 36.f : 32.f;
    }
}

float TabInnerHeight(TabVariant v, UiSize size) {
    bool boxed = v == TabVariant::Tab || v == TabVariant::Outline ||
                 v == TabVariant::Pill;
    switch (size) {
        case UiSize::XSmall:
            return boxed ? 18.f : v == TabVariant::Segmented ? 16.f : 20.f;
        case UiSize::Small:
            return boxed ? 22.f : v == TabVariant::Segmented ? 18.f : 22.f;
        case UiSize::Large:
            return boxed ? 36.f : v == TabVariant::Segmented ? 28.f : 32.f;
        default:
            switch (v) {
                case TabVariant::Tab:
                    return 30.f;
                case TabVariant::Outline:
                case TabVariant::Pill:
                    return 26.f;
                case TabVariant::Segmented:
                    return 24.f;
                default:
                    return 26.f;
            }
    }
}

float TabPadX(TabVariant v, UiSize size) {
    // Underline has no padding of its own; the bar's gap does that job.
    if (v == TabVariant::Underline) {
        return 0;
    }
    switch (size) {
        case UiSize::XSmall:
            return 8.f;
        case UiSize::Small:
            return 10.f;
        case UiSize::Large:
            return 16.f;
        default:
            return 12.f;
    }
}

float TabMarginTop(TabVariant v, UiSize size) {
    if (v != TabVariant::Underline) {
        return 0;
    }
    switch (size) {
        case UiSize::XSmall:
            return 1.f;
        case UiSize::Small:
            return 2.f;
        case UiSize::Large:
            return 5.f;
        default:
            return 3.f;
    }
}

float TabMarginBottom(TabVariant v, UiSize size) {
    if (v != TabVariant::Underline) {
        return 0;
    }
    return TabMarginTop(v, size) + 1.f;
}

float TabBarGap(TabVariant v, UiSize size) {
    switch (v) {
        case TabVariant::Tab:
            return 0;
        case TabVariant::Pill:
            return 4.f;
        case TabVariant::Segmented:
            return 2.f;
        case TabVariant::Underline:
            // The same as the tab's own padding would have been, which is
            // what Rust says where it writes this out.
            switch (size) {
                case UiSize::XSmall:
                    return 10.f;
                case UiSize::Small:
                    return 12.f;
                case UiSize::Large:
                    return 20.f;
                default:
                    return 16.f;
            }
        default:
            // Outline takes the default gap.
            switch (size) {
                case UiSize::XSmall:
                case UiSize::Small:
                    return 8.f;
                case UiSize::Large:
                    return 16.f;
                default:
                    return 12.f;
            }
    }
}

float TabBarPadX(TabVariant v, UiSize size) {
    if (v != TabVariant::Segmented) {
        return 0;
    }
    switch (size) {
        case UiSize::XSmall:
            return 2.f;
        case UiSize::Small:
            return 3.f;
        default:
            return 4.f;
    }
}

float TabBarRadius(TabVariant v, UiSize size, float radius, float radiusLg) {
    if (v != TabVariant::Segmented) {
        return 0;
    }
    return (size == UiSize::XSmall || size == UiSize::Small) ? radius
                                                             : radiusLg;
}

float TabRadius(TabVariant v, UiSize size, float radius, float radiusLg) {
    if (v == TabVariant::Outline || v == TabVariant::Pill) {
        // Fully round: Rust asks for 99px and lets the height cap it.
        return 99.f;
    }
    return TabBarRadius(v, size, radius, radiusLg);
}

float TabInnerRadius(TabVariant v, UiSize size, float radius, float radiusLg) {
    if (v != TabVariant::Segmented) {
        return 0;
    }
    float outer = TabBarRadius(v, size, radius, radiusLg);
    float inset = size == UiSize::Large ? 3.f : 2.f;
    float r = outer - inset;
    return r > 0 ? r : 0;
}

Tab* Tab::New(Ctx* cx) {
    return New(cx, {});
}
Tab* Tab::New(Ctx* cx, Str label) {
    Tab* tab = ArenaNew<Tab>(cx->a);
    tab->a = cx->a;
    tab->cx = cx;
    tab->label = label;
    return tab;
}
Tab* Tab::Label(Str value) {
    label = value;
    return this;
}
Tab* Tab::AriaLabel(Str value) {
    ariaLabel = value;
    return this;
}
Tab* Tab::Icon(IconName value) {
    icon = value;
    return this;
}
Tab* Tab::Prefix(El* value) {
    prefix = value;
    return this;
}
Tab* Tab::Suffix(El* value) {
    suffix = value;
    return this;
}
Tab* Tab::Child(El* value) {
    if (value) {
        children.Append(a, value);
    }
    return this;
}
Tab* Tab::Disabled(bool value) {
    disabled = value;
    return this;
}
Tab* Tab::Selected(bool value) {
    selected = value;
    return this;
}
Tab* Tab::OnClick(Listener value) {
    onClick = value;
    return this;
}
Tab* Tab::WithVariant(TabVariant value) {
    variant = value;
    return this;
}
Tab* Tab::Outline() {
    return WithVariant(TabVariant::Outline);
}
Tab* Tab::Pill() {
    return WithVariant(TabVariant::Pill);
}
Tab* Tab::Segmented() {
    return WithVariant(TabVariant::Segmented);
}
Tab* Tab::Underline() {
    return WithVariant(TabVariant::Underline);
}
Tab* Tab::WithSize(UiSize value) {
    size = value;
    return this;
}
Tab* Tab::Flex1() {
    flex1 = true;
    return this;
}
Tab* Tab::MaxWidth(float value) {
    maxWidth = value;
    return this;
}
Tab* Tab::TabBarPrefix(bool value) {
    tabBarPrefix = value;
    return this;
}
Tab* Tab::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}

static El* FindTabById(El* root, Str id) {
    if (!root) {
        return nullptr;
    }
    if (base::StrEq(root->id, id)) {
        return root;
    }
    for (El* child = root->first; child; child = child->next) {
        if (El* found = FindTabById(child, id)) {
            return found;
        }
    }
    return nullptr;
}

El* Tab::IntoEl() {
    // Use the same rendering path as a TabBar so the state tables cannot
    // drift. The returned node is the tab itself; the two temporary layout
    // wrappers remain harmless frame-arena allocations.
    TabBar* bar = TabBar::New(cx, StrL("standalone-tab"));
    bar->items.Append(a, *this);
    bar->variant = variant;
    bar->size = size;
    bar->maxWidth = maxWidth;
    El* root = bar->IntoEl();
    return FindTabById(root, StrL("0"));
}

TabBar* TabBar::New(Ctx* cx) {
    return New(cx, StrL("tabs"));
}
TabBar* TabBar::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    TabBar* t = ArenaNew<TabBar>(a);
    t->a = a;
    t->cx = cx;
    t->id = id;
    t->lastEmptySpace = Div(a)->W(12)->Shrink0();
    return t;
}
TabBar* TabBar::Child(component::Tab* child) {
    if (child) {
        items.Append(a, *child);
    }
    return this;
}
TabBar* TabBar::Child(Str label) {
    return Tab(label);
}
TabBar* TabBar::Tab(Str label) {
    return Tab(label, IconName::None, false);
}
TabBar* TabBar::Tab(Str label, IconName icon, bool disabled) {
    component::Tab it;
    it.a = a;
    it.cx = cx;
    it.label = label;
    it.icon = icon;
    it.disabled = disabled;
    items.Append(a, it);
    return this;
}
TabBar* TabBar::W(float v) {
    width = v;
    return this;
}
TabBar* TabBar::WFill() {
    width = kFill;
    return this;
}
TabBar* TabBar::Flex1() {
    if (items.len > 0) {
        items[items.len - 1].flex1 = true;
    }
    return this;
}
TabBar* TabBar::AriaLabel(Str label) {
    if (items.len > 0) {
        items[items.len - 1].ariaLabel = label;
    }
    return this;
}
TabBar* TabBar::Disabled(int ix, bool v) {
    if (ix >= 0 && ix < items.len) {
        items[ix].disabled = v;
    }
    return this;
}
TabBar* TabBar::Selected(int i) {
    selected = i;
    return this;
}
TabBar* TabBar::OnChange(Listener fn) {
    onChange = fn;
    return this;
}
TabBar* TabBar::OnClick(Listener fn) {
    return OnChange(fn);
}
TabBar* TabBar::Variant(TabVariant v) {
    variant = v;
    return this;
}
TabBar* TabBar::WithVariant(TabVariant v) {
    return Variant(v);
}
TabBar* TabBar::Outline() {
    return Variant(TabVariant::Outline);
}
TabBar* TabBar::Pill() {
    return Variant(TabVariant::Pill);
}
TabBar* TabBar::Segmented() {
    return Variant(TabVariant::Segmented);
}
TabBar* TabBar::Underline() {
    return Variant(TabVariant::Underline);
}
TabBar* TabBar::Size(UiSize v) {
    size = v;
    return this;
}
TabBar* TabBar::WithSize(UiSize v) {
    return Size(v);
}
TabBar* TabBar::MaxWidth(float v) {
    maxWidth = v;
    return this;
}
TabBar* TabBar::Prefix(El* e) {
    prefix = e;
    return this;
}
TabBar* TabBar::Suffix(El* e) {
    suffix = e;
    return this;
}
TabBar* TabBar::LastEmptySpace(El* e) {
    lastEmptySpace = e;
    return this;
}
TabBar* TabBar::Menu(bool v) {
    menu = v;
    return this;
}
TabBar* TabBar::TrackScroll(int scrollKey, float offset, Listener fn) {
    trackScroll = true;
    scrollId = scrollKey;
    scrollX = offset;
    onScroll = fn;
    return this;
}
TabBar* TabBar::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}

// Nothing painted: `a == 0` is what says a style leaves this part alone,
// which is Rust's `theme.transparent`.
const Rgba kTabNone = {0, 0, 0, 0};

// TabStyle: the four states of a variant, as Rust's normal / hovered /
// selected / disabled return them. `bg` is the tab's own box, `innerBg` the
// box around the label — Segmented paints the selected tab there so the strip
// underneath keeps its own background.
struct TabStyle {
    Rgba fg = {};
    Rgba bg = kTabNone;
    Rgba innerBg = kTabNone;
    Rgba borderColor = kTabNone;
    float borderT = 0;
    float borderL = 0;
    float borderR = 0;
    float borderB = 0;
    bool shadow = false;
};

static TabStyle TabNormal(TabVariant v, const Theme& th) {
    TabStyle s;
    switch (v) {
        case TabVariant::Tab:
            s.fg = th.tabFg;
            s.borderL = s.borderR = 1;
            break;
        case TabVariant::Outline:
            s.fg = th.tabFg;
            // Edges::all(px(1.)): an outline tab is ringed, not underlined.
            s.borderT = s.borderL = s.borderR = s.borderB = 1;
            s.borderColor = th.border;
            break;
        case TabVariant::Pill:
            s.fg = th.foreground;
            break;
        case TabVariant::Segmented:
            s.fg = th.tabFg;
            break;
        case TabVariant::Underline:
            s.fg = th.tabFg;
            s.borderB = 2;
            break;
    }
    return s;
}

static TabStyle TabSelected(TabVariant v, const Theme& th) {
    TabStyle s = TabNormal(v, th);
    switch (v) {
        case TabVariant::Tab:
            s.fg = th.tabActiveFg;
            s.bg = th.tabActiveBg;
            s.borderColor = th.border;
            break;
        case TabVariant::Outline:
            s.fg = th.primary;
            s.borderColor = th.primary;
            break;
        case TabVariant::Pill:
            s.fg = th.primaryFg;
            s.bg = th.primary;
            break;
        case TabVariant::Segmented:
            s.fg = th.tabActiveFg;
            s.innerBg = th.background;
            s.shadow = true;
            break;
        case TabVariant::Underline:
            s.fg = th.tabActiveFg;
            s.borderColor = th.primary;
            break;
    }
    return s;
}

static TabStyle TabHovered(TabVariant v, bool selected, const Theme& th) {
    TabStyle s = TabNormal(v, th);
    switch (v) {
        case TabVariant::Tab:
            s.fg = th.tabActiveFg;
            break;
        case TabVariant::Outline:
            s.fg = th.secondaryFg;
            s.bg = th.secondaryHover;
            break;
        case TabVariant::Pill:
            s.fg = th.secondaryFg;
            s.bg = th.secondary;
            break;
        case TabVariant::Segmented:
            s.fg = th.tabActiveFg;
            s.innerBg = selected ? th.background : kTabNone;
            break;
        case TabVariant::Underline:
            s.fg = th.tabActiveFg;
            break;
    }
    return s;
}

static TabStyle TabDisabled(TabVariant v, bool selected, const Theme& th) {
    TabStyle s = TabNormal(v, th);
    s.fg = th.mutedFg;
    switch (v) {
        case TabVariant::Tab:
            s.borderColor = selected ? th.border : kTabNone;
            break;
        case TabVariant::Outline:
            s.borderColor = selected ? th.primary : th.border;
            break;
        case TabVariant::Pill:
            if (selected) {
                s.fg = RgbaOpacity(th.primaryFg, 0.5f);
                s.bg = RgbaOpacity(th.primary, 0.5f);
            }
            break;
        case TabVariant::Segmented:
            s.bg = th.tabBar;
            s.innerBg = selected ? th.background : kTabNone;
            break;
        case TabVariant::Underline:
            s.borderColor = selected ? th.border : kTabNone;
            break;
    }
    return s;
}

// TabBar::menu's "more" button: an xsmall ghost with a caret, whose dropdown
// is the tab list itself. The rows are the tabs in order, so the index the
// menu confirms is the index the bar's on_click wants.
static El* TabMenuButton(TabBar* tabs, const Theme&, float) {
    Ctx* cx = tabs->cx;
    // The button and its menu are built inside the bar, so the bar's name is
    // what tells one strip's overflow menu from another's.
    IdScope scope(cx, tabs->id);
    Str menuId = StrL("menu");
    Entity<PopupMenuState> st = PopupMenuStateFor(cx, menuId);
    if (PopupMenuState* s = st.Get(cx)) {
        s->onConfirm = tabs->onChange;
    }
    PopupMenu* menu = PopupMenu::New(cx, menuId, st)->Scrollable();
    int i = -1;
    for (const component::Tab& it : tabs->items) {
        i++;
        // A tab with no label is an icon-only one; Rust puts the icon in the
        // row, and falls back to "Unnamed" when there is neither.
        if (it.label.s) {
            menu->MenuWithCheck(it.label, i == tabs->selected);
        } else if (it.icon != IconName::None) {
            menu->Element(IconEl(cx->a, it.icon, 16));
            menu->Checked(i == tabs->selected);
        } else {
            menu->MenuWithCheck(Tr("Dock.Unnamed"), i == tabs->selected);
        }
        if (it.disabled) {
            menu->Disabled(true);
        }
    }
    return DropdownMenu::New(cx, StrL("more"))
        ->Trigger(Button::New(cx, StrL("more-btn"))
                      ->Ghost()
                      ->WithSize(UiSize::XSmall)
                      ->DropdownCaret()
                      ->IntoEl())
        ->Menu(menu)
        ->AnchorRight()
        ->IntoEl();
}

struct TabBarScrollState {
    float offset = 0;

    static void OnScroll(TabBarScrollState* self, Ctx* cx,
                         const ScrollEvent* ev) {
        self->offset = ev->offsetX;
        Notify(cx);
    }
};

El* TabBar::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    float h = TabHeight(variant, size);
    float innerH = TabInnerHeight(variant, size);
    float padX = TabPadX(variant, size);
    float gap = TabBarGap(variant, size);
    float barPadX = TabBarPadX(variant, size);
    float barRadius = TabBarRadius(variant, size, th.radius, th.radius * 1.5f);
    float radius = TabRadius(variant, size, th.radius, th.radius * 1.5f);
    float innerRadius =
        TabInnerRadius(variant, size, th.radius, th.radius * 1.5f);
    float font = UiFontPx(size);

    El* bar = gpui::Tabs::New(cx, id)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->W(width)
                  ->H(h)
                  ->Radius(barRadius);
    if (variant == TabVariant::Tab) {
        bar->Bg(th.tokens.tabBar);
    } else if (variant == TabVariant::Segmented) {
        bar->Bg(th.tokens.tabBarSegmented);
    }
    if (barPadX > 0) {
        bar->PadX(barPadX);
    }
    // The strip's own bottom border, which the folder and underline looks
    // share; Rust draws it as an absolute child so the tabs sit over it.
    if (variant == TabVariant::Tab || variant == TabVariant::Underline) {
        bar->BorderB(1, th.border);
    }
    bar->Refine(style, styleSet);
    if (prefix) {
        bar->Child(prefix);
    }

    // h_flex().id("tabs").flex_1().overflow_x_hidden(): the strip gives way
    // before the bar does, so a suffix and the overflow menu keep their place
    // when there are more tabs than fit.
    El* viewport = Div(a)
                       ->Id(StrL("tabs"))
                       ->FlexRow()
                       ->ItemsCenter()
                       ->Flex1()
                       ->H(kFill)
                       ->ClipX();
    if (barPadX > 0) {
        // Expand the clipping box into the bar's padding, then put its
        // content origin back. This leaves every tab where it was while the
        // segmented indicator's horizontal shadow gets room to fall off.
        viewport->MarginX(-barPadX)->PadX(barPadX);
    }
    float activeScrollX = scrollX;
    int activeScrollId = scrollId;
    Listener activeOnScroll = onScroll;
    if (!trackScroll && cx->win) {
        Entity<TabBarScrollState> state = ElementStateEntity<TabBarScrollState>(
            cx, id, StrL("gpui::component::TabBarScrollState"));
        if (TabBarScrollState* value = state.Get(cx)) {
            activeScrollX = value->offset;
        }
        activeScrollId = HashClickId(id);
        activeOnScroll = ListenTo(state, &TabBarScrollState::OnScroll);
    }
    El* strip = Div(a)
                    ->Id(StrL("tabs-inner"))
                    ->FlexRow()
                    ->ItemsCenter()
                    ->W(kFill)
                    ->H(kFill)
                    ->ScrollX(activeScrollX)
                    ->ScrollId(activeScrollId)
                    ->OnScroll(activeOnScroll);
    if (barPadX > 0) {
        strip->MarginX(-barPadX)->PadX(barPadX);
    }
    if (gap > 0) {
        strip->Gap(gap);
    }
    // Where the selected tab was last frame, and where the indicator has got
    // to on its way there. Both outlive the frame, so both are motion slots.
    auto* selBox = (Bounds*)MotionSlot(cx, MotionId(StrL("tab-sel"), id),
                                       (int)sizeof(Bounds));
    auto* stripBox = (Bounds*)MotionSlot(cx, MotionId(StrL("tab-strip"), id),
                                         (int)sizeof(Bounds));
    strip->BoundsOut(stripBox);
    // spring_move: the selection moves again while the indicator is still
    // travelling — a run down a row of tabs — so it is sprung, and slightly
    // under critical so it arrives with a little of the overshoot the eye
    // reads as weight. A tenth of a pixel is arrived.
    Spring indSpring = th.motion.springMove;
    float indX = 0;
    float indW = 0;
    bool sliding = false;
    if (selBox && selBox->w > 0) {
        indX = SpringValue(cx, MotionId(StrL("tab-ind-x"), id), selBox->x,
                           indSpring);
        indW = SpringValue(cx, MotionId(StrL("tab-ind-w"), id), selBox->w,
                           indSpring);
        // In flight: the tab it belongs to holds back its own selected look
        // while the indicator is on its way, which is what Rust does too.
        float dx = indX - selBox->x;
        float dw = indW - selBox->w;
        sliding = (dx < -0.5f || dx > 0.5f) || (dw < -0.5f || dw > 0.5f);
    }
    int i = -1;
    for (const component::Tab& item : items) {
        i++;
        bool on = selected >= 0 ? i == selected : item.selected;
        TabStyle st = item.disabled      ? TabDisabled(variant, on, th)
                      : (on && !sliding) ? TabSelected(variant, th)
                                         : TabNormal(variant, th);
        if (on && sliding && !item.disabled) {
            // The label keeps the colour it is going to end up with; it is the
            // box behind it that the indicator is carrying.
            st.fg = TabSelected(variant, th).fg;
        }
        Str tabId = StrDup(a, fmt("%d", i));
        Listener click =
            onChange.IsValid() ? ListenerArg(onChange, i) : item.onClick;
        El* tab = gpui::Tab::New(cx, tabId, item.disabled,
                                 item.disabled ? Listener{} : click, on,
                                 item.ariaLabel.s ? item.ariaLabel : item.label,
                                 i + 1, items.len)
                      ->FlexRow()
                      ->ItemsCenter()
                      ->JustifyCenter()
                      ->Shrink0()
                      ->H(kFill)
                      ->Gap(4)
                      ->Radius(radius);
        // The instance refinement is applied before selected/disabled state,
        // the same order gpui-base::Tab resolves its StateStyle.
        StyleApplyFields(&tab->style, item.style, item.styleSet);
        if (st.bg.a) {
            tab->Bg(st.bg);
        }
        // A caller with no prefix can opt out of the first folder tab's left
        // edge, matching Rust's tab_bar_prefix(false) refinement.
        if (st.borderT > 0 && st.borderL > 0 && st.borderR > 0 &&
            st.borderB > 0) {
            // Edges::all: a ring rather than four rules, so the radius the
            // tab asked for is what the stroke follows.
            tab->Border(st.borderT, st.borderColor);
        } else {
            if (st.borderL > 0 &&
                !(i == 0 && variant == TabVariant::Tab && !item.tabBarPrefix)) {
                tab->BorderL(st.borderL, st.borderColor);
            }
            if (st.borderR > 0) {
                tab->BorderR(st.borderR, st.borderColor);
            }
            if (st.borderB > 0) {
                tab->BorderB(st.borderB, st.borderColor);
            }
        }
        if (!item.disabled) {
            TabStyle hov = TabHovered(variant, on, th);
            if (hov.bg.a) {
                tab->HoverBg(hov.bg);
            }
            tab->HoverFg(hov.fg);
        }

        // The inner box is what carries the padding, the label and — for
        // Segmented — the background of the selected tab.
        El* inner = Div(a)
                        ->FlexRow()
                        ->LineHeight(1.25f)
                        ->ItemsCenter()
                        ->JustifyCenter()
                        ->H(innerH)
                        ->Radius(innerRadius);
        if (st.innerBg.a) {
            inner->Bg(st.innerBg);
        }
        if (st.shadow) {
            RaisedShadow(inner);
        }
        // inner_margins: the underline's inner box stands off the border
        // above and below it. There is no margin here, so the tab pads the
        // box away from its own edges, which comes to the same thing.
        float marginTop = TabMarginTop(variant, size);
        if (marginTop > 0) {
            tab->PadT(marginTop)->PadB(TabMarginBottom(variant, size));
        }
        if (item.icon != IconName::None) {
            // An icon tab is square and exempt from max_width.
            inner->W(innerH * 1.25f)
                ->Child(IconEl(a, item.icon, UiIconPx(size))->Fg(st.fg));
        } else {
            if (padX > 0) {
                inner->PadX(padX);
            }
            if (item.label.s) {
                El* label = TextEl(a, item.label)->Font(font)->Fg(st.fg);
                if (maxWidth > 0) {
                    // Text takes its natural width, so capping a tab means
                    // giving the label a box it is allowed to shrink inside.
                    float labelMax = maxWidth - padX * 2;
                    label->MaxW(labelMax > 0 ? labelMax : 0)->Truncate();
                }
                inner->Child(label);
            }
            for (El* child : item.children) {
                inner->Child(child);
            }
        }
        // Upstream now copies a tab's flex grow/basis onto its measurement
        // wrapper. C++ has no extra wrapper here, so applying Flex1 directly
        // gives every variant the same corrected behavior.
        if (item.flex1) {
            tab->Flex1();
            inner->W(kFill);
        }
        if (item.prefix) {
            El* prefixWrap = Div(a)->Child(item.prefix);
            if (maxWidth > 0) {
                prefixWrap->Shrink0();
            }
            tab->Child(prefixWrap);
        }
        tab->Child(inner);
        if (item.suffix) {
            El* suffixWrap = Div(a)->Child(item.suffix);
            if (maxWidth > 0) {
                suffixWrap->Shrink0();
            }
            tab->Child(suffixWrap);
        }
        if (maxWidth > 0 && item.icon == IconName::None) {
            tab->MaxW(maxWidth);
        }
        if (on && selBox) {
            // The box the indicator is heading for, measured where it is.
            tab->BoundsOut(selBox);
        }
        strip->Child(tab);
    }
    if ((suffix || menu) && lastEmptySpace) {
        strip->Child(lastEmptySpace);
    }
    if (sliding && stripBox) {
        // The indicator itself, which is whatever the selected tab would have
        // painted: the raised inner box for a folder or a segmented strip, the
        // filled pill, or the two-pixel rule under an underline tab.
        float x = indX - stripBox->x;
        El* ind = Div(a)->Absolute()->Left(x)->W(indW);
        switch (variant) {
            case TabVariant::Underline:
                ind->Bottom(0)->H(2)->Bg(th.tokens.primary);
                break;
            case TabVariant::Pill:
                ind->Top(0)->H(kFill)->Radius(99)->Bg(th.tokens.primary);
                break;
            case TabVariant::Segmented:
                ind->Top((h - innerH) * 0.5f)
                    ->H(innerH)
                    ->Radius(innerRadius)
                    ->Bg(th.tokens.background);
                RaisedShadow(ind);
                break;
            case TabVariant::Tab:
                ind->Top((h - innerH) * 0.5f)
                    ->H(innerH)
                    ->Radius(innerRadius)
                    ->Bg(th.tokens.background);
                break;
            default:
                ind = nullptr;
                break;
        }
        if (ind) {
            strip->Child(ind);
        }
    }
    viewport->Child(strip);
    bar->Child(viewport);
    if (menu) {
        bar->Child(TabMenuButton(this, th, font));
    }
    if (suffix) {
        bar->Child(suffix);
    }
    return bar;
}

} // namespace component
} // namespace gpui
