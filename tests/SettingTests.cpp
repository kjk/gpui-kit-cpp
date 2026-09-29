/* Ported from crates/ui/src/setting/item.rs, settings.rs and tests.rs.
 *
 * `SettingItem::is_match` is what the search box filters on: the title, the
 * description and the keywords, all lowercased. SettingsFilter keeps original
 * page and group indexes and remaps selection onto the pages that still match.
 */

#include "Test.h"

using namespace gpui::component;

static SettingItem Item(const char* title, const char* desc) {
    SettingItem it;
    it.title = Str(title);
    it.description = Str(desc);
    return it;
}

static void TheQueryMatchesTitleDescriptionAndKeywords() {
    Arena* a = ArenaNew();
    SettingItem it = Item("Dark Mode", "Switch between light and dark themes.");
    it.keywords.Append(a, StrL("appearance"));

    // An empty query matches everything, which is what an unfiltered list is.
    utassert(SettingItemMatches(&it, StrL("")));
    // The title, in any case.
    utassert(SettingItemMatches(&it, StrL("dark")));
    utassert(SettingItemMatches(&it, StrL("DARK")));
    utassert(SettingItemMatches(&it, StrL("Mode")));
    // The description.
    utassert(SettingItemMatches(&it, StrL("themes")));
    // And the keywords, which is the whole point of having them: nothing the
    // item shows says "appearance".
    utassert(SettingItemMatches(&it, StrL("APPEAR")));
    // Anything else does not.
    utassert(!SettingItemMatches(&it, StrL("font")));
    // A query longer than what it is matched against cannot be in it.
    utassert(!SettingItemMatches(&it, StrL("Dark Mode and then some")));
    ArenaDelete(a);
}

static void AGroupIsShownWhenAnythingInItIs() {
    Arena* a = ArenaNew();
    SettingGroup g;
    g.title = StrL("Appearance");
    g.items.Append(a, Item("Dark Mode", "Switch between themes."));
    g.items.Append(a, Item("Auto Switch", "Follow the system."));

    utassert(SettingGroupMatches(&g, StrL("")));
    utassert(SettingGroupMatches(&g, StrL("auto")));
    utassert(SettingGroupMatches(&g, StrL("system")));
    // Nothing in it matches, so the group goes — and Rust drops its header
    // and footer with it.
    utassert(!SettingGroupMatches(&g, StrL("font")));
    // An empty group has nothing to match.
    SettingGroup empty;
    utassert(!SettingGroupMatches(&empty, StrL("dark")));
    utassert(SettingGroupMatches(&empty, StrL("")));
    ArenaDelete(a);
}

static void APageIsShownWhenAnyGroupIs() {
    Arena* a = ArenaNew();
    SettingPage p;
    p.title = StrL("General");
    SettingGroup appearance;
    appearance.title = StrL("Appearance");
    appearance.items.Append(a, Item("Dark Mode", "Switch between themes."));
    SettingGroup font;
    font.title = StrL("Font");
    font.items.Append(a, Item("Font Size", "How big the text is."));
    p.groups.Append(a, appearance);
    p.groups.Append(a, font);

    utassert(SettingPageMatches(&p, StrL("dark")));
    utassert(SettingPageMatches(&p, StrL("font")));
    // The page's own title is not what a search matches on; the items are.
    utassert(!SettingPageMatches(&p, StrL("general")));
    utassert(!SettingPageMatches(&p, StrL("network")));
    ArenaDelete(a);
}

struct BoolSettingTarget {
    bool value = false;
    int sets = 0;
    bool customDirty = false;
    int customResets = 0;
};

static bool GetBoolSetting(void* user, const App*) {
    return ((BoolSettingTarget*)user)->value;
}

static void SetBoolSetting(void* user, bool value, App*) {
    BoolSettingTarget* target = (BoolSettingTarget*)user;
    target->value = value;
    target->sets++;
}

static bool IsCustomSettingDirty(void* user, const App*) {
    return ((BoolSettingTarget*)user)->customDirty;
}

static void ResetCustomSetting(void* user, Ctx*) {
    ((BoolSettingTarget*)user)->customResets++;
}

