#include "ui/questionnaire.h"

#include "base/radio_group.h"
#include "base/theme.h"
#include "ui/button.h"
#include "ui/i18n.h"
#include "ui/input.h"
#include "ui/kbd.h"

namespace gpui {

namespace component {

// ─── scale ────────────────────────────────────────────────────────────────

// QuestionnaireSizes: where a questionnaire's scale lives between the root
// and its parts. The root records the scale it was given under its state's
// id, and every part of that questionnaire reads it back. A root renders
// before its children, so the entry is in place by the time a part looks.
struct QuestionnaireSizes {
    struct Entry {
        EntityId id = {};
        UiSize size = UiSize::Medium;
    };
    Vec<Entry> entries;
};

// Bounded so an application that builds and drops many questionnaires cannot
// grow the table without end. Every root rewrites its entry on the next
// frame, so clearing it costs at most one frame at the default scale.
static const int kMaxTrackedQuestionnaires = 128;

void QuestionnairePublishSize(App* app, Entity<QuestionnaireState> state,
                              UiSize size) {
    QuestionnaireSizes* sizes = AppGlobalEnsure<QuestionnaireSizes>(app);
    if (!sizes) {
        return;
    }
    for (QuestionnaireSizes::Entry& e : sizes->entries) {
        if (e.id == state.id) {
            e.size = size;
            return;
        }
    }
    if (len(sizes->entries) >= kMaxTrackedQuestionnaires) {
        VecClear(sizes->entries);
    }
    QuestionnaireSizes::Entry e;
    e.id = state.id;
    e.size = size;
    VecAppend(sizes->entries, e);
}

UiSize QuestionnaireResolveSize(App* app, const QuestionnairePart* part) {
    if (part->hasSize) {
        return part->size;
    }
    const QuestionnaireSizes* sizes = AppGlobalGet<QuestionnaireSizes>(app);
    if (sizes) {
        for (const QuestionnaireSizes::Entry& e : sizes->entries) {
            if (e.id == part->state.id) {
                return e.size;
            }
        }
    }
    return UiSize::Medium;
}

static const SemanticThemeTokens& Tokens(App* app) {
    return BaseThemeGlobal(app)->tokens;
}

// The questionnaire skin's geometry, following the ReUI `base-nova`
// questionnaire at Medium. Every number comes from the semantic spacing and
// radius tokens; the size picks which of them apply.
struct QuestionnaireMetrics {
    float rootGap = 0;
    float itemGap = 0;
    float choicesGap = 0;
    float choiceGap = 0;
    float contentGap = 0;
    float choicePaddingX = 0;
    float choicePaddingY = 0;
    float choiceMinHeight = 0;
    float choiceRadius = 0;
    float indicatorSize = 0;
    float indicatorMarkSize = 0;
    float indicatorCheckSize = 0;
    float shortcutSize = 0;
    float shortcutTextSize = 0;
    float shortcutRadius = 0;
};

static QuestionnaireMetrics Metrics(UiSize size, App* app) {
    const SemanticThemeTokens& tokens = Tokens(app);
    const SpacingTokens& sp = tokens.spacing;
    const RadiusTokens& radius = tokens.radius;
    QuestionnaireMetrics m;
    switch (size) {
        case UiSize::XSmall:
            m = {sp.sm,
                 sp.sm,
                 sp.xs,
                 sp.xs + sp.xxs,
                 sp.xxs,
                 sp.sm,
                 sp.xs,
                 sp.xl + sp.xs,
                 radius.md,
                 sp.md,
                 sp.xs + sp.xxs * 0.5f,
                 sp.sm + sp.xxs,
                 sp.lg,
                 sp.sm,
                 radius.sm};
            break;
        case UiSize::Small:
            m = {sp.md,          sp.md,
                 sp.xs + sp.xxs, sp.sm,
                 sp.xxs,         sp.sm + sp.xxs,
                 sp.sm,          sp.xxl + sp.xs,
                 radius.lg,      sp.md + sp.xxs,
                 sp.xs + sp.xxs, sp.md,
                 sp.lg + sp.xxs, sp.sm + sp.xxs * 0.5f,
                 radius.md};
            break;
        case UiSize::Large:
            m = {sp.xl,     sp.xl,          sp.sm + sp.xxs, sp.md,
                 sp.xs,     sp.lg,          sp.md,          sp.xxl + sp.lg,
                 radius.xl, sp.lg + sp.xxs, sp.sm + sp.xxs, sp.lg,
                 sp.xl,     sp.md,          radius.lg};
            break;
        case UiSize::Size: {
            float v = size.pixels;
            m = {v,         v,          v * 0.5f,  v * 0.625f, v * 0.125f,
                 v * 0.75f, v * 0.625f, v * 2.75f, radius.lg,  v,
                 v * 0.5f,  v * 0.875f, v * 1.25f, v * 0.625f, radius.md};
            break;
        }
        default:
            m = {sp.lg,         sp.lg,          sp.sm,          sp.sm + sp.xxs,
                 sp.xxs,        sp.md,          sp.sm + sp.xxs, sp.xxl + sp.md,
                 radius.lg,     sp.lg,          sp.sm,          sp.md + sp.xxs,
                 sp.lg + sp.xs, sp.sm + sp.xxs, radius.md};
            break;
    }
    return m;
}

static El* ApplyTextToken(El* e, const TextStyleToken& token) {
    e->Font(token.size);
    if (token.size > 0) {
        e->LineHeight(token.lineHeight / token.size);
    }
    return e->Weight(token.weight);
}

// Answer text matches the Checkbox and Radio family's label at each size.
static El* TextStyle(El* e, UiSize size, App* app) {
    const TypographyTokens& t = Tokens(app).typography;
    switch (size) {
        case UiSize::XSmall:
            return ApplyTextToken(e, t.xs);
        case UiSize::Small:
            return ApplyTextToken(e, t.sm);
        case UiSize::Large:
            return ApplyTextToken(e, t.lg);
        case UiSize::Size:
            return e->Font(size.pixels);
        default:
            return ApplyTextToken(e, t.md);
    }
}

// Secondary text sits one step below the answer text.
static El* SecondaryTextStyle(El* e, UiSize size, App* app) {
    const TypographyTokens& t = Tokens(app).typography;
    switch (size) {
        case UiSize::XSmall:
        case UiSize::Small:
            return ApplyTextToken(e, t.xs);
        case UiSize::Large:
            return ApplyTextToken(e, t.md);
        case UiSize::Size:
            return e->Font(size.pixels * 0.875f);
        default:
            return ApplyTextToken(e, t.sm);
    }
}

static El* ProgressTextStyle(El* e, UiSize size, App* app) {
    const TypographyTokens& t = Tokens(app).typography;
    switch (size) {
        case UiSize::Large:
            ApplyTextToken(e, t.sm);
            break;
        case UiSize::Size:
            e->Font(size.pixels * 0.75f);
            break;
        default:
            ApplyTextToken(e, t.xs);
            break;
    }
    return e->Weight(FontWeight::Medium);
}

static El* TitleTextStyle(El* e, UiSize size, App* app) {
    const TypographyTokens& t = Tokens(app).typography;
    switch (size) {
        case UiSize::XSmall:
            ApplyTextToken(e, t.sm);
            break;
        case UiSize::Small:
            ApplyTextToken(e, t.md);
            break;
        case UiSize::Large:
            ApplyTextToken(e, t.xl);
            break;
        case UiSize::Size:
            e->Font(size.pixels * 1.125f);
            break;
        default:
            ApplyTextToken(e, t.lg);
            break;
    }
    return e->Weight(FontWeight::Medium);
}

// The line box the answer label occupies. An indicator or a shortcut badge
// centers on that first line, so a two-line answer keeps them beside the
// label rather than drifting toward the description.
static float AnswerLineHeight(UiSize size, App* app) {
    const TypographyTokens& t = Tokens(app).typography;
    switch (size) {
        case UiSize::XSmall:
            return t.xs.lineHeight;
        case UiSize::Small:
            return t.sm.lineHeight;
        case UiSize::Large:
            return t.lg.lineHeight;
        case UiSize::Size:
            return size.pixels * 1.5f;
        default:
            return t.md.lineHeight;
    }
}

// How far to push an adornment of `height` down so it centers on that line.
static float CenterOnAnswerLine(float height, UiSize size, App* app) {
    float v = (AnswerLineHeight(size, app) - height) * 0.5f;
    return v > 0 ? v : 0;
}

// A part addresses its question by name. A name the schema does not define
// renders nothing, which is silent enough to hide a typo, so a debug build
// names what went missing.
static void ReportUnknownItem(Str item) {
#if defined(DEBUG)
    logf("questionnaire has no item named `%s`; the part renders nothing\n",
         item);
#else
    (void)item;
#endif
}

static void ReportUnknownChoice(Str item, Str value) {
#if defined(DEBUG)
    logf(
        "questionnaire item `%s` has no choice named `%s`; the part renders "
        "nothing\n",
        item, value);
#else
    (void)item;
    (void)value;
#endif
}

static Str ElementId(Ctx* cx, Entity<QuestionnaireState> state, Str suffix) {
    return StrDup(cx->a, fmt("questionnaire-%d-%s", state.id.index, suffix));
}

static El* RefineWith(El* e, const QuestionnairePart* part) {
    if (e && part->styleSet) {
        e->Refine(part->style, part->styleSet);
    }
    part->refiner.Apply(e);
    return e;
}

static El* AddChildren(El* e, QuestionnairePart* part) {
    for (El* child : part->children) {
        e->Child(child);
    }
    return e;
}

template <typename T>
static T* NewPart(Ctx* cx, Entity<QuestionnaireState> state, Str item) {
    T* p = ArenaNew<T>(cx->a);
    p->a = cx->a;
    p->cx = cx;
    p->state = state;
    p->item = item;
    return p;
}

#define GPUI_QUESTIONNAIRE_PART_IMPL(T)                      \
    T* T::WithSize(UiSize s) {                               \
        size = s;                                            \
        hasSize = true;                                      \
        return this;                                         \
    }                                                        \
    T* T::Child(El* e) {                                     \
        if (e) {                                             \
            children.Append(a, e);                           \
        }                                                    \
        return this;                                         \
    }                                                        \
    T* T::Refine(const Style& refinement, uint32_t fields) { \
        StyleApplyFields(&style, refinement, fields);        \
        styleSet |= fields;                                  \
        return this;                                         \
    }

GPUI_QUESTIONNAIRE_PART_IMPL(Questionnaire)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireProgress)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireTitle)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireDescription)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireItem)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireChoices)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireChoice)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireChoiceDescription)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireInput)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireError)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireActions)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnairePrevious)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireSkip)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireNext)
GPUI_QUESTIONNAIRE_PART_IMPL(QuestionnaireSubmit)

