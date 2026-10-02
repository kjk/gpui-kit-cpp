#include "Story.h"

// The items each section lists, straight out of combobox_story.rs. A
// SearchableList keeps a pointer to them, so they are static rather than
// built on the frame arena.
static const component::SearchableItem kFrameworks[] = {
    {StrL("Next.js"), StrL("Next.js"), 0, false, IconName::None},
    {StrL("SvelteKit"), StrL("SvelteKit"), 0, false, IconName::None},
    {StrL("Nuxt.js"), StrL("Nuxt.js"), 0, false, IconName::None},
    {StrL("Remix"), StrL("Remix"), 0, false, IconName::None},
    {StrL("Astro"), StrL("Astro"), 0, false, IconName::None},
};
static const component::SearchableItem kMultiFrameworks[] = {
    {StrL("React"), StrL("React"), 0, false, IconName::None},
    {StrL("Nextjs"), StrL("Nextjs"), 0, false, IconName::None},
    {StrL("Angular"), StrL("Angular"), 0, false, IconName::None},
    {StrL("VueJS"), StrL("VueJS"), 0, false, IconName::None},
    {StrL("Django"), StrL("Django"), 0, false, IconName::None},
    {StrL("Astro"), StrL("Astro"), 0, false, IconName::None},
    {StrL("Remix"), StrL("Remix"), 0, false, IconName::None},
    {StrL("Svelte"), StrL("Svelte"), 0, false, IconName::None},
    {StrL("SolidJS"), StrL("SolidJS"), 0, false, IconName::None},
    {StrL("Qwik"), StrL("Qwik"), 0, false, IconName::None},
};
// food_groups(): three groups, with one item disabled in two of them.
static const Str kFoodGroups[] = {StrL("Fruits"), StrL("Vegetables"),
                                  StrL("Beverages")};
static const component::SearchableItem kFoods[] = {
    {StrL("Apples"), StrL("Apples"), 0, false, IconName::None},
    {StrL("Bananas"), StrL("Bananas"), 0, false, IconName::None},
    {StrL("Cherries"), StrL("Cherries"), 0, false, IconName::None},
    {StrL("Carrots"), StrL("Carrots"), 1, false, IconName::None},
    {StrL("Broccoli"), StrL("Broccoli"), 1, true, IconName::None},
    {StrL("Spinach"), StrL("Spinach"), 1, false, IconName::None},
    {StrL("Tea"), StrL("Tea"), 2, false, IconName::None},
    {StrL("Coffee"), StrL("Coffee"), 2, true, IconName::None},
    {StrL("Juice"), StrL("Juice"), 2, false, IconName::None},
};
static const component::SearchableItem kDisabledItems[] = {
    {StrL("Apples"), StrL("Apples"), 0, false, IconName::None},
    {StrL("Bananas"), StrL("Bananas"), 0, true, IconName::None},
    {StrL("Cherries"), StrL("Cherries"), 0, false, IconName::None},
    {StrL("Carrots"), StrL("Carrots"), 0, false, IconName::None},
    {StrL("Broccoli"), StrL("Broccoli"), 0, true, IconName::None},
};
// industries(): each row draws its own icon.
static const component::SearchableItem kIndustries[] = {
    {StrL("Information Technology"), StrL("Information Technology"), 0, false,
     IconName::Cpu},
    {StrL("Healthcare"), StrL("Healthcare"), 0, false, IconName::Heart},
    {StrL("Finance"), StrL("Finance"), 0, false, IconName::Globe},
    {StrL("Education"), StrL("Education"), 0, false, IconName::BookOpen},
    {StrL("Entertainment"), StrL("Entertainment"), 0, false, IconName::Star},
};
// PinnedDelegate: is_item_checked is true for the first two whatever the
// selection holds, and is_item_enabled is false for them.
static const component::SearchableItem kPinnedFrameworks[] = {
    {StrL("Next.js"), StrL("Next.js"), 0, false, IconName::None, true},
    {StrL("SvelteKit"), StrL("SvelteKit"), 0, false, IconName::None, true},
    {StrL("Nuxt.js"), StrL("Nuxt.js"), 0, false, IconName::None},
    {StrL("Remix"), StrL("Remix"), 0, false, IconName::None},
    {StrL("Astro"), StrL("Astro"), 0, false, IconName::None},
};
// FeaturedDelegate: render_item gives the first row a badge after its check.
static const component::SearchableItem kFeaturedFrameworks[] = {
    {StrL("Next.js"), StrL("Next.js"), 0, false, IconName::None, false,
     StrL("Featured")},
    {StrL("SvelteKit"), StrL("SvelteKit"), 0, false, IconName::None},
    {StrL("Nuxt.js"), StrL("Nuxt.js"), 0, false, IconName::None},
    {StrL("Remix"), StrL("Remix"), 0, false, IconName::None},
    {StrL("Astro"), StrL("Astro"), 0, false, IconName::None},
};
static const component::SearchableItem kUniversities[] = {
    {StrL("Harvard University"), StrL("Harvard University"), 0, false,
     IconName::None},
    {StrL("MIT"), StrL("MIT"), 0, false, IconName::None},
    {StrL("Stanford"), StrL("Stanford"), 0, false, IconName::None},
    {StrL("Cambridge"), StrL("Cambridge"), 0, false, IconName::None},
};

