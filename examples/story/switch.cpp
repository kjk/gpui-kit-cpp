#include "Story.h"

struct SwitchStory {
    bool switches[8] = {true, false, true, true, false};
    bool longLabelChecked = false;
    StoryToolbarState toolbar;

    static El* Render(SwitchStory* self, Ctx* cx);
};

static void SetSw0(SwitchStory* self, Ctx*, const ClickEvent*, intptr_t v) {
    self->switches[0] = v;
}
static void SetSw1(SwitchStory* self, Ctx*, const ClickEvent*, intptr_t v) {
    self->switches[1] = v;
}
static void SetSw3(SwitchStory* self, Ctx*, const ClickEvent*, intptr_t v) {
    self->switches[3] = v;
}
static void SetSw4(SwitchStory* self, Ctx*, const ClickEvent*, intptr_t v) {
    self->switches[4] = v;
}
static void SetLongLabel(SwitchStory* self, Ctx* cx, const ClickEvent*,
                         intptr_t v) {
    self->longLabelChecked = v;
    Notify(cx);
}

// h_flex().w_full().items_center().justify_between().gap_6().p_4(), holding
// v_flex().gap_1() of a font_medium line and a text_sm muted one.
static El* SwitchRow(Ctx* cx, SwitchStory* self, const char* title,
                     const char* desc, const char* id, int slot, Listener on) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* text = Div(a)->FlexCol()->Gap(4);
    text->Child(StoryTxt(cx, Str(title), 16, th.foreground)->Medium());
    text->Child(StoryTxt(cx, Str(desc), 14, th.mutedFg));
    return Div(a)
        ->FlexRow()
        ->W(kFill)
        ->ItemsCenter()
        ->JustifyBetween()
        ->Gap(24)
        ->Pad(16)
        ->Child(text)
        ->Child(component::Switch::New(cx, Str(id))
                    ->Checked(self->switches[slot])
                    ->WithSize(self->toolbar.size)
                    ->OnClick(on)
                    ->IntoEl());
}

El* SwitchStory::Render(SwitchStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    // section(..).w_128().items_stretch(), holding one v_flex().w_full()
    // bordered list.
    El* def = StorySection(cx, "Default",
                           "Switches work well in a compact settings list.");
    StorySectionBody(def)->W(512)->ItemsStretch();
    El* list =
        Div(a)->FlexCol()->W(kFill)->Border(1, th.border)->Radius(th.radiusLg);
    list->Child(SwitchRow(cx, self, "Product updates",
                          "New features and release notes.", "switch1", 0,
                          Listen(cx, &SetSw0)));
    list->Child(component::Separator::Horizontal(cx)->IntoEl());
    list->Child(SwitchRow(cx, self, "Security alerts",
                          "Important activity on your account.", "switch2", 1,
                          Listen(cx, &SetSw1)));
    StorySectionAdd(def, list);
    page->Child(def);

    // A 320px settings card: long setting names wrap while the track keeps
    // its size.
    El* longLabels =
        StorySection(cx, "Long labels",
                     "Long setting names wrap while the track keeps its size.");
    El* card =
        Div(a)->FlexCol()->W(320)->Border(1, th.border)->Radius(th.radiusLg);
    card->Child(
        component::Switch::New(cx, StrL("long-label"))
            ->WithSize(self->toolbar.size)
            ->Label(StrL("Automatically transcribe downloaded episodes"))
            ->Checked(self->longLabelChecked)
            ->OnChange(Listen(cx, &SetLongLabel))
            ->IntoEl()
            ->Pad(16));
    card->Child(component::Separator::Horizontal(cx)->IntoEl());
    card->Child(component::Switch::New(cx, StrL("long-label-disabled"))
                    ->WithSize(self->toolbar.size)
                    ->Label(StrL("Automatically download new episodes"))
                    ->Checked(true)
                    ->Disabled(true)
                    ->IntoEl()
                    ->Pad(16));
    StorySectionAdd(longLabels, card);
    page->Child(longLabels);

    // section(..).w_128(): the two switches are the section's own children,
    // so its h_flex wraps and centres them.
    El* dis = StorySection(
        cx, "Disabled", "Unavailable switches preserve their current value.");
    StorySectionBody(dis)->W(512);
    StorySectionAdd(dis, component::Switch::New(cx, StrL("switch3"))
                             ->Checked(self->switches[2])
                             ->Disabled(true)
                             ->WithSize(self->toolbar.size)
                             ->IntoEl());
    StorySectionAdd(dis, component::Switch::New(cx, StrL("switch3_1"))
                             ->Label(StrL("Airplane mode"))
                             ->Checked(true)
                             ->Disabled(true)
                             ->WithSize(self->toolbar.size)
                             ->IntoEl()
                             ->W(200));
    page->Child(dis);

    El* col = StorySection(cx, "Color",
                           "Semantic colors can reinforce the setting state.");
    StorySectionAdd(col, component::Switch::New(cx, StrL("switch4"))
                             ->Label(StrL("Success"))
                             ->Checked(self->switches[3])
                             ->Color(th.success)
                             ->WithSize(self->toolbar.size)
                             ->OnClick(Listen(cx, &SetSw3))
                             ->IntoEl());
    StorySectionAdd(col, component::Switch::New(cx, StrL("switch5"))
                             ->Label(StrL("Destructive"))
                             ->Checked(self->switches[4])
                             ->Color(th.danger)
                             ->WithSize(self->toolbar.size)
                             ->OnClick(Listen(cx, &SetSw4))
                             ->IntoEl());
    StorySectionAdd(col, component::Switch::New(cx, StrL("switch4_disabled"))
                             ->Label(StrL("Disabled"))
                             ->Checked(true)
                             ->Color(th.success)
                             ->Disabled(true)
                             ->WithSize(self->toolbar.size)
                             ->IntoEl());
    page->Child(col);
    return page;
}

STORY_PAGE(StorySwitch, SwitchStory);