#undef GPUI_QUESTIONNAIRE_PART_IMPL

// ─── root ─────────────────────────────────────────────────────────────────

static void RootKeyDown(QuestionnaireState* self, Ctx* cx, const KeyEvent* ev) {
    QuestionnaireHandleKeyDown(self->self, const_cast<KeyEvent*>(ev), cx);
}

Questionnaire* Questionnaire::New(Ctx* cx, Entity<QuestionnaireState> state) {
    return NewPart<Questionnaire>(cx, state, {});
}

El* Questionnaire::IntoEl() {
    // The root is the one place a caller names the scale, so it records it
    // for the parts before any of them render.
    UiSize resolved = hasSize ? size : UiSize(UiSize::Medium);
    QuestionnairePublishSize(cx->app, state, resolved);
    QuestionnaireMetrics m = Metrics(resolved, cx->app);
    const QuestionnaireState* s = state.Get(cx->app);
    El* e = Div(a)
                ->Id(ElementId(cx, state, StrL("root")))
                ->Role(AccessibilityRole::Form)
                ->KeyContext(StrL("Questionnaire"))
                ->CaptureKeyDown(ListenTo(state, &RootKeyDown))
                ->FlexCol()
                ->MinW(0)
                ->Gap(m.rootGap)
                ->W(kFill);
    if (s) {
        e->TrackFocus(s->GetFocusHandle());
    }
    return AddChildren(RefineWith(e, this), this);
}

