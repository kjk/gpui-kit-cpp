#include "Story.h"

// settings_story.rs: three pages of groups and items over the story's
// AppSettings. The Settings component owns the sidebar, the search and the
// layout; what is left here is the values behind the fields and the fields
// that are the story's own (the custom elements and the URL buttons).
//
// Rust reaches AppSettings through getter/setter closures over a global; the
// typed fields here are handed the address of the value (or the list state)
// instead, and the story reads them back where it needs them.

// The dropdowns' options, in Rust's order. A SearchableList keeps a pointer
// to them, so they outlive the frame.
static const component::SearchableItem kGroupVariants[] = {
    {StrL("Normal"), StrL("normal"), 0, false},
    {StrL("Outline"), StrL("outline"), 0, false},
    {StrL("Fill"), StrL("fill"), 0, false},
};
static const component::GroupBoxVariant kGroupVariantValues[] = {
    component::GroupBoxVariant::Normal, component::GroupBoxVariant::Outline,
    component::GroupBoxVariant::Fill};
// GroupBoxVariant::Outline, the story's starting variant.
static const int kDefaultGroupVariant = 1;

static const component::SearchableItem kGroupSizes[] = {
    {StrL("Medium"), StrL("medium"), 0, false},
    {StrL("Small"), StrL("small"), 0, false},
    {StrL("XSmall"), StrL("xsmall"), 0, false},
};
static const UiSize kGroupSizeValues[] = {UiSize::Medium, UiSize::Small,
                                          UiSize::XSmall};

static const component::SearchableItem kFontFamilies[] = {
    {StrL("Arial"), StrL("Arial"), 0, false},
    {StrL("Helvetica"), StrL("Helvetica"), 0, false},
    {StrL("Times New Roman"), StrL("Times New Roman"), 0, false},
    {StrL("Courier New"), StrL("Courier New"), 0, false},
};

// The Density field's two choices; "Comfortable" is the default.
static const char* kDensities[] = {"Comfortable", "Compact"};

// OpenURLSettingField's and the other buttons' targets.
static const char* kUrls[] = {
    "https://gpui-kit.com/",
    "https://github.com/longbridge/gpui-kit",
    "https://docs.rs/gpui-component",
};
enum {
    kUrlWebsite,
    kUrlRepository,
    kUrlDocs
};

struct SettingsStory {
    // AppSettings, at AppSettings::default(). font_family, font_size,
    // line_height and cli_path live in their fields' own state.
    bool autoSwitchTheme = false;
    int density = 0;
    bool notificationsEnabled = true;
    bool autoUpdate = true;
    bool resettable = true;
    bool disabled = false;
    Entity<component::SearchableListState> fontFamily = {};
    Entity<component::SearchableListState> groupVariant = {};
    Entity<component::SearchableListState> groupSize = {};
    bool seeded = false;

    static El* Render(SettingsStory* self, Ctx* cx);
};

static void OpenUrlAt(SettingsStory*, Ctx*, const ClickEvent*, intptr_t ix) {
    OpenUrl(Str(kUrls[ix]));
}

// The Dark Mode switch is the theme's mode, as `cx.theme().mode.is_dark()`
// and `Theme::change` make it in Rust.
static void OnDarkMode(SettingsStory*, Ctx* cx, const ClickEvent*) {
    ThemeSet(cx->app, ThemeGet(cx->app) == ThemeMode::Dark ? ThemeMode::Light
                                                           : ThemeMode::Dark);
    Notify(cx);
}

static void ResetDarkMode(SettingsStory*, Ctx* cx, const ClickEvent*) {
    ThemeSet(cx->app, ThemeMode::Light);
    Notify(cx);
}

static void OnDensity(SettingsStory* self, Ctx* cx, const ClickEvent*,
                      intptr_t ix) {
    self->density = (int)ix;
    Notify(cx);
}

static void ResetDensity(SettingsStory* self, Ctx* cx, const ClickEvent*) {
    self->density = 0;
    Notify(cx);
}

static int SelectedIndex(Entity<component::SearchableListState> e, Ctx* cx,
                         int fallback) {
    component::SearchableListState* st = e.Get(cx);
    return st && st->selected.len > 0 ? st->selected[0] : fallback;
}

// OpenURLSettingField::render_field: an outline button of the field's size.
static El* OpenUrlButton(Ctx* cx, Str id, Str label, int url, UiSize size) {
    return component::Button::New(cx, id)
        ->Outline()
        ->Label(label)
        ->WithSize(size)
        ->OnClick(Listen(cx, &OpenUrlAt, (intptr_t)url))
        ->IntoEl();
}