#define COMBO_COUNT(a) (int)(sizeof(a) / sizeof(a[0]))

// render_trigger: which of the story's five custom triggers a section uses,
// or the plain one the Select builds for itself.
enum class ComboTrigger : uint8_t {
    Default,
    Icon,     // the selected industry's icon before its title
    Palette,  // a palette glyph and the selection as a pill
    Badges,   // the first selection as a removable badge, then "+N"
    Overflow, // up to two bordered chips, then "+N more"
    Count     // a red count bubble and "frameworks selected"
};

// One combobox per section, in the order the Rust story renders them.
struct ComboSpec {
    const char* id;
    const char* title;
    const char* description;
    const char* placeholder;
    const component::SearchableItem* items;
    int count;
    const Str* groups;
    int nGroups;
    bool multiple;
    // Which items start selected, as a bit per index.
    unsigned selected;
    // check_icon(Icon::new(IconName::CircleCheck)).
    IconName checkIcon;
    ComboTrigger trigger;
    // on_will_change's cap, or 0 for none.
    int maxSelected;
    // Combobox::footer.
    bool footer;
};

static const ComboSpec kSpecs[] = {
    {"basic", "Default", "Search and choose one option.", "Select framework...",
     kFrameworks, COMBO_COUNT(kFrameworks), nullptr, 0, false, 0,
     IconName::Check, ComboTrigger::Default, 0, false},
    {"basic-multi", "Multiple", "Select more than one option.",
     "Select frameworks...", kFrameworks, COMBO_COUNT(kFrameworks), nullptr, 0,
     true, 0, IconName::Check, ComboTrigger::Default, 0, false},
    {"grouped", "Groups", "Organize results into groups.", "Select item...",
     kFoods, COMBO_COUNT(kFoods), kFoodGroups, 3, false, 1u << 0,
     IconName::Check, ComboTrigger::Default, 0, false},
    {"disabled-items", "Disabled items", "Keep unavailable options visible.",
     "Select item...", kDisabledItems, COMBO_COUNT(kDisabledItems), nullptr, 0,
     false, 0, IconName::Check, ComboTrigger::Default, 0, false},
    {"with-icon", "Icons", "Show icons in options and the trigger.",
     "Select industry category", kIndustries, COMBO_COUNT(kIndustries), nullptr,
     0, false, 0, IconName::Check, ComboTrigger::Icon, 0, false},
    {"custom-check", "Check icon", "Replace the default selection mark.",
     "Select framework...", kFrameworks, COMBO_COUNT(kFrameworks), nullptr, 0,
     false, 0, IconName::CircleCheck, ComboTrigger::Default, 0, false},
    {"with-footer", "Footer", "Add an action below the option list.",
     "Select university", kUniversities, COMBO_COUNT(kUniversities), nullptr, 0,
     false, 1u << 0, IconName::Check, ComboTrigger::Default, 0, true},
    {"custom-trigger", "Custom trigger", "Render custom trigger content.",
     "Select framework", kFrameworks, COMBO_COUNT(kFrameworks), nullptr, 0,
     false, 0, IconName::Check, ComboTrigger::Palette, 0, false},
    {"multi-badges", "Badges", "Show removable selected badges.",
     "Select frameworks", kMultiFrameworks, COMBO_COUNT(kMultiFrameworks),
     nullptr, 0, true, (1u << 0) | (1u << 2), IconName::Check,
     ComboTrigger::Badges, 0, false},
    {"custom-max2", "Maximum selections",
     "Limit how many items can be "
     "selected.",
     "Select up to 2 frameworks", kMultiFrameworks,
     COMBO_COUNT(kMultiFrameworks), nullptr, 0, true, 0, IconName::Check,
     ComboTrigger::Default, 2, false},
    {"pinned", "Pinned items", "Keep required items selected.",
     "Select framework...", kPinnedFrameworks, COMBO_COUNT(kPinnedFrameworks),
     nullptr, 0, false, 0, IconName::Check, ComboTrigger::Default, 0, false},
    {"featured", "Rich items", "Render supporting content in option rows.",
     "Select framework...", kFeaturedFrameworks,
     COMBO_COUNT(kFeaturedFrameworks), nullptr, 0, false, 0, IconName::Check,
     ComboTrigger::Default, 0, false},
    {"multi-expand", "Overflow", "Collapse selections after a visible limit.",
     "Select frameworks", kMultiFrameworks, COMBO_COUNT(kMultiFrameworks),
     nullptr, 0, true,
     (1u << 0) | (1u << 2) | (1u << 5) | (1u << 8) | (1u << 9), IconName::Check,
     ComboTrigger::Overflow, 0, false},
    {"multi-count", "Count", "Summarize selections as a count.",
     "Select frameworks", kMultiFrameworks, COMBO_COUNT(kMultiFrameworks),
     nullptr, 0, true, 0x3f, IconName::Check, ComboTrigger::Count, 0, false},
};
static const int kNSpecs = (int)(sizeof(kSpecs) / sizeof(kSpecs[0]));