// ─── progress ─────────────────────────────────────────────────────────────

// t!("Questionnaire.progress", current = .., total = ..): the catalogue keeps
// rust_i18n's `%{name}` placeholders, filled in here.
static Str ProgressLabel(Arena* a, int current, int total) {
    Str pattern = Tr("Questionnaire.progress");
    StrBuilder out;
    int i = 0;
    while (i < len(pattern)) {
        Str rest = Str(pattern.s + i, len(pattern) - i);
        if (StrStartsWith(rest, "%{current}")) {
            out.Append(fmt("%d", current));
            i += 10;
        } else if (StrStartsWith(rest, "%{total}")) {
            out.Append(fmt("%d", total));
            i += 8;
        } else {
            out.AppendChar(pattern.s[i]);
            i++;
        }
    }
    return StrDup(a, Str(out.els, len(out)));
}

QuestionnaireProgress* QuestionnaireProgress::New(
    Ctx* cx, Entity<QuestionnaireState> state) {
    return NewPart<QuestionnaireProgress>(cx, state, {});
}

El* QuestionnaireProgress::IntoEl() {
    const QuestionnaireState* s = state.Get(cx->app);
    if (!s) {
        return nullptr;
    }
    QuestionnaireProgressState progress = s->Progress();
    Str label = ProgressLabel(a, progress.current, progress.total);
    const ColorTokens& colors = Tokens(cx->app).colors;
    UiSize resolved = QuestionnaireResolveSize(cx->app, this);
    El* e = ProgressTextStyle(Div(a)
                                  ->Id(ElementId(cx, state, StrL("progress")))
                                  ->Role(AccessibilityRole::ProgressIndicator)
                                  ->AriaLabel(label)
                                  ->AriaMinNumericValue(0)
                                  ->AriaMaxNumericValue((float)progress.total)
                                  ->AriaNumericValue((float)progress.current)
                                  ->Fg(colors.mutedForeground),
                              resolved, cx->app);
    RefineWith(e, this);
    if (children.len == 0) {
        e->Child(TextEl(a, label));
    }
    return AddChildren(e, this);
}