El* SettingsStory::Render(SettingsStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->fontFamily =
            EntityNewState<component::SearchableListState>(cx->app);
        self->groupVariant =
            EntityNewState<component::SearchableListState>(cx->app);
        self->groupSize =
            EntityNewState<component::SearchableListState>(cx->app);
        if (auto* st = self->fontFamily.Get(cx)) {
            component::SearchableListSelectOnly(st, 0);
        }
        if (auto* st = self->groupVariant.Get(cx)) {
            component::SearchableListSelectOnly(st, kDefaultGroupVariant);
        }
        if (auto* st = self->groupSize.Get(cx)) {
            component::SearchableListSelectOnly(st, 0);
        }
    }
    component::GroupBoxVariant variant = kGroupVariantValues[SelectedIndex(
        self->groupVariant, cx, kDefaultGroupVariant)];
    UiSize size = kGroupSizeValues[SelectedIndex(self->groupSize, cx, 0)];
    bool disabled = self->disabled;
    bool dark = ThemeGet(cx->app) == ThemeMode::Dark;

    // SettingsStory::paddings() is 0: the Settings fills the story's pane.
    component::Settings* s = component::Settings::New(cx, StrL("app-settings"))
                                 ->WithSize(size)
                                 ->WithGroupVariant(variant);

    s->Page(StrL("General"), IconName::Settings2)
        ->PageResettable(self->resettable)
        ->PageDefaultOpen(true)
        ->PageTitleSuffix(
            component::Button::New(cx, StrL("help"))
                ->Icon(IconName::Info)
                ->Ghost()
                ->WithSize(UiSize::XSmall)
                ->OnClick(Listen(cx, &OpenUrlAt, (intptr_t)kUrlWebsite))
                ->IntoEl());

    s->Group(StrL("Appearance"));
    // SettingField::switch over the theme's mode, default_value(false). The
    // control is the story's own so a click goes to Theme::change; its reset
    // is the default_value's, spelled out.
    s->Item(StrL("Dark Mode"), StrL("Switch between light and dark themes."),
            component::Switch::New(cx, StrL("dark-mode"))
                ->Checked(dark)
                ->Disabled(disabled)
                ->WithSize(size)
                ->OnClick(Listen(cx, &OnDarkMode))
                ->IntoEl())
        ->Resettable(dark, Listen(cx, &ResetDarkMode))
        ->Disabled(disabled);
    s->Item(StrL("Auto Switch Theme"),
            StrL("Automatically switch theme based on system settings."))
        ->CheckboxField(&self->autoSwitchTheme, false, true)
        ->Disabled(disabled);
    // No default_value: Reset All leaves this switch alone.
    s->Item(StrL("resettable"),
            StrL("Enable/Disable reset button for settings."))
        ->SwitchField(&self->resettable)
        ->Disabled(disabled);
    s->Item(StrL("Group Variant"),
            StrL("Select the variant for setting groups."))
        ->DropdownField(self->groupVariant, kGroupVariants, 3,
                        kDefaultGroupVariant)
        ->Disabled(disabled);
    s->Item(StrL("Group Size"), StrL("Select the size for the setting group."))
        ->DropdownField(self->groupSize, kGroupSizes, 3, 0)
        ->Disabled(disabled);

    s->Group(StrL("Font"))
        ->GroupFooter(
            TextEl(a, StrL("Font preferences apply to this story only.")));
    s->Item(StrL("Font Family"), StrL("Select the font family for the story."))
        ->DropdownField(self->fontFamily, kFontFamilies, 4, 0)
        ->Disabled(disabled);
    // NumberFieldOptions { min, max, ..Default::default() }: a step of 1.
    s->Item(
         StrL("Font Size"),
         StrL("Adjust the font size for better readability between 8 and 72."))
        ->NumberField(StrL("14"), {8, 72, 1}, StrL("14"))
        ->Disabled(disabled);
    s->Item(
         StrL("Line Height"),
         StrL(
             "Adjust the line height for better readability between 8 and 32."))
        ->NumberField(StrL("12"), {8, 32, 1}, StrL("12"))
        ->Disabled(disabled);

    s->Group(StrL("Other"));
    s->Item(StrL("Disable Settings"), StrL("Lock the other settings."))
        ->SwitchField(&self->disabled, false, true);
    // Rust binds Foo to the same `disabled` value as its sibling.
    s->Item(StrL("Foo"), StrL("Find me by searching for my sibling"))
        ->SwitchField(&self->disabled, false, true)
        ->Keywords(StrL("Bar"));
    // SettingItem::render: the row is the story's own. It dims itself when
    // disabled, inside the item's own 0.5, as Rust's does.
    {
        El* row = Div(a)
                      ->FlexRow()
                      ->W(kFill)
                      ->JustifyBetween()
                      ->FlexWrap()
                      ->Gap(12)
                      ->Child(TextEl(a, StrL("View source, report issues, and "
                                             "follow project updates."))
                                  ->Wrap())
                      ->Child(component::Button::New(cx, StrL("action"))
                                  ->Icon(IconName::Globe)
                                  ->Label(StrL("Repository..."))
                                  ->Outline()
                                  ->WithSize(size)
                                  ->Disabled(disabled)
                                  ->OnClick(Listen(cx, &OpenUrlAt,
                                                   (intptr_t)kUrlRepository))
                                  ->IntoEl());
        if (disabled) {
            row->Opacity(0.5f);
        }
        s->ElementItem(row)->Disabled(disabled);
    }
    // SettingField::render with on_reset: a custom element keeps its own
    // state, so it says itself when it is dirty and how to reset.
    {
        El* choices = Div(a)->FlexRow()->Gap(4);
        for (int i = 0; i < 2; i++) {
            component::Button* b =
                component::Button::New(cx, Str(kDensities[i]))
                    ->Label(Str(kDensities[i]))
                    ->WithSize(size)
                    ->OnClick(Listen(cx, &OnDensity, (intptr_t)i));
            if (self->density == i) {
                b->Primary();
            } else {
                b->Outline();
            }
            choices->Child(b->IntoEl());
        }
        s->Item(StrL("Density"),
                StrL("A custom element field with reset support via "
                     "`on_reset`."),
                choices)
            ->Resettable(self->density != 0, Listen(cx, &ResetDensity))
            ->Disabled(disabled);
    }
    // layout(Axis::Vertical). Rust's string continuations drop the leading
    // whitespace of the next line, which is why "title,description" has no
    // space.
    s->Item(StrL("CLI Path"),
            StrL("Path to the CLI executable. \nThis item uses Vertical "
                 "layout. The title,description, and field are all aligned "
                 "vertically with width 100%."))
        ->InputField(StrL("/usr/local/bin/bash"), StrL("/usr/local/bin/bash"))
        ->Layout(Axis::Vertical)
        ->Disabled(disabled);

    s->Page(StrL("Software Update"), IconName::Cpu)
        ->PageResettable(self->resettable);
    s->Group(StrL("Updates"));
    s->Item(StrL("Enable Notifications"),
            StrL("Receive notifications about updates and news."))
        ->SwitchField(&self->notificationsEnabled, true, true)
        ->Disabled(disabled);
    s->Item(StrL("Auto Update"),
            StrL("Automatically download and install updates."))
        ->SwitchField(&self->autoUpdate, true, true)
        ->Disabled(disabled);

    s->Page(StrL("About"), IconName::Info)->PageResettable(self->resettable);
    // An untitled group of one custom element.
    s->Group({});
    s->ElementItem(
        Div(a)
            ->FlexCol()
            ->Gap(12)
            ->W(kFill)
            ->ItemsCenter()
            ->JustifyCenter()
            ->Child(IconEl(a, IconName::GalleryVerticalEnd, 64))
            ->Child(TextEl(a, StrL("GPUI Component")))
            ->Child(TextEl(a, StrL("Rust GUI components for building fantastic "
                                   "cross-platform desktop application by "
                                   "using GPUI."))
                        ->Font(14)
                        ->Fg(th.mutedFg)
                        ->Wrap()));
    s->Group(StrL("Links"));
    s->Item(StrL("GitHub Repository"),
            StrL("Open the GitHub repository in your default browser."),
            OpenUrlButton(cx, StrL("open-url-repo"), StrL("Repository..."),
                          kUrlRepository, size));
    // description(markdown(..)): the search reads the source text. The
    // TextView names its own foreground on its root (text_view.rs
    // `.text_color(text_view_style.foreground())`), so the description's
    // muted colour stops at it, as it does in Rust.
    s->Item(StrL("Documentation"),
            StrL("Rust doc for the `gpui-component` crate."),
            OpenUrlButton(cx, StrL("open-url-docs"), StrL("Rust Docs..."),
                          kUrlDocs, size))
        ->DescriptionEl(
            component::Markdown(
                cx, StrL("Rust doc for the `gpui-component` crate."))
                ->Font(14)
                ->IntoEl());
    s->Item(StrL("Website"),
            StrL("Official website and documentation for the GPUI Component."),
            OpenUrlButton(cx, StrL("open-url-site"), StrL("Website..."),
                          kUrlWebsite, size));

    return s->IntoEl();
}

STORY_PAGE(StorySettings, SettingsStory);
