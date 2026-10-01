#include "Story.h"

// ButtonAction: the rows of this page's Options dropdown.
enum {
    BtnActDisabled = 3100,
    BtnActLoading,
    BtnActSelected,
    BtnActCompact,
    BtnActShadow,
    BtnActMultiple,
};

struct ButtonStory {
    StoryToolbarState toolbar;
    bool disabled = false;
    bool loading = false;
    bool selected = false;
    bool compact = false;
    bool toggleMultiple = false;

    static El* Render(ButtonStory* self, Ctx* cx);
};

static void OnOption(ButtonStory* self, Ctx* cx, const ClickEvent*,
                     intptr_t act) {
    switch ((int)act) {
        case BtnActDisabled:
            self->disabled = !self->disabled;
            break;
        case BtnActLoading:
            self->loading = !self->loading;
            break;
        case BtnActSelected:
            self->selected = !self->selected;
            break;
        case BtnActCompact:
            self->compact = !self->compact;
            break;
        case BtnActShadow:
            ThemeUpdate(cx->app, [](Theme* t) { t->shadow = !t->shadow; });
            break;
        case BtnActMultiple:
            self->toggleMultiple = !self->toggleMultiple;
            break;
        default:
            StoryToolbarApply(&self->toolbar, nullptr, (int)act);
            break;
    }
    Notify(cx);
}

// println!("Button clicked {:?}", ev).
static void OnButtonClick(ButtonStory*, Ctx*, const ClickEvent*) {
    logf("Button clicked\n");
}

// The Selection group reports the ordered indices selected after the click,
// and this page keeps its four flags in them.
static void OnSelectionGroup(ButtonStory* self, Ctx* cx,
                             const component::ButtonGroupEvent* ev) {
    self->disabled = ev->Contains(0);
    self->loading = ev->Contains(1);
    self->selected = ev->Contains(2);
    self->compact = ev->Contains(3);
    Notify(cx);
}

// `let button = |id| Button::new(id).with_size(self.size)`, plus the four
// flags every button on the page carries.
static component::Button* Btn(Ctx* cx, ButtonStory* self, const char* id) {
    component::Button* b = component::Button::New(cx, Str(id))
                               ->WithSize(self->toolbar.size)
                               ->Disabled(self->disabled)
                               ->Selected(self->selected)
                               ->Loading(self->loading);
    if (self->compact) {
        b->Compact();
    }
    return b;
}

static component::ButtonIcon* ProgressIcon(Ctx* cx, float value, Rgba color,
                                           bool hasColor) {
    component::ProgressCircle* p =
        component::ProgressCircle::New(cx)->Value(value)->Label(false);
    if (hasColor) {
        p->Color(color);
    }
    return component::ButtonIcon::New(cx, p);
}

struct BtnVarSpec {
    const char* id;
    const char* label;
    component::ButtonVariant v;
};