// ─── title / description ──────────────────────────────────────────────────

using TextStyleFn = El* (*)(El*, UiSize, App*);

// questionnaire_item_part!: a line of an item's own text, or its children.
static El* ItemTextPart(QuestionnairePart* part, Str fallback, TextStyleFn fn,
                        Rgba color, bool closesItemGap) {
    Ctx* cx = part->cx;
    const QuestionnaireState* s = part->state.Get(cx->app);
    const QuestionnaireItemDefinition* d =
        s ? s->ItemDefinition(part->item) : nullptr;
    if (!d) {
        ReportUnknownItem(part->item);
        return nullptr;
    }
    bool hasChildren = part->children.len > 0;
    if (!hasChildren && len(fallback) == 0) {
        return nullptr;
    }
    UiSize resolved = QuestionnaireResolveSize(cx->app, part);
    El* e = fn(Div(part->a)->W(kFill)->Fg(color), resolved, cx->app);
    // The item stacks its parts on one gap. A title with no description of
    // its own closes the gap the description would have filled, so answers
    // never crowd the question.
    if (closesItemGap && len(d->description) == 0) {
        e->MarginB(Metrics(resolved, cx->app).itemGap);
    }
    RefineWith(e, part);
    if (!hasChildren) {
        e->Child(TextEl(part->a, fallback));
    }
    return AddChildren(e, part);
}

QuestionnaireTitle* QuestionnaireTitle::New(Ctx* cx,
                                            Entity<QuestionnaireState> state,
                                            Str item) {
    return NewPart<QuestionnaireTitle>(cx, state, item);
}

El* QuestionnaireTitle::IntoEl() {
    const QuestionnaireState* s = state.Get(cx->app);
    const QuestionnaireItemDefinition* d =
        s ? s->ItemDefinition(item) : nullptr;
    return ItemTextPart(this, d ? d->accessibilityLabel : Str{},
                        &TitleTextStyle, Tokens(cx->app).colors.foreground,
                        true);
}

static El* DescriptionTextStyle(El* e, UiSize size, App* app) {
    return SecondaryTextStyle(e, size, app);
}

QuestionnaireDescription* QuestionnaireDescription::New(
    Ctx* cx, Entity<QuestionnaireState> state, Str item) {
    return NewPart<QuestionnaireDescription>(cx, state, item);
}

El* QuestionnaireDescription::IntoEl() {
    const QuestionnaireState* s = state.Get(cx->app);
    const QuestionnaireItemDefinition* d =
        s ? s->ItemDefinition(item) : nullptr;
    return ItemTextPart(this, d ? d->description : Str{}, &DescriptionTextStyle,
                        Tokens(cx->app).colors.mutedForeground, false);
}

// ─── item ─────────────────────────────────────────────────────────────────

// Whether `item` is the active, enabled question; reports an unknown name.
static bool ActiveItem(const QuestionnaireState* s, Str item,
                       QuestionnaireItemState* out) {
    if (!s || !s->ItemState(item, out)) {
        ReportUnknownItem(item);
        return false;
    }
    return StrEq(s->CurrentItem(), item) && !out->disabled;
}

QuestionnaireItem* QuestionnaireItem::New(Ctx* cx,
                                          Entity<QuestionnaireState> state,
                                          Str item) {
    return NewPart<QuestionnaireItem>(cx, state, item);
}

El* QuestionnaireItem::IntoEl() {
    const QuestionnaireState* s = state.Get(cx->app);
    QuestionnaireItemState itemState;
    if (!ActiveItem(s, item, &itemState)) {
        return nullptr;
    }
    const QuestionnaireItemDefinition* d = s->ItemDefinition(item);
    QuestionnaireMetrics m =
        Metrics(QuestionnaireResolveSize(cx->app, this), cx->app);
    El* e = Div(a)
                ->Id(ElementId(cx, state, StrDup(a, fmt("item-%s", item))))
                ->Role(AccessibilityRole::Group)
                ->AriaLabel(d->accessibilityLabel)
                ->FlexCol()
                ->Gap(m.itemGap)
                ->W(kFill);
    if (const FocusHandle* h = s->ItemFocusHandle(item)) {
        e->TrackFocus(*h)->TabIndex(-1)->TabStop(false);
    }
    return AddChildren(RefineWith(e, this), this);
}

