#include "Story.h"

/* crates/story/src/stories/layout_direction_story.rs */

struct LayoutDirectionStory {
    InputState name;
    InputState search;
    SliderState volume = SliderStateNew(0, 100, SliderSingle(30), 1);
    int activeTab = 0;
    bool notifications = true;
    bool remember = false;
    bool seeded = false;

    static void Toggle(LayoutDirectionStory*, Ctx* cx, const ClickEvent*) {
        LayoutDirectionSetGlobal(cx->app, LayoutIsRtl(cx->app)
                                              ? LayoutDirection::Ltr
                                              : LayoutDirection::Rtl);
    }

    static void SelectTab(LayoutDirectionStory* self, Ctx* cx,
                          const ClickEvent*, int64_t index) {
        self->activeTab = (int)index;
        Notify(cx);
    }

    static void SetNotifications(LayoutDirectionStory* self, Ctx* cx,
                                 const ClickEvent*, int64_t checked) {
        self->notifications = checked != 0;
        Notify(cx);
    }

    static void SetRemember(LayoutDirectionStory* self, Ctx* cx,
                            const ClickEvent*, int64_t checked) {
        self->remember = checked != 0;
        Notify(cx);
    }

    static El* Render(LayoutDirectionStory* self, Ctx* cx);
};

El* LayoutDirectionStory::Render(LayoutDirectionStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    using component::Button;
    if (!self->seeded) {
        self->seeded = true;
        InputSetPlaceholder(&self->name, StrL("الاسم الكامل"));
        InputSetValue(&self->name, StrL("مرحبا بالعالم"));
        InputSetPlaceholder(&self->search, StrL("بحث…"));
    }
    bool rtl = LayoutIsRtl(cx->app);
    El* direction = StorySection(
        cx, "Direction",
        "One global setting; components never take a per-instance direction.");
    StorySectionAdd(
        direction,
        Div(a)
            ->FlexRowDir(cx->app)
            ->ItemsCenter()
            ->Gap(12)
            ->Child(TextEl(a, Str(rtl ? "Right to left" : "Left to right"))
                        ->Fg(th.mutedFg))
            ->Child(Button::New(cx, StrL("toggle-direction"))
                        ->Outline()
                        ->Label(Str(rtl ? "Switch to Left to Right"
                                        : "Switch to Right to Left"))
                        ->OnClick(Listen(cx, &LayoutDirectionStory::Toggle))
                        ->IntoEl()));
    El* form = Div(a)->FlexCol()->W(kFill)->Gap(16);
    form->Child(component::TabBar::New(cx, StrL("rtl-tabs"))
                    ->WFill()
                    ->Underline()
                    ->Selected(self->activeTab)
                    ->OnClick(Listen(cx, &LayoutDirectionStory::SelectTab))
                    ->Tab(StrL("الحساب"))
                    ->Tab(StrL("الملف الشخصي"))
                    ->Tab(StrL("الإعدادات"))
                    ->IntoEl());
    form->Child(
        Div(a)
            ->FlexCol()
            ->Gap(4)
            ->Child(TextEl(a, StrL("الاسم"))->Medium())
            ->Child(component::Input::New(cx, StrL("rtl-name"), &self->name)
                        ->IntoEl()));
    form->Child(component::Input::New(cx, StrL("rtl-search"), &self->search)
                    ->Prefix(IconEl(a, IconName::Search, 16))
                    ->IntoEl());
    form->Child(
        Div(a)
            ->FlexRowDir(cx->app)
            ->W(kFill)
            ->JustifyBetween()
            ->ItemsCenter()
            ->Child(
                Div(a)
                    ->FlexCol()
                    ->Gap(4)
                    ->Child(TextEl(a, StrL("الإشعارات"))->Medium())
                    ->Child(TextEl(a, StrL("تلقي التحديثات حول نشاط حسابك."))
                                ->Font(14)
                                ->Fg(th.mutedFg)))
            ->Child(component::Switch::New(cx, StrL("rtl-switch"))
                        ->Checked(self->notifications)
                        ->OnClick(
                            Listen(cx, &LayoutDirectionStory::SetNotifications))
                        ->IntoEl()));
    form->Child(Div(a)
                    ->FlexCol()
                    ->Gap(8)
                    ->Child(TextEl(a, StrL("مستوى الصوت"))->Medium())
                    ->Child(component::Slider::New(cx, StrL("rtl-volume"),
                                                   &self->volume)
                                ->IntoEl()));
    form->Child(component::Checkbox::New(cx, StrL("rtl-remember"))
                    ->Label(StrL("تذكرني"))
                    ->Checked(self->remember)
                    ->OnClick(Listen(cx, &LayoutDirectionStory::SetRemember))
                    ->IntoEl());
    form->Child(Div(a)
                    ->FlexRowDir(cx->app)
                    ->Gap(8)
                    ->Child(Button::New(cx, StrL("rtl-save"))
                                ->Primary()
                                ->Label(StrL("حفظ"))
                                ->IntoEl())
                    ->Child(Button::New(cx, StrL("rtl-cancel"))
                                ->Outline()
                                ->Label(StrL("إلغاء"))
                                ->IntoEl())
                    ->Child(Button::New(cx, StrL("rtl-next"))
                                ->Ghost()
                                ->Label(StrL("التالي"))
                                ->Icon(DirectionalChevron(cx->app, true))
                                ->IntoEl()));
    El* section = StorySection(cx, "Arabic Form", nullptr)->W(560)->MaxW(kFill);
    StorySectionAdd(section, form);
    return Div(a)->FlexCol()->Gap(24)->W(kFill)->Child(direction)->Child(
        section);
}

STORY_PAGE(StoryLayoutDirection, LayoutDirectionStory);