struct ComboboxStory {
    // One list per combobox: the items, the query, the selection and whether
    // it is open are all its own.
    Entity<component::ComboboxState> combo[kNSpecs] = {};
    bool seeded = false;

    static El* Render(ComboboxStory* self, Ctx* cx);
};

static void ToggleCombo(ComboboxStory* self, Ctx* cx, const ClickEvent*,
                        int64_t which) {
    for (int i = 0; i < kNSpecs; i++) {
        component::ComboboxState* owner = self->combo[i].Get(cx);
        component::SearchableListState* s = owner ? owner->List() : nullptr;
        if (!s) {
            continue;
        }
        if (i == (int)which) {
            owner->SetOpen(!s->open, cx);
        } else {
            s->open = false;
        }
    }
    Notify(cx);
}
static void ClearCombo(ComboboxStory* self, Ctx* cx, const ClickEvent*,
                       int64_t which) {
    component::ComboboxState* s = self->combo[which].Get(cx);
    if (s) {
        s->ClearSelection(cx);
    }
}
// The badge trigger's ✕: remove_selected_index on the one it sits on, which
// is always the first of the selection here.
static void RemoveComboBadge(ComboboxStory* self, Ctx* cx, const ClickEvent*,
                             int64_t which) {
    component::ComboboxState* owner = self->combo[which].Get(cx);
    component::SearchableListState* s = owner ? owner->List() : nullptr;
    if (!s || s->selected.len == 0) {
        return;
    }
    for (int i = 0; i + 1 < s->selected.len; i++) {
        s->selected[i] = s->selected[i + 1];
    }
    s->selected.len--;
    owner->SyncSnapshot();
    Notify(cx);
}

// Caret::new(trigger.size()), which every custom trigger ends with.
static El* ComboCaret(Ctx* cx) {
    return IconEl(cx->a, IconName::ChevronDown, 16)
        ->Fg(ThemeNow(cx->app).mutedFg)
        ->Shrink0();
}

// A bordered chip, which the badge and overflow triggers are both made of.
static El* ComboChip(Ctx* cx, Str label, Rgba fg) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->FlexRow()
        ->ItemsCenter()
        ->Gap(2)
        ->PadX(4)
        ->Radius(th.radius * 0.5f)
        ->Border(1, th.border)
        ->MinW(0)
        ->Child(TextEl(a, label)->Font(12)->Fg(fg)->Truncate());
}