// ─── choices ──────────────────────────────────────────────────────────────

QuestionnaireChoices* QuestionnaireChoices::New(
    Ctx* cx, Entity<QuestionnaireState> state, Str item) {
    return NewPart<QuestionnaireChoices>(cx, state, item);
}

El* QuestionnaireChoices::IntoEl() {
    const QuestionnaireState* s = state.Get(cx->app);
    QuestionnaireItemState itemState;
    if (!ActiveItem(s, item, &itemState)) {
        return nullptr;
    }
    QuestionnaireMetrics m =
        Metrics(QuestionnaireResolveSize(cx->app, this), cx->app);
    Str id = ElementId(cx, state, StrDup(a, fmt("choices-%s", item)));
    El* e = nullptr;
    if (itemState.multiple) {
        e = Div(a)->Id(id)->Role(AccessibilityRole::Group);
    } else {
        e = gpui::RadioGroup::New(cx, id, Axis::Vertical);
    }
    e->FlexCol()->Gap(m.choicesGap)->W(kFill);
    return AddChildren(RefineWith(e, this), this);
}

// ─── choice ───────────────────────────────────────────────────────────────

QuestionnaireChoice* QuestionnaireChoice::New(Ctx* cx,
                                              Entity<QuestionnaireState> state,
                                              Str item, Str value) {
    QuestionnaireChoice* p = NewPart<QuestionnaireChoice>(cx, state, item);
    p->value = value;
    return p;
}

QuestionnaireChoice* QuestionnaireChoice::IndicatorStyle(const Style& v,
                                                         uint32_t fields) {
    StyleApplyFields(&indicatorStyle, v, fields);
    indicatorStyleSet |= fields;
    return this;
}

QuestionnaireChoice* QuestionnaireChoice::ContentStyle(const Style& v,
                                                       uint32_t fields) {
    StyleApplyFields(&contentStyle, v, fields);
    contentStyleSet |= fields;
    return this;
}

QuestionnaireChoice* QuestionnaireChoice::ShortcutStyle(const Style& v,
                                                        uint32_t fields) {
    StyleApplyFields(&shortcutStyle, v, fields);
    shortcutStyleSet |= fields;
    return this;
}

QuestionnaireChoice* QuestionnaireChoice::RenderIndicator(
    QuestionnaireChoiceRenderer fn) {
    indicatorRenderer = fn;
    return this;
}

QuestionnaireChoice* QuestionnaireChoice::RenderShortcut(
    QuestionnaireChoiceRenderer fn) {
    shortcutRenderer = fn;
    return this;
}