static void TypedFieldsRetainSourceResetSemanticsWithoutRtti() {
    BoolSettingTarget target;
    SettingField<bool> field = SettingField<bool>::New(
        SettingFieldType::Switch, &target, GetBoolSetting, SetBoolSetting);
    utassert(field.fieldType == SettingFieldType::Switch);
    utassert(!field.IsResettable(nullptr));

    field.DefaultValue(false);
    utassert(!field.IsResettable(nullptr));
    target.value = true;
    utassert(field.IsResettable(nullptr));

    AnySettingField any = EraseSettingField(&field);
    utassert(any.IsValid());
    utassert(any.typeId == SettingFieldTypeOf<bool>());
    utassert(any.fieldType == SettingFieldType::Switch);
    utassert(any.IsResettable(nullptr));
    any.Reset(nullptr);
    utassert(!target.value && target.sets == 1);

    field.OnReset(IsCustomSettingDirty, ResetCustomSetting);
    target.customDirty = false;
    utassert(!any.IsResettable(nullptr));
    target.customDirty = true;
    utassert(any.IsResettable(nullptr));
    any.Reset(nullptr);
    utassert(target.customResets == 1 && target.sets == 1);

    SettingField<Str> element = SettingField<Str>::New(
        SettingFieldType::Element, nullptr, nullptr, nullptr);
    element.DefaultValue(StrL("unused"));
    utassert(!element.IsResettable(nullptr));
    utassert(SettingFieldTypeOf<Str>() != SettingFieldTypeOf<bool>());
}

struct FieldElementCapture {
    RenderOptions options = {};
    int calls = 0;
};

static El* CaptureFieldOptions(void* user, const RenderOptions* options, Ctx*) {
    FieldElementCapture* capture = (FieldElementCapture*)user;
    capture->options = *options;
    capture->calls++;
    return nullptr;
}

static void RenderOptionsNarrowCopiesAndReachCustomFields() {
    RenderOptions base = RenderOptions::New();
    RenderOptions item = base.WithPageIx(2)
                             .WithGroupIx(3)
                             .WithItemIx(4)
                             .WithSize(UiSize::Large)
                             .WithGroupVariant(GroupBoxVariant::Outline)
                             .WithLayout(Axis::Vertical)
                             .WithDisabled(true);
    utassert(base.pageIx == 0 && base.layout == Axis::Horizontal &&
             !base.disabled && base.size == UiSize::Medium);
    utassert(item.pageIx == 2 && item.groupIx == 3 && item.itemIx == 4);
    utassert(item.size == UiSize::Large &&
             item.groupVariant == GroupBoxVariant::Outline);
    utassert(item.layout == Axis::Vertical && item.disabled);

    FieldElementCapture capture;
    SettingFieldElement element = {&capture, CaptureFieldOptions};
    utassert(element.IsValid());
    utassert(element.Render(&item, nullptr) == nullptr);
    utassert(capture.calls == 1 && capture.options.pageIx == 2 &&
             capture.options.itemIx == 4 && capture.options.disabled);

    SelectIndex selected;
    utassert(selected.pageIx == 0 && selected.groupIx == -1);
    selected = {3, 2};
    utassert(selected.pageIx == 3 && selected.groupIx == 2);
}

static El* FindSettingElement(El* root, const char* id) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && StrEqI(root->id, id)) {
        return root;
    }
    for (El* child = root->first; child; child = child->next) {
        if (El* found = FindSettingElement(child, id)) {
            return found;
        }
    }
    return nullptr;
}

