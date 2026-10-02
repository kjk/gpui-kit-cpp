#include "Story.h"

enum {
    NumDefault = 0,
    NumDisabled,
    NumSuffix,
    NumFormat,
    NumCustom,
    NumCount
};

struct NumberInputStory {
    InputState fields[NumCount];
    int focused = -1;
    StoryToolbarState toolbar;
    bool seeded = false;

    static El* Render(NumberInputStory* self, Ctx* cx);
    static void OnKey(NumberInputStory* self, Ctx* cx, const KeyEvent* ev);
};

static void StepSlot(NumberInputStory* self, Ctx* cx, int slot,
                     StepAction action) {
    // The format story steps by 0.01, the rest by one.
    double step = slot == NumFormat ? 0.01 : (slot == NumCustom ? 0.1 : 1);
    InputState* f = &self->fields[slot];
    // gpui_base::step_value works on the text, so "1.50" steps to "2.50" and
    // the field keeps the precision the reader is looking at. It answers no
    // when the step would not move the value.
    TempStr next = NumberStepValueTemp(Str(InputCStr(f)), action, step, false,
                                       0, false, 0);
    if (!next) {
        return;
    }
    InputSetValue(f, next);
    Notify(cx);
}

static void StepNum(NumberInputStory* self, Ctx* cx, const ClickEvent*,
                    int64_t packed) {
    StepSlot(self, cx, (int)(packed >> 1),
             (packed & 1) ? StepAction::Increment : StepAction::Decrement);
}

// The "NumberInput" key context: up steps the focused field up and down steps
// it down.
void NumberInputStory::OnKey(NumberInputStory* self, Ctx* cx,
                             const KeyEvent* ev) {
    if (!ev->down) {
        return;
    }
    StepAction action = StepAction::Increment;
    if (!NumberStepForKey(ev->vk, &action)) {
        return;
    }
    for (int i = 0; i < NumCount; i++) {
        if (self->fields[i].focused) {
            StepSlot(self, cx, i, action);
            return;
        }
    }
}

static void FocusNum(NumberInputStory* self, Ctx* cx, const ClickEvent*,
                     int64_t slot) {
    for (int i = 0; i < NumCount; i++) {
        self->fields[i].focused = false;
    }
    self->fields[slot].focused = true;
    self->focused = (int)slot;
    Notify(cx);
}

El* NumberInputStory::Render(NumberInputStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        struct {
            int slot;
            const char* value;
            const char* placeholder;
        } seeds[] = {
            {NumDefault, "1", "Normal Integer"},
            {NumDisabled, "100", "Disabled"},
            {NumSuffix, "", "Unsized Integer"},
            {NumFormat, "1234.56", "Mask pattern"},
            {NumCustom, "0.9", "Styling"},
        };
        for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
            InputState* f = &self->fields[seeds[i].slot];
            InputSetValue(f, Str(seeds[i].value));
            InputSetPlaceholder(f, Str(seeds[i].placeholder));
        }
    }
    if (self->focused >= 0) {
        cx->win->input = &self->fields[self->focused];
    }
    Listener step = Listen(cx, &StepNum);
    Listener focus = Listen(cx, &FocusNum);

    // NumberInput is flex_1 inside the section, so the w(260) each story asks
    // for loses to the 512 the section gives it.
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);

    El* def = StorySection(cx, "Default", "Application-managed step events.");
    StorySectionBody(def)->W(512)->ItemsCenter();
    StorySectionAdd(def, component::NumberInput::New(cx, StrL("num1"),
                                                     &self->fields[NumDefault])
                             ->W(512)
                             ->WithSize(self->toolbar.size)
                             ->OnInc(ListenerArg(step, (NumDefault << 1) | 1))
                             ->OnDec(ListenerArg(step, NumDefault << 1))
                             ->OnFocus(ListenerArg(focus, NumDefault))
                             ->IntoEl());
    page->Child(def);

    El* dis = StorySection(cx, "Disabled", "Read-only disabled state.");
    StorySectionBody(dis)->W(512)->ItemsCenter();
    StorySectionAdd(dis, component::NumberInput::New(cx, StrL("num-disabled"),
                                                     &self->fields[NumDisabled])
                             ->W(512)
                             ->WithSize(self->toolbar.size)
                             ->Disabled(true)
                             ->IntoEl());
    page->Child(dis);

    El* suf = StorySection(cx, "Suffix", "Small size with a suffix action.");
    StorySectionBody(suf)->W(512)->ItemsCenter();
    StorySectionAdd(suf, component::NumberInput::New(cx, StrL("num2"),
                                                     &self->fields[NumSuffix])
                             ->W(512)
                             ->WithSize(UiSize::Small)
                             ->Suffix(component::Button::New(cx, StrL("info"))
                                          ->Text()
                                          ->WithSize(UiSize::XSmall)
                                          ->Icon(IconName::Info)
                                          ->IntoEl())
                             ->OnInc(ListenerArg(step, (NumSuffix << 1) | 1))
                             ->OnDec(ListenerArg(step, NumSuffix << 1))
                             ->OnFocus(ListenerArg(focus, NumSuffix))
                             ->IntoEl());
    page->Child(suf);

    El* fmtSec = StorySection(cx, "Number format",
                              "Grouping, decimals, range, and step.");
    StorySectionBody(fmtSec)->W(512)->ItemsCenter();
    StorySectionAdd(fmtSec, component::NumberInput::New(
                                cx, StrL("num3"), &self->fields[NumFormat])
                                ->W(512)
                                ->WithSize(self->toolbar.size)
                                ->OnInc(ListenerArg(step, (NumFormat << 1) | 1))
                                ->OnDec(ListenerArg(step, NumFormat << 1))
                                ->OnFocus(ListenerArg(focus, NumFormat))
                                ->IntoEl());
    page->Child(fmtSec);

    El* custom = StorySection(cx, "Custom style",
                              "Appearance-free input with dynamic steps.");
    StorySectionBody(custom)->W(512)->ItemsCenter();
    StorySectionAdd(custom, component::NumberInput::New(
                                cx, StrL("num4"), &self->fields[NumCustom])
                                ->W(512)
                                ->WithSize(self->toolbar.size)
                                ->Appearance(false)
                                ->Bg(th.tokens.secondary)
                                ->TextColor(th.info)
                                ->OnInc(ListenerArg(step, (NumCustom << 1) | 1))
                                ->OnDec(ListenerArg(step, NumCustom << 1))
                                ->OnFocus(ListenerArg(focus, NumCustom))
                                ->IntoEl());
    page->Child(custom);
    return page;
}

STORY_PAGE_KEYS(StoryNumberInput, NumberInputStory);