El* QuestionnaireChoice::IntoEl() {
    App* app = cx->app;
    const QuestionnaireState* s = state.Get(app);
    if (!s) {
        return nullptr;
    }
    QuestionnaireChoiceState choice;
    if (!s->ChoiceState(item, value, &choice)) {
        QuestionnaireItemState probe;
        if (!s->ItemState(item, &probe)) {
            ReportUnknownItem(item);
        } else {
            ReportUnknownChoice(item, value);
        }
        return nullptr;
    }
    if (!StrEq(s->CurrentItem(), item)) {
        return nullptr;
    }
    QuestionnaireItemState itemState;
    s->ItemState(item, &itemState);
    const QuestionnaireChoiceDefinition* definition =
        s->ChoiceDefinition(item, value);
    bool multiple = itemState.multiple;
    bool selected = choice.selected;
    bool disabled = choice.disabled;
    bool invalid = choice.invalid;
    const SemanticThemeTokens& tokens = Tokens(app);
    const ColorTokens& colors = tokens.colors;
    const RadiusTokens& radius = tokens.radius;
    const Theme& th = ThemeNow(app);
    Rgba indicatorBackground = th.inputBg;
    UiSize resolved = QuestionnaireResolveSize(app, this);
    QuestionnaireMetrics m = Metrics(resolved, app);
    float indicatorOffset = CenterOnAnswerLine(m.indicatorSize, resolved, app);
    float shortcutOffset = CenterOnAnswerLine(m.shortcutSize, resolved, app);
    const FocusHandle* focus = s->ChoiceFocusHandle(item, value);
    bool focused = focus && FocusHandleIsFocused(cx->win, *focus);
    bool hasChildren = children.len > 0;

    // The slot, not the element, owns the vertical alignment, so a custom
    // indicator lands on the label's line without having to know the
    // metrics.
    El* indicatorEl = nullptr;
    if (indicatorRenderer) {
        indicatorEl = indicatorRenderer(cx, &choice);
    } else {
        indicatorEl = Div(a)
                          ->FlexRow()
                          ->ItemsCenter()
                          ->JustifyCenter()
                          ->Shrink0()
                          ->W(m.indicatorSize)
                          ->H(m.indicatorSize)
                          ->Border(1, selected ? colors.primary : colors.input)
                          ->Bg(selected ? colors.primary : indicatorBackground)
                          ->Radius(multiple ? radius.sm : radius.full);
        if (indicatorStyleSet) {
            indicatorEl->Refine(indicatorStyle, indicatorStyleSet);
        }
        if (selected && multiple) {
            indicatorEl->Child(IconEl(a, IconName::Check, m.indicatorCheckSize)
                                   ->Fg(colors.primaryForeground));
        }
        if (selected && !multiple) {
            indicatorEl->Child(Div(a)
                                   ->W(m.indicatorMarkSize)
                                   ->H(m.indicatorMarkSize)
                                   ->Radius(radius.full)
                                   ->Bg(colors.primaryForeground));
        }
    }
    El* indicator =
        Div(a)->Shrink0()->MarginT(indicatorOffset)->Child(indicatorEl);

    El* content = Div(a)->FlexCol()->Flex1()->Gap(m.contentGap);
    if (contentStyleSet) {
        content->Refine(contentStyle, contentStyleSet);
    }
    if (!hasChildren) {
        content->Child(
            TextStyle(Div(a)
                          ->Fg(colors.foreground)
                          ->Child(TextEl(a, definition->accessibilityLabel)),
                      resolved, app));
        if (len(definition->description) > 0) {
            content->Child(SecondaryTextStyle(
                Div(a)
                    ->Fg(colors.mutedForeground)
                    ->Child(TextEl(a, definition->description)),
                resolved, app));
        }
    }
    AddChildren(content, this);

    El* shortcutEl = nullptr;
    if (shortcutRenderer) {
        shortcutEl = shortcutRenderer(cx, &choice);
    } else if (len(choice.shortcut) > 0) {
        // Kbd::new(keystroke).outline(), restyled whole — its size, padding,
        // fill, border, text colour, mono family, text size, weight and
        // radius are all the questionnaire's. Kbd here sets its own text
        // size on the inner run, so the chip is spelled out rather than
        // overridden from outside.
        Keystroke stroke;
        stroke.key = StrDup(a, fmt("%c", choice.shortcut.s[0] >= 'A' &&
                                                 choice.shortcut.s[0] <= 'Z'
                                             ? choice.shortcut.s[0] + 32
                                             : choice.shortcut.s[0]));
        shortcutEl =
            Div(a)
                ->FlexRow()
                ->ItemsCenter()
                ->JustifyCenter()
                ->W(m.shortcutSize)
                ->H(m.shortcutSize)
                ->Pad(0)
                ->Bg(colors.background)
                ->Border(1, colors.input)
                ->Fg(colors.mutedForeground)
                ->Mono()
                ->Font(m.shortcutTextSize)
                ->Weight(FontWeight::Medium)
                ->Radius(m.shortcutRadius)
                ->Child(TextEl(a, KbdFormatStr(cx, stroke))->LineHeight(1.f));
        if (shortcutStyleSet) {
            shortcutEl->Refine(shortcutStyle, shortcutStyleSet);
        }
    }
    El* shortcut =
        Div(a)->Shrink0()->MarginT(shortcutOffset)->Child(shortcutEl);

    Str id = ElementId(cx, state, StrDup(a, fmt("choice-%s-%s", item, value)));
    QuestionnaireChoiceControl control;
    if (!QuestionnaireChoiceControl::New(cx, state, item, value, id,
                                         &control)) {
        return nullptr;
    }
    // style_choice_card: the same card whichever control is underneath.
    El* e = control.el;
    e->FlexRow()
        ->ItemsStart()
        ->Gap(m.choiceGap)
        ->W(kFill)
        ->MinH(m.choiceMinHeight)
        ->PadX(m.choicePaddingX)
        ->PadY(m.choicePaddingY)
        ->Border(1, invalid    ? colors.destructive
                    : selected ? RgbaOpacity(colors.primary, 0.4f)
                               : colors.input)
        ->Bg(selected                     ? colors.muted
             : th.mode == ThemeMode::Dark ? RgbaOpacity(colors.input, 0.2f)
                                          : RgbaOpacity(colors.background, 0))
        ->Radius(m.choiceRadius);
    if (!disabled) {
        e->HoverBg(BackgroundOpacity(Background(colors.muted), 0.5f));
    }
    if (focused) {
        e->FocusRing(true);
    }
    if (disabled) {
        e->Opacity(0.5f);
    }
    RefineWith(e, this);
    return e->Child(indicator)->Child(content)->Child(shortcut);
}