// render_trigger, one per shape the Rust story builds. Null leaves the
// Select to draw its own title and caret.
static El* ComboTriggerEl(ComboboxStory* self, Ctx* cx, int i) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const ComboSpec& spec = kSpecs[i];
    if (spec.trigger == ComboTrigger::Default) {
        return nullptr;
    }
    component::ComboboxState* owner = self->combo[i].Get(cx);
    component::SearchableListState* st = owner ? owner->List() : nullptr;
    int n = st ? st->selected.len : 0;
    El* row = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->Gap(8)->MinW(0);
    switch (spec.trigger) {
        case ComboTrigger::Icon: {
            // The selected industry's own icon, then its title.
            if (n == 1) {
                const component::SearchableItem& it =
                    spec.items[st->selected[0]];
                if (it.icon != IconName::None) {
                    row->Child(IconEl(a, it.icon, 16)->Fg(th.mutedFg));
                }
                row->Child(TextEl(a, it.title)
                               ->Font(14)
                               ->Fg(th.foreground)
                               ->Truncate());
            } else if (n > 1) {
                row->Child(TextEl(a, StoryFmt(cx, "%d selected", n))
                               ->Font(14)
                               ->Fg(th.foreground));
            } else {
                row->Child(TextEl(a, Str(spec.placeholder))
                               ->Font(14)
                               ->Fg(th.mutedFg)
                               ->Truncate());
            }
            row->Child(Div(a)->Flex1());
            row->Child(ComboCaret(cx));
            return row;
        }
        case ComboTrigger::Palette: {
            El* left = Div(a)->FlexRow()->ItemsCenter()->Gap(8)->MinW(0);
            left->Child(IconEl(a, IconName::Palette, 16)->Fg(th.primary));
            if (n > 0) {
                Str label = n == 1 ? spec.items[st->selected[0]].title
                                   : StoryFmt(cx, "%d selected", n);
                left->Child(Div(a)
                                ->PadX(8)
                                ->PadY(2)
                                ->Radius(99)
                                ->Bg(th.tokens.primary)
                                ->Child(TextEl(a, label)
                                            ->Font(12)
                                            ->Fg(th.primaryFg)
                                            ->LineHeight(1.4f)));
            } else {
                left->Child(
                    TextEl(a, Str(spec.placeholder))->Font(14)->Fg(th.mutedFg));
            }
            row->JustifyBetween()->Child(left)->Child(ComboCaret(cx));
            return row;
        }
        case ComboTrigger::Badges: {
            if (n == 0) {
                row->Child(TextEl(a, StrL("Select frameworks"))
                               ->Font(14)
                               ->Fg(th.mutedFg));
                return row;
            }
            // The first selection as a chip with a remove button, and a count
            // of whatever else is picked.
            El* left = Div(a)->FlexRow()->ItemsCenter()->Gap(4)->MinW(0);
            El* chip =
                ComboChip(cx, spec.items[st->selected[0]].title, th.foreground);
            chip->Child(component::Button::New(
                            cx, StoryFmt(cx, "remove-%d", st->selected[0]))
                            ->Ghost()
                            ->WithSize(UiSize::XSmall)
                            ->Icon(IconName::X)
                            ->OnClick(ListenerArg(Listen(cx, &RemoveComboBadge),
                                                  (int64_t)i))
                            ->IntoEl());
            left->Child(chip);
            if (n > 1) {
                left->Child(TextEl(a, StoryFmt(cx, "+%d", n - 1))
                                ->Font(12)
                                ->Fg(th.mutedFg)
                                ->Shrink0());
            }
            row->JustifyBetween()
                ->Child(left)
                ->Child(Div(a)->Shrink0()->Child(ComboCaret(cx)));
            return row;
        }
        case ComboTrigger::Overflow: {
            if (n == 0) {
                row->Child(TextEl(a, StrL("Select frameworks"))
                               ->Font(14)
                               ->Fg(th.mutedFg));
                return row;
            }
            const int kMaxShown = 2;
            row->FlexWrap()->Gap(4);
            for (int k = 0; k < n && k < kMaxShown; k++) {
                row->Child(ComboChip(cx, spec.items[st->selected[k]].title,
                                     th.foreground));
            }
            if (n > kMaxShown) {
                row->Child(ComboChip(
                    cx, StoryFmt(cx, "+%d more", n - kMaxShown), th.mutedFg));
            }
            return row;
        }
        default: {
            if (n == 0) {
                row->Child(TextEl(a, StrL("Select frameworks"))
                               ->Font(14)
                               ->Fg(th.mutedFg));
                return row;
            }
            row->Gap(6);
            row->Child(Div(a)
                           ->FlexRow()
                           ->JustifyCenter()
                           ->ItemsCenter()
                           ->MinW(16)
                           ->H(16)
                           ->PadX(4)
                           ->Radius(99)
                           ->Bg(th.red)
                           ->Child(TextEl(a, n > 99 ? StrL("99+")
                                                    : StoryFmt(cx, "%d", n))
                                       ->Font(10)
                                       ->Fg(Rgb(255, 255, 255))
                                       ->LineHeight(1.f)));
            row->Child(TextEl(a, StrL("frameworks selected"))
                           ->Font(14)
                           ->Fg(th.foreground));
            return row;
        }
    }
}