static void NumberSettingsDelegateStepAndRangeToTheInputEngine() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Entity<SettingsState> state = EntityNewState<SettingsState>(&app);
    Ctx cx = {&app, win, arena, {}};

    NumberFieldOptions options;
    options.min = 100;
    options.max = 900;
    options.step = 100;
    El* root = Settings::New(&cx, StrL("number-settings"), state)
                   ->Page(StrL("Editor"))
                   ->Group(StrL("Font"))
                   ->Item(StrL("Weight"), StrL("Font weight"))
                   ->NumberField(StrL("400"), options)
                   ->IntoEl();
    SettingsState* settings = state.Get(&app);
    InputState* input = settings && settings->fields.len == 1
                            ? settings->fields[0].input
                            : nullptr;
    El* increment = FindSettingElement(root, "increment");
    utassert(input && increment && increment->onClick.IsValid());
    if (input && increment) {
        increment->onClick.Call();
        utassert(StrEqI(InputValue(input), "500"));
        utassert(input->numberHasMin && input->numberMin == 100);
        utassert(input->numberHasMax && input->numberMax == 900);

        // A new range is refreshed by the facade. "1" is not clamped while
        // typing the next digit; the completed "12" survives blur.
        component::NumberInput::New(&cx, StrL("size"), input)
            ->Min(6)
            ->Max(48)
            ->IntoEl();
        InputSetValue(input, StrL("1"));
        utassert(StrEqI(InputValue(input), "1"));
        InputSetValue(input, StrL("12"));
        InputBlur(input, &app, win);
        utassert(StrEqI(InputValue(input), "12"));
        InputSetValue(input, StrL("3"));
        InputBlur(input, &app, win);
        utassert(StrEqI(InputValue(input), "6"));
    }

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

static SettingItem KeywordItem(Arena* a, const char* keyword) {
    SettingItem it;
    it.title = StrL("Setting");
    it.keywords.Append(a, Str(keyword));
    return it;
}

static void AppendKeywordGroup(Arena* a, SettingPage* page, const char* title,
                               const char* k1, const char* k2 = nullptr) {
    SettingGroup g;
    if (title && title[0]) {
        g.title = Str(title);
    }
    g.items.Append(a, KeywordItem(a, k1));
    if (k2) {
        g.items.Append(a, KeywordItem(a, k2));
    }
    page->groups.Append(a, g);
}

static void FillSearchFixture(Arena* a, ArenaVec<SettingPage>& pages) {
    SettingPage general;
    general.title = StrL("General");
    AppendKeywordGroup(a, &general, "", "language");
    pages.Append(a, general);

    SettingPage appearance;
    appearance.title = StrL("Appearance");
    AppendKeywordGroup(a, &appearance, "", "unrelated");
    AppendKeywordGroup(a, &appearance, "Colors", "theme colors");
    AppendKeywordGroup(a, &appearance, "Fonts", "unrelated", "theme font");
    pages.Append(a, appearance);

    SettingPage editor;
    editor.title = StrL("Editor");
    AppendKeywordGroup(a, &editor, "", "theme editor");
    pages.Append(a, editor);
}

static void SearchPreservesPageIdentityAndFallsBackToTheFirstMatch() {
    Arena* a = ArenaNew();
    ArenaVec<SettingPage> pages;
    FillSearchFixture(a, pages);
    SelectIndex selected;
    selected.pageIx = 1;
    selected.groupIx = -1;

    // The old numeric index is still in range, but would point to Editor.
    SelectIndex got =
        SettingsResolveSelectedIndex(pages, StrL("theme"), selected);
    utassert(got.pageIx == 1 && got.groupIx == -1);

    got = SettingsResolveSelectedIndex(pages, StrL("font"), selected);
    utassert(got.pageIx == 1 && got.groupIx == -1);

    selected.pageIx = 2;
    got = SettingsResolveSelectedIndex(pages, StrL(""), selected);
    utassert(got.pageIx == 2 && got.groupIx == -1);

    // The current page disappears: choose the first matching page.
    got = SettingsResolveSelectedIndex(pages, StrL("font"), selected);
    utassert(got.pageIx == 1 && got.groupIx == -1);

    // No results keep the selection so clearing the query can restore it.
    selected.pageIx = 1;
    got = SettingsResolveSelectedIndex(pages, StrL("no matching setting"),
                                       selected);
    utassert(got.pageIx == 1 && got.groupIx == -1);
    got = SettingsResolveSelectedIndex(pages, StrL(""), selected);
    utassert(got.pageIx == 1 && got.groupIx == -1);
    ArenaDelete(a);
}