El* ButtonStory::Render(ButtonStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    UiSize size = self->toolbar.size;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);

    StoryToolbarOpt opts[6] = {
        {"Disabled", self->disabled, BtnActDisabled},
        {"Loading", self->loading, BtnActLoading},
        {"Selected", self->selected, BtnActSelected},
        {"Compact", self->compact, BtnActCompact},
        {"Shadow", th.shadow, BtnActShadow},
        {"Multiple selection", self->toggleMultiple, BtnActMultiple, false,
         true},
    };
    page->Child(StoryToolbarOptions(cx, self, opts, 6, Listen(cx, &OnOption)));

    Listener click = Listen(cx, &OnButtonClick);

    El* vars = StorySection(cx, "Variants",
                            "Visual treatments communicate action priority.");
    StorySectionBody(vars)->W(512);
    static const BtnVarSpec kVars[] = {
        {"button-0", "Default", component::ButtonVariant::Default},
        {"button-1", "Primary", component::ButtonVariant::Primary},
        {"button-2", "Secondary", component::ButtonVariant::Secondary},
        {"button-4", "Danger", component::ButtonVariant::Danger},
        {"button-4-warning", "Warning", component::ButtonVariant::Warning},
        {"button-4-success", "Success", component::ButtonVariant::Success},
        {"button-5-info", "Info", component::ButtonVariant::Info},
        {"button-5-ghost", "Ghost", component::ButtonVariant::Ghost},
        {"button-5-link", "Link", component::ButtonVariant::Link},
        {"button-5-text", "Text", component::ButtonVariant::Text},
    };
    for (const BtnVarSpec& v : kVars) {
        component::Button* b = Btn(cx, self, v.id)->Label(Str(v.label));
        b->variant = v.v;
        StorySectionAdd(vars, b->OnClick(click)->IntoEl());
    }
    page->Child(vars);

    El* icons = StorySection(
        cx, "Icons", "Icons can lead labels or appear in custom content.");
    StorySectionAdd(icons, Btn(cx, self, "button-icon-1")
                               ->Outline()
                               ->Label(StrL("Confirm"))
                               ->Icon(IconName::Check)
                               ->OnClick(click)
                               ->IntoEl());
    StorySectionAdd(icons, Btn(cx, self, "button-icon-2")
                               ->Outline()
                               ->Label(StrL("Abort"))
                               ->Icon(IconName::X)
                               ->OnClick(click)
                               ->IntoEl());
    StorySectionAdd(icons, Btn(cx, self, "button-icon-3")
                               ->Outline()
                               ->Label(StrL("Maximize"))
                               ->Icon(IconName::Maximize)
                               ->OnClick(click)
                               ->IntoEl());
    El* custom = Div(a)->FlexRow()->ItemsCenter()->Gap(8);
    custom->Child(TextEl(a, StrL("Custom Child")));
    custom->Child(IconEl(a, IconName::ChevronDown, 14));
    custom->Child(IconEl(a, IconName::Eye, 14));
    StorySectionAdd(icons, Btn(cx, self, "button-icon-4")
                               ->Extra(custom)
                               ->OnClick(click)
                               ->IntoEl());
    StorySectionAdd(icons, Btn(cx, self, "button-icon-5-ghost")
                               ->Ghost()
                               ->Icon(IconName::Check)
                               ->Label(StrL("Confirm"))
                               ->OnClick(click)
                               ->IntoEl());
    StorySectionAdd(icons, Btn(cx, self, "button-icon-6-link")
                               ->Link()
                               ->Icon(IconName::Check)
                               ->Label(StrL("Link"))
                               ->OnClick(click)
                               ->IntoEl());
    StorySectionAdd(icons, Btn(cx, self, "button-icon-6-text")
                               ->Text()
                               ->Icon(IconName::Check)
                               ->Label(StrL("Text Button"))
                               ->OnClick(click)
                               ->IntoEl());
    page->Child(icons);

    // The progress buttons take none of the page's flags — Rust builds them
    // straight off the plain button helper.
    El* prog =
        StorySection(cx, "Progress", "Buttons can show determinate progress.");
    El* progRow = Div(a)->FlexRow()->Gap(16)->ItemsCenter();
    progRow->Child(component::Button::New(cx, StrL("progress-button-1"))
                       ->WithSize(size)
                       ->Primary()
                       ->Icon(ProgressIcon(cx, 25, th.primaryFg, true))
                       ->Label(StrL("Installing..."))
                       ->IntoEl());
    const float kProgress[] = {35, 68, 85};
    for (int i = 0; i < 3; i++) {
        progRow->Child(component::Button::New(
                           cx, StoryFmt(cx, "progress-button-%d", i + 2))
                           ->WithSize(size)
                           ->Icon(ProgressIcon(cx, kProgress[i], {}, false))
                           ->Label(StrL("Installing..."))
                           ->IntoEl());
    }
    StorySectionAdd(prog, progRow);
    page->Child(prog);

    El* out = StorySection(cx, "Outline",
                           "Outlined treatments keep actions visually quiet.");
    StorySectionBody(out)->W(512);
    static const BtnVarSpec kOutline[] = {
        {"button-outline-1", "Primary Button",
         component::ButtonVariant::Primary},
        {"button-outline-2", "Normal Button",
         component::ButtonVariant::Default},
        {"button-outline-4-danger", "Danger Button",
         component::ButtonVariant::Danger},
        {"button-outline-4-warning", "Warning Button",
         component::ButtonVariant::Warning},
        {"button-outline-4-success", "Success Button",
         component::ButtonVariant::Success},
        {"button-outline-5-info", "Info Button",
         component::ButtonVariant::Info},
        {"button-outline-5-ghost", "Ghost Button",
         component::ButtonVariant::Ghost},
        {"button-outline-5-link", "Link Button",
         component::ButtonVariant::Link},
        {"button-outline-5-text", "Text Button",
         component::ButtonVariant::Text},
    };
    for (const BtnVarSpec& v : kOutline) {
        component::Button* b =
            Btn(cx, self, v.id)->Outline()->Label(Str(v.label));
        b->variant = v.v;
        StorySectionAdd(out, b->OnClick(click)->IntoEl());
    }
    page->Child(out);

    El* drop =
        StorySection(cx, "Dropdown", "A caret indicates an attached menu.");
    StorySectionBody(drop)->W(512);
    static const BtnVarSpec kDrop[] = {
        {"button-dropdown-caret-primary", "Primary Button",
         component::ButtonVariant::Primary},
        {"button-dropdown-caret-default", "Default Button",
         component::ButtonVariant::Default},
        {"button-outline-3", "Secondary Button",
         component::ButtonVariant::Secondary},
        {"button-dropdown-caret-ghost", "Ghost Button",
         component::ButtonVariant::Ghost},
        {"button-dropdown-caret-link", "Link Button",
         component::ButtonVariant::Link},
    };
    for (const BtnVarSpec& v : kDrop) {
        component::Button* b =
            Btn(cx, self, v.id)->DropdownCaret()->Label(Str(v.label));
        b->variant = v.v;
        StorySectionAdd(drop, b->OnClick(click)->IntoEl());
    }
    StorySectionAdd(drop, Btn(cx, self, "button-dropdown-caret-small")
                              ->Outline()
                              ->DropdownCaret()
                              ->Label(StrL("Small Button"))
                              ->OnClick(click)
                              ->IntoEl());
    page->Child(drop);

    static const char* const kOneTwoThree[] = {"One", "Two", "Three"};
    El* hg = StorySection(cx, "Horizontal group", nullptr);
    component::ButtonGroup* hgrp =
        component::ButtonGroup::New(cx, StrL("button-group"))
            ->Outline()
            ->Disabled(self->disabled);
    for (int i = 0; i < 3; i++) {
        hgrp->Child(Btn(cx, self, StoryFmt(cx, "button-%s", kOneTwoThree[i]).s)
                        ->Label(Str(kOneTwoThree[i]))
                        ->Loading(false)
                        ->OnClick(click));
    }
    StorySectionAdd(hg, hgrp->IntoEl());
    page->Child(hg);

    El* vg = StorySection(cx, "Vertical group", nullptr);
    component::ButtonGroup* vgrp =
        component::ButtonGroup::New(cx, StrL("button-group-vertical"))
            ->Outline()
            ->Vertical()
            ->Disabled(self->disabled);
    for (int i = 0; i < 3; i++) {
        vgrp->Child(
            Btn(cx, self, StoryFmt(cx, "button-vertical-%s", kOneTwoThree[i]).s)
                ->Label(Str(kOneTwoThree[i]))
                ->Loading(false)
                ->OnClick(click));
    }
    StorySectionAdd(vg, vgrp->IntoEl());
    page->Child(vg);

    // The four toggles in this group are the four Options rows, so the group
    // and the dropdown drive one another.
    El* sel = StorySection(cx, "Selection group",
                           "Groups support single or multiple selection.");
    struct ToggleSpec {
        const char* id;
        const char* label;
        bool on;
    };
    const ToggleSpec kToggles[] = {
        {"disabled-toggle-button", "Disabled", self->disabled},
        {"loading-toggle-button", "Loading", self->loading},
        {"selected-toggle-button", "Selected", self->selected},
        {"compact-toggle-button", "Compact", self->compact},
    };
    component::ButtonGroup* tgrp =
        component::ButtonGroup::New(cx, StrL("toggle-button-group"))
            ->Outline()
            ->Compact()
            ->Multiple(self->toggleMultiple)
            ->OnClick(Listen(cx, &OnSelectionGroup));
    for (const ToggleSpec& t : kToggles) {
        tgrp->Child(component::Button::New(cx, Str(t.id))
                        ->WithSize(size)
                        ->Label(Str(t.label))
                        ->Selected(t.on));
    }
    StorySectionAdd(sel, tgrp->IntoEl());
    page->Child(sel);

    El* only = StorySection(cx, "Icon-only",
                            "Compact actions can omit visible labels.");
    StorySectionAdd(only, Btn(cx, self, "icon-button-primary")
                              ->Icon(IconName::Search)
                              ->LoadingIcon(IconName::LoaderCircle)
                              ->Primary()
                              ->IntoEl());
    // .loading(true) then .loading(loading): the later call wins.
    StorySectionAdd(only, Btn(cx, self, "icon-button-secondary")
                              ->Icon(IconName::Info)
                              ->IntoEl());
    StorySectionAdd(only, Btn(cx, self, "icon-button-danger")
                              ->Icon(IconName::X)
                              ->Danger()
                              ->IntoEl());
    StorySectionAdd(only, Btn(cx, self, "icon-button-small-primary")
                              ->Icon(IconName::Search)
                              ->Primary()
                              ->IntoEl());
    StorySectionAdd(only, Btn(cx, self, "icon-button-outline")
                              ->Icon(IconName::Search)
                              ->Outline()
                              ->IntoEl());
    StorySectionAdd(only, Btn(cx, self, "icon-button-ghost")
                              ->Icon(IconName::ArrowLeft)
                              ->LoadingIcon(IconName::LoaderCircle)
                              ->Ghost()
                              ->IntoEl());
    page->Child(only);

    El* csz = StorySection(
        cx, "Custom size",
        "A fixed pixel size is available for compact icon actions.");
    StorySectionAdd(csz, Btn(cx, self, "icon-button-9")
                             ->Icon(IconName::Heart)
                             ->Size(24)
                             ->Ghost()
                             ->IntoEl());
    page->Child(csz);

    // ButtonCustomVariant::new(cx).color(magenta).foreground(magenta)
    // .hover(magenta at 0.1).active(magenta at 0.2).
    El* customSec = StorySection(cx, "Custom color", nullptr);
    StorySectionAdd(customSec, Btn(cx, self, "button-6-custom")
                                   ->Custom(th.magenta)
                                   ->Label(StrL("Custom Button"))
                                   ->OnClick(click)
                                   ->IntoEl());
    StorySectionAdd(customSec, Btn(cx, self, "button-outline-6-custom")
                                   ->Outline()
                                   ->Custom(th.magenta)
                                   ->Label(StrL("Outline Button"))
                                   ->OnClick(click)
                                   ->IntoEl());
    StorySectionAdd(customSec, Btn(cx, self, "button-outline-6-custom-1")
                                   ->Outline()
                                   ->Icon(IconName::Bell)
                                   ->Custom(th.magenta)
                                   ->Label(StrL("Icon Button"))
                                   ->OnClick(click)
                                   ->IntoEl());
    page->Child(customSec);
    return page;
}

STORY_PAGE(StoryButton, ButtonStory);