El* ComboboxStory::Render(ComboboxStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        for (int i = 0; i < kNSpecs; i++) {
            self->combo[i] = component::ComboboxState::New(cx->app);
            component::ComboboxState* owner = self->combo[i].Get(cx);
            component::SearchableListState* s = owner ? owner->List() : nullptr;
            if (!s) {
                continue;
            }
            owner->Searchable(true)->Multiple(kSpecs[i].multiple);
            for (int k = 0; k < kSpecs[i].count; k++) {
                if (kSpecs[i].selected & (1u << k)) {
                    VecAppend(s->selected, k);
                }
            }
            owner->SyncSnapshot();
        }
    }
    Listener toggle = Listen(cx, &ToggleCombo);
    Listener clear = Listen(cx, &ClearCombo);

    El* page = Div(a)->FlexCol()->Gap(16)->W(kFill)->ItemsCenter();
    for (int i = 0; i < kNSpecs; i++) {
        const ComboSpec& s = kSpecs[i];
        El* sec = StorySection(cx, s.title, s.description);
        StorySectionBody(sec)->W(280);
        component::Combobox* cb =
            component::Combobox::New(cx, Str(s.id), self->combo[i])
                ->Items(s.items, s.count)
                ->Placeholder(Str(s.placeholder))
                ->SearchPlaceholder(StrL("Search…"))
                ->CheckIcon(s.checkIcon)
                ->W(280)
                ->OnToggle(ListenerArg(toggle, (int64_t)i))
                ->OnClear(ListenerArg(clear, (int64_t)i));
        if (s.groups) {
            cb->Sections(s.groups, s.nGroups);
        }
        if (s.multiple) {
            cb->Multiple();
        }
        cb->MaxSelected(s.maxSelected);
        if (El* trig = ComboTriggerEl(self, cx, i)) {
            cb->Trigger(trig);
        }
        if (s.footer) {
            // footer(..): a ghost button under the list, full width and
            // left-aligned, which adds whatever the query says.
            cb->Footer(component::Button::New(cx, StrL("add-new"))
                           ->Ghost()
                           ->Label(StrL("New university"))
                           ->Icon(IconName::Plus)
                           ->IntoEl()
                           ->W(kFill)
                           ->JustifyStart());
        }
        StorySectionAdd(sec, cb->IntoEl());
        page->Child(sec);
    }

    // The last section reads back what each list holds.
    El* values =
        StorySection(cx, "Values", "Read selected values from each delegate.");
    El* valueCol = Div(a)->FlexCol()->W(kFill)->Gap(8);
    static const int kShown[] = {0, 2, 8, 13};
    for (int k = 0; k < 4; k++) {
        int i = kShown[k];
        component::ComboboxState* owner = self->combo[i].Get(cx);
        component::SearchableListState* s = owner ? owner->List() : nullptr;
        Str line = StoryFmt(cx, "%s: []", kSpecs[i].id);
        if (s && s->selected.len == 1) {
            line = StoryFmt(cx, "%s: [\"%s\"]", kSpecs[i].id,
                            kSpecs[i].items[s->selected[0]].title);
        } else if (s && s->selected.len > 1) {
            line =
                StoryFmt(cx, "%s: %d selected", kSpecs[i].id, s->selected.len);
        }
        valueCol->Child(StoryTxt(cx, line, 16, th.foreground));
    }
    StorySectionAdd(values, valueCol);
    page->Child(values);
    return page;
}

STORY_PAGE(StoryCombobox, ComboboxStory);
