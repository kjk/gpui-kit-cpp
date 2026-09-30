#include "Story.h"

// The pages the Rust story builds: a sidebar of pages, groups inside the
// active one, and an item per setting. The component owns all of that now —
// what is left here is the values behind the fields and the fields
// themselves, which is what the Rust story's `SettingField`s stand for.
// The two dropdowns' items. A SearchableList keeps a pointer to them, so
// they outlive the frame.
static const component::SearchableItem kFontFamilies[] = {
    {StrL("Arial"), StrL("arial"), 0, false},
    {StrL("Consolas"), StrL("consolas"), 0, false},
    {StrL("Segoe UI"), StrL("segoe"), 0, false},
};
static const component::SearchableItem kGroupVariants[] = {
    {StrL("Outline"), StrL("outline"), 0, false},
    {StrL("Fill"), StrL("fill"), 0, false},
    {StrL("Plain"), StrL("plain"), 0, false},
};
static const component::SearchableItem kGroupSizes[] = {
    {StrL("Medium"), StrL("medium"), 0, false},
    {StrL("Small"), StrL("small"), 0, false},
    {StrL("XSmall"), StrL("xsmall"), 0, false},
};

struct SettingsStory {
    bool darkMode = false;
    bool autoSwitch = false;
    bool resettable = true;
    Entity<component::SearchableListState> fontFamily = {};
    Entity<component::SearchableListState> groupVariant = {};
    Entity<component::SearchableListState> groupSize = {};
    // AppSettings::disabled: the Other group's first switch locks the rest.
    bool disabled = false;
    bool foo = false;
    bool autoUpdates = true;
    bool seeded = false;

    static El* Render(SettingsStory* self, Ctx* cx);
};

// cx.open_url("https://gpui-kit.com/").
static void OpenDocs(SettingsStory*, Ctx*, const ClickEvent*) {
    OpenUrl(StrL("https://gpui-kit.com/"));
}

El* SettingsStory::Render(SettingsStory* self, Ctx* cx) {
    Arena* a = cx->a;
    if (!self->seeded) {
        self->seeded = true;
        self->fontFamily =
            EntityNewState<component::SearchableListState>(cx->app);
        self->groupVariant =
            EntityNewState<component::SearchableListState>(cx->app);
        self->groupSize =
            EntityNewState<component::SearchableListState>(cx->app);
        for (Entity<component::SearchableListState> e :
             {self->fontFamily, self->groupVariant, self->groupSize}) {
            if (component::SearchableListState* st = e.Get(cx)) {
                component::SearchableListSelectOnly(st, 0);
            }
        }
    }
    // with_group_variant: the story opens on Outline, its dropdown's first
    // choice.
    component::Settings* s = component::Settings::New(cx, StrL("settings"))
                                 ->Bordered(true)
                                 ->SidebarWidth(200)
                                 ->H(WindowSize(cx->win).dipH - 160);

    // SettingPage::resettable: the story's own switch, which turns the reset
    // buttons on this page off.
    s->Page(StrL("General"), IconName::Settings2)
        ->PageDefaultOpen(true)
        ->PageResettable(self->resettable)
        // title_suffix: a ghost Info button that opens the docs.
        ->PageTitleSuffix(component::Button::New(cx, StrL("help"))
                              ->Icon(IconName::Info)
                              ->Ghost()
                              ->WithSize(UiSize::XSmall)
                              ->OnClick(Listen(cx, &OpenDocs))
                              ->IntoEl());
    s->Group(StrL("Appearance"));
    s->Item(StrL("Dark Mode"), StrL("Switch between light and dark themes."));
    s->SwitchField(&self->darkMode);
    s->Keywords(StrL("theme"), StrL("night"));
    s->Item(StrL("Auto Switch Theme"),
            StrL("Automatically switch theme based on system settings."));
    s->CheckboxField(&self->autoSwitch);
    s->Keywords(StrL("theme"), StrL("system"));
    s->Item(StrL("resettable"),
            StrL("Enable/Disable reset button for settings."));
    s->SwitchField(&self->resettable);
    s->Item(StrL("Group Variant"),
            StrL("Select the variant for setting groups."));
    s->DropdownField(self->groupVariant, kGroupVariants, 3)->FieldWidth(140);
    s->Item(StrL("Group Size"), StrL("Select the size for the setting group."));
    s->DropdownField(self->groupSize, kGroupSizes, 3)->FieldWidth(140);

    s->Group(StrL("Font"));
    s->GroupFooter(
        TextEl(cx->a, StrL("Font preferences apply to this story only.")));
    s->Item(StrL("Font Family"), StrL("Select the font family for the story."));
    s->DropdownField(self->fontFamily, kFontFamilies, 3)->FieldWidth(140);
    s->Keywords(StrL("typeface"));
    // default_value("14"): a size that is no longer the default offers to go
    // back to it, and the two steppers obey min, max and step. Rust's story
    // says the same range in the description.
    s->Item(
        StrL("Font Size"),
        StrL("Adjust the font size for better readability between 8 and 72."));
    s->NumberField(StrL("14"), {8, 72, 1}, StrL("14"))->FieldWidth(140);
    s->Item(StrL("Line Height"),
            StrL("Adjust the line height for better readability between 0 and "
                 "100."));
    s->NumberField(StrL("12"), {0, 100, 1})->FieldWidth(140);

    // The Other group: a switch that locks the rest, one that is only
    // findable by its keyword, and a row that is all content.
    s->Group(StrL("Other"));
    s->Item(StrL("Disable Settings"), StrL("Lock the other settings."));
    s->SwitchField(&self->disabled);
    s->Item(StrL("Foo"), StrL("Find me by searching for my sibling"));
    s->SwitchField(&self->foo)->Disabled(self->disabled);
    s->Keywords(StrL("Bar"));
    s->Item(StrL("View source, report issues, and follow project updates."),
            Str{},
            component::Button::New(cx, StrL("action"))
                ->Icon(IconName::Globe)
                ->Label(StrL("Repository..."))
                ->Outline()
                ->IntoEl());

    s->Page(StrL("Software Update"), IconName::Building2);
    s->Group(StrL("Updates"));
    s->Item(StrL("Automatic Updates"),
            StrL("Download and install updates in the background."));
    s->SwitchField(&self->autoUpdates);

    s->Page(StrL("About"), IconName::Info);
    s->Group(StrL("This build"));
    s->Item(StrL("Version"), StrL("The version this story was built from."),
            nullptr);

    El* page = Div(a)->FlexCol()->W(kFill);
    page->Child(s->IntoEl());
    return page;
}

STORY_PAGE(StorySettings, SettingsStory);