// ─── choice description ───────────────────────────────────────────────────

QuestionnaireChoiceDescription* QuestionnaireChoiceDescription::New(Ctx* cx) {
    return NewPart<QuestionnaireChoiceDescription>(cx, {}, {});
}

El* QuestionnaireChoiceDescription::IntoEl() {
    const ColorTokens& colors = Tokens(cx->app).colors;
    El* e =
        SecondaryTextStyle(Div(a)->Fg(colors.mutedForeground),
                           hasSize ? size : UiSize(UiSize::Medium), cx->app);
    return AddChildren(RefineWith(e, this), this);
}

// ─── input ────────────────────────────────────────────────────────────────

QuestionnaireInput* QuestionnaireInput::New(Ctx* cx,
                                            Entity<QuestionnaireState> state,
                                            Str item) {
    return NewPart<QuestionnaireInput>(cx, state, item);
}

El* QuestionnaireInput::IntoEl() {
    const QuestionnaireState* s = state.Get(cx->app);
    QuestionnaireItemState itemState;
    if (!s || !s->ItemState(item, &itemState)) {
        ReportUnknownItem(item);
        return nullptr;
    }
    const QuestionnaireItemDefinition* d = s->ItemDefinition(item);
    const QuestionnaireInputDefinition* input = d ? d->Input() : nullptr;
    if (!input || !input->state || !StrEq(s->CurrentItem(), item)) {
        return nullptr;
    }
    UiSize resolved = QuestionnaireResolveSize(cx->app, this);
    QuestionnaireMetrics m = Metrics(resolved, cx->app);
    El* e =
        Input::New(cx, ElementId(cx, state, StrDup(a, fmt("input-%s", item))),
                   input->state)
            ->AriaLabel(input->accessibilityLabel)
            ->Disabled(itemState.disabled || input->disabled)
            ->WithSize(resolved)
            ->IntoEl();
    // The freeform answer is one of the answers, so its text starts on the
    // same edge a choice's indicator does — the card padding.
    e->PadL(m.choicePaddingX)->Radius(m.choiceRadius);
    if (itemState.invalid) {
        e->Border(1, Tokens(cx->app).colors.destructive);
    }
    return RefineWith(e, this);
}

// ─── error ────────────────────────────────────────────────────────────────

Str QuestionnaireErrorText(const QuestionnaireValidationError& error) {
    switch (error.kind) {
        case QuestionnaireValidationErrorKind::Required:
            return Tr("Questionnaire.error.required");
        case QuestionnaireValidationErrorKind::Unanswered:
            return Tr("Questionnaire.error.optional");
        case QuestionnaireValidationErrorKind::Message:
            return error.message;
    }
    return {};
}

QuestionnaireError* QuestionnaireError::New(Ctx* cx,
                                            Entity<QuestionnaireState> state,
                                            Str item) {
    return NewPart<QuestionnaireError>(cx, state, item);
}

El* QuestionnaireError::IntoEl() {
    const QuestionnaireState* s = state.Get(cx->app);
    QuestionnaireItemState itemState;
    if (!s || !s->ItemState(item, &itemState)) {
        ReportUnknownItem(item);
        return nullptr;
    }
    const QuestionnaireValidationError* error = s->Error(item);
    if (!itemState.invalid || !error) {
        return nullptr;
    }
    const SemanticThemeTokens& tokens = Tokens(cx->app);
    UiSize resolved = QuestionnaireResolveSize(cx->app, this);
    El* e = SecondaryTextStyle(
        Div(a)
            ->Id(ElementId(cx, state, StrDup(a, fmt("error-%s", item))))
            ->Role(AccessibilityRole::Alert)
            ->MarginT(tokens.spacing.sm)
            ->Fg(tokens.colors.destructive),
        resolved, cx->app);
    RefineWith(e, this);
    if (children.len == 0) {
        e->Child(TextEl(a, StrDup(a, QuestionnaireErrorText(*error))));
    }
    return AddChildren(e, this);
}

// ─── actions ──────────────────────────────────────────────────────────────