static void SearchPreservesGroupIdentityUntilTheGroupDisappears() {
    Arena* a = ArenaNew();
    ArenaVec<SettingPage> pages;
    FillSearchFixture(a, pages);
    SelectIndex selected;
    selected.pageIx = 1;
    selected.groupIx = 2;

    SelectIndex got =
        SettingsResolveSelectedIndex(pages, StrL("theme"), selected);
    utassert(got.pageIx == 1 && got.groupIx == 2);
    got = SettingsResolveSelectedIndex(pages, StrL("font"), selected);
    utassert(got.pageIx == 1 && got.groupIx == 2);
    got = SettingsResolveSelectedIndex(pages, StrL(""), selected);
    utassert(got.pageIx == 1 && got.groupIx == 2);
    got = SettingsResolveSelectedIndex(pages, StrL("colors"), selected);
    utassert(got.pageIx == 1 && got.groupIx == -1);
    ArenaDelete(a);
}

static void ResettingSearchResultsLeavesHiddenSettingsUnchanged() {
    Arena* a = ArenaNew();
    SettingGroup group;
    SettingItem visible = KeywordItem(a, "theme");
    visible.dirty = false;
    SettingItem hidden = KeywordItem(a, "hidden");
    hidden.dirty = true;
    hidden.onReset.fn = 1;
    group.items.Append(a, visible);
    group.items.Append(a, hidden);

    utassert(!SettingGroupIsResettable(&group, StrL("theme")));
    group.items[0].dirty = true;
    group.items[0].onReset.fn = 1;
    utassert(SettingGroupIsResettable(&group, StrL("theme")));
    utassert(!SettingGroupIsResettable(&group, StrL("missing")));
    ArenaDelete(a);
}

static Settings* SearchTestSettings(Ctx* cx, Entity<SettingsState> state) {
    return Settings::New(cx, StrL("search-test"), state)
        ->Page(StrL("General"))
        ->Group({})
        ->Item(StrL("Language"), {})
        ->Keywords(StrL("language"))
        ->Page(StrL("Appearance"))
        ->Group({})
        ->Item(StrL("Unrelated"), {})
        ->Keywords(StrL("unrelated"))
        ->Group(StrL("Colors"))
        ->Item(StrL("Colors"), {})
        ->Keywords(StrL("theme colors"))
        ->Group(StrL("Fonts"))
        ->Item(StrL("Unrelated"), {})
        ->Keywords(StrL("unrelated"))
        ->Item(StrL("Font"), {})
        ->Keywords(StrL("theme font"))
        ->Page(StrL("Editor"))
        ->Group({})
        ->Item(StrL("Editor"), {})
        ->Keywords(StrL("theme editor"))
        ->DefaultSelectedIndex({1, -1});
}

static void SearchRenderKeepsOriginalItemIndexes() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Entity<SettingsState> state = EntityNewState<SettingsState>(&app);
    Ctx cx = {&app, win, arena, {}};

    SearchTestSettings(&cx, state)->IntoEl();
    SettingsState* settings = state.Get(&app);
    utassert(settings && settings->page == 1 && settings->group == -1);
    El* root = nullptr;

    InputSetValue(&settings->search, StrL("theme"));
    root = SearchTestSettings(&cx, state)->IntoEl();
    utassert(settings->page == 1 && settings->group == -1);
    utassert(FindSettingElement(root, "1-1-0"));
    utassert(!FindSettingElement(root, "2-0-0"));

    InputSetValue(&settings->search, StrL("font"));
    root = SearchTestSettings(&cx, state)->IntoEl();
    utassert(settings->page == 1);
    utassert(FindSettingElement(root, "1-2-1"));

    InputSetValue(&settings->search, StrL("theme"));
    root = SearchTestSettings(&cx, state)->IntoEl();
    SettingsState::OnPageClick(settings, &cx, nullptr, 2);
    root = SearchTestSettings(&cx, state)->IntoEl();
    utassert(settings->page == 2);
    utassert(FindSettingElement(root, "2-0-0"));

    InputSetValue(&settings->search, StrL(""));
    root = SearchTestSettings(&cx, state)->IntoEl();
    utassert(settings->page == 2);
    utassert(FindSettingElement(root, "2-0-0"));

    InputSetValue(&settings->search, StrL("font"));
    root = SearchTestSettings(&cx, state)->IntoEl();
    utassert(settings->page == 1);
    utassert(FindSettingElement(root, "1-2-1"));

    InputSetValue(&settings->search, StrL("no matching setting"));
    root = SearchTestSettings(&cx, state)->IntoEl();
    utassert(settings->page == 1);
    utassert(!FindSettingElement(root, "1-2-1"));

    InputSetValue(&settings->search, StrL(""));
    root = SearchTestSettings(&cx, state)->IntoEl();
    utassert(settings->page == 1);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