QuestionnaireActions* QuestionnaireActions::New(
    Ctx* cx, Entity<QuestionnaireState> state) {
    return NewPart<QuestionnaireActions>(cx, state, {});
}

El* QuestionnaireActions::IntoEl() {
    QuestionnaireMetrics m =
        Metrics(QuestionnaireResolveSize(cx->app, this), cx->app);
    El* e = Div(a)
                ->Id(ElementId(cx, state, StrL("actions")))
                ->FlexRow()
                ->MinW(0)
                ->ItemsCenter()
                ->JustifyStart()
                ->Gap(m.choicesGap)
                ->W(kFill);
    return AddChildren(RefineWith(e, this), this);
}

enum class QuestionnaireAction : uint8_t {
    Previous,
    Skip,
    Next,
    Submit
};

static void ActionClick(QuestionnaireState* self, Ctx* cx, const ClickEvent*,
                        intptr_t action) {
    switch ((QuestionnaireAction)action) {
        case QuestionnaireAction::Previous:
            self->GoPrevious(cx);
            break;
        case QuestionnaireAction::Skip:
            self->SkipCurrent(cx);
            break;
        case QuestionnaireAction::Next:
            self->GoNext(cx);
            break;
        case QuestionnaireAction::Submit:
            self->Submit(cx);
            break;
    }
}

// questionnaire_action_part!: a button that is only there while the
// navigation state says its action is available.
static El* ActionPart(QuestionnairePart* part, QuestionnaireAction action,
                      const char* name, const char* translation, bool outline,
                      bool primary) {
    Ctx* cx = part->cx;
    const QuestionnaireState* s = part->state.Get(cx->app);
    if (!s) {
        return nullptr;
    }
    QuestionnaireNavigationState nav = s->NavigationState();
    bool visible = false;
    switch (action) {
        case QuestionnaireAction::Previous:
            visible = nav.previousVisible;
            break;
        case QuestionnaireAction::Skip:
            visible = nav.skipVisible;
            break;
        case QuestionnaireAction::Next:
            visible = nav.nextVisible;
            break;
        case QuestionnaireAction::Submit:
            visible = nav.submitVisible;
            break;
    }
    if (!visible) {
        return nullptr;
    }
    bool anchorsTrailingActions = action == QuestionnaireAction::Skip ||
                                  ((action == QuestionnaireAction::Next ||
                                    action == QuestionnaireAction::Submit) &&
                                   !nav.skipVisible);
    Button* button =
        Button::New(cx, ElementId(cx, part->state, Str(name)))
            ->WithSize(QuestionnaireResolveSize(cx->app, part))
            ->OnClick(ListenTo(part->state, &ActionClick, (intptr_t)action));
    if (outline) {
        button->Outline();
    }
    if (primary) {
        button->Primary();
    }
    if (part->children.len == 0) {
        button->Label(Tr(translation));
    }
    for (El* child : part->children) {
        button->Child(child);
    }
    El* e = button->IntoEl();
    if (anchorsTrailingActions) {
        e->MlAuto();
    }
    return RefineWith(e, part);
}

QuestionnairePrevious* QuestionnairePrevious::New(
    Ctx* cx, Entity<QuestionnaireState> state) {
    return NewPart<QuestionnairePrevious>(cx, state, {});
}

El* QuestionnairePrevious::IntoEl() {
    return ActionPart(this, QuestionnaireAction::Previous, "Previous",
                      "Questionnaire.previous", true, false);
}

QuestionnaireSkip* QuestionnaireSkip::New(Ctx* cx,
                                          Entity<QuestionnaireState> state) {
    return NewPart<QuestionnaireSkip>(cx, state, {});
}

El* QuestionnaireSkip::IntoEl() {
    return ActionPart(this, QuestionnaireAction::Skip, "Skip",
                      "Questionnaire.skip", true, false);
}

QuestionnaireNext* QuestionnaireNext::New(Ctx* cx,
                                          Entity<QuestionnaireState> state) {
    return NewPart<QuestionnaireNext>(cx, state, {});
}

El* QuestionnaireNext::IntoEl() {
    return ActionPart(this, QuestionnaireAction::Next, "Next",
                      "Questionnaire.next", false, true);
}

QuestionnaireSubmit* QuestionnaireSubmit::New(
    Ctx* cx, Entity<QuestionnaireState> state) {
    return NewPart<QuestionnaireSubmit>(cx, state, {});
}

El* QuestionnaireSubmit::IntoEl() {
    return ActionPart(this, QuestionnaireAction::Submit, "Submit",
                      "Questionnaire.submit", false, true);
}

} // namespace component
} // namespace gpui