static El* FindChild(El* root, const El* wanted) {
    if (!root) {
        return nullptr;
    }
    if (root == wanted) {
        return root;
    }
    for (El* child = root->first; child; child = child->next) {
        if (El* found = FindChild(child, wanted)) {
            return found;
        }
    }
    return nullptr;
}

// footer_follows_group_search_visibility: the footer goes with its group and
// never makes the group match on its own.
static void FooterFollowsGroupSearchVisibility() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Entity<SettingsState> state = EntityNewState<SettingsState>(&app);
    Ctx cx = {&app, win, arena, {}};
    auto render = [&](El** footer) {
        *footer = TextEl(arena, StrL("Changes apply to this device only."));
        Settings* s = SearchTestSettings(&cx, state);
        // The Fonts group, which is groups[2] on the Appearance page.
        s->pages[1].groups[2].footer = *footer;
        return s->IntoEl();
    };
    El* footer = nullptr;
    render(&footer);
    SettingsState* settings = state.Get(&app);

    InputSetValue(&settings->search, StrL("font"));
    El* root = render(&footer);
    utassert(FindSettingElement(root, "1-2-1"));
    utassert(FindChild(root, footer));

    // Footer copy does not independently make a group match the query.
    InputSetValue(&settings->search, StrL("colors"));
    root = render(&footer);
    utassert(FindSettingElement(root, "1-1-0"));
    utassert(!FindChild(root, footer));

    InputSetValue(&settings->search, StrL("font"));
    root = render(&footer);
    utassert(FindChild(root, footer));
    utassert(settings->page == 1 && settings->group == -1);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

static void ResetAllOnSearchResultsLeavesHiddenSettingsUnchanged() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Entity<SettingsState> state = EntityNewState<SettingsState>(&app);
    Ctx cx = {&app, win, arena, {}};

    bool visible = true;
    bool hidden = true;
    SettingsState* settings = state.Get(&app);
    utassert(settings);
    InputSetValue(&settings->search, StrL("theme"));
    Settings::New(&cx, StrL("reset-search"), state)
        ->Page(StrL("Appearance"))
        ->Group(StrL("Theme"))
        ->Item(StrL("Visible"), {})
        ->Keywords(StrL("theme"))
        ->SwitchField(&visible, false, true)
        ->Item(StrL("Hidden"), {})
        ->Keywords(StrL("hidden"))
        ->SwitchField(&hidden, false, true)
        ->IntoEl();
    utassert(settings->fields.len == 1);
    SettingsState::OnResetPage(settings, &cx, nullptr, 0);
    utassert(!visible);
    utassert(hidden);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

static El* FindParentOf(El* root, const El* wanted) {
    if (!root) {
        return nullptr;
    }
    for (El* child = root->first; child; child = child->next) {
        if (child == wanted) {
            return root;
        }
        if (El* found = FindParentOf(child, wanted)) {
            return found;
        }
    }
    return nullptr;
}

// group_variant_overrides_the_settings_default: with a bordered (Outline)
// default, a group that says Normal presents its items directly -- no border
// and no padding on its surface -- while the group beside it keeps the card.
static void GroupVariantOverridesTheSettingsDefault() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Entity<SettingsState> state = EntityNewState<SettingsState>(&app);
    Ctx cx = {&app, win, arena, {}};
    El* root = Settings::New(&cx, StrL("variant-test"), state)
                   ->Bordered(true)
                   ->Page(StrL("General"))
                   ->Group(StrL("First"))
                   ->GroupVariant(GroupBoxVariant::Normal)
                   ->Item(StrL("plain"), StrL("A plain item"))
                   ->Group(StrL("Second"))
                   ->Item(StrL("outlined"), StrL("An outlined item"))
                   ->IntoEl();
    El* plain = FindSettingElement(root, "0-0-0");
    El* outlined = FindSettingElement(root, "0-1-0");
    El* plainCard = FindParentOf(root, plain);
    El* outlinedCard = FindParentOf(root, outlined);
    utassert(plainCard && outlinedCard);
    if (plainCard && outlinedCard) {
        utassertnear(plainCard->style.border, 0.f);
        utassertnear(plainCard->style.pad.top, 0.f);
        utassertnear(outlinedCard->style.border, 1.f);
        utassertnear(outlinedCard->style.pad.top, 16.f);
    }
    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// The search fixture with Colors made 900 tall, which pushes Fonts below a
// 700-high pane.
static Settings* TallColorsSettings(Ctx* cx, Entity<SettingsState> state) {
    return Settings::New(cx, StrL("scroll-test"), state)
        ->H(700)
        ->Page(StrL("General"))
        ->Group({})
        ->Item(StrL("Language"), {})
        ->Page(StrL("Appearance"))
        ->Group({})
        ->Item(StrL("Unrelated"), {})
        ->Group(StrL("Colors"))
        ->Item(StrL("theme colors"), {}, Div(cx->a)->W(40)->H(900))
        ->Group(StrL("Fonts"))
        ->Item(StrL("Unrelated"), {})
        ->Item(StrL("Font"), {})
        ->DefaultSelectedIndex({1, -1});
}

// Build, lay out and paint the page's scroll hook, which is where a jump to a
// group resolves once its place is known.
static El* DrawTallColors(Ctx* cx, Window* win, Entity<SettingsState> state) {
    El* root = TallColorsSettings(cx, state)->IntoEl();
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    LayoutEl(&win->paint, root, 0, 0, 900, 700, th.fontSize, th.foreground);
    El* body = FindSettingElement(root, "page-body");
    if (body && body->customPaint) {
        body->customPaint(&win->paint, body, body->customUser);
    }
    return root;
}

// tests.rs: selecting_a_group_from_another_page_scrolls_to_it. Leaving the
// page drops its scroll state; the jump back must not land at the top.
static void SelectingAGroupFromAnotherPageScrollsToIt() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Entity<SettingsState> state = EntityNewState<SettingsState>(&app);
    Ctx cx = {&app, win, arena, {}};
    DrawTallColors(&cx, win, state);
    SettingsState* settings = state.Get(&app);
    utassert(settings && settings->page == 1);

    SettingsState::OnPageClick(settings, &cx, nullptr, 0);
    DrawTallColors(&cx, win, state);
    SettingsState::OnGroupClick(settings, &cx, nullptr, 1 * 64 + 2);
    DrawTallColors(&cx, win, state);
    utassert(settings->scrollY > 0);
    El* root = DrawTallColors(&cx, win, state);
    El* target = FindSettingElement(root, "1-2-1");
    utassert(target);
    if (target) {
        utassert(target->y >= 0 && target->y + target->h <= 700);
    }

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

void TestSetting() {
    TestSuite("setting");
    TheQueryMatchesTitleDescriptionAndKeywords();
    AGroupIsShownWhenAnythingInItIs();
    APageIsShownWhenAnyGroupIs();
    TypedFieldsRetainSourceResetSemanticsWithoutRtti();
    RenderOptionsNarrowCopiesAndReachCustomFields();
    NumberSettingsDelegateStepAndRangeToTheInputEngine();
    SearchPreservesPageIdentityAndFallsBackToTheFirstMatch();
    SearchPreservesGroupIdentityUntilTheGroupDisappears();
    ResettingSearchResultsLeavesHiddenSettingsUnchanged();
    SearchRenderKeepsOriginalItemIndexes();
    FooterFollowsGroupSearchVisibility();
    ResetAllOnSearchResultsLeavesHiddenSettingsUnchanged();
    GroupVariantOverridesTheSettingsDefault();
    SelectingAGroupFromAnotherPageScrollsToIt();
}
