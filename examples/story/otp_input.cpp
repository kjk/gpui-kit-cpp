#include "Story.h"

struct OtpInputStory {
    // The Options dropdown flips masking on every input at once.
    bool masked = true;
    StoryToolbarState toolbar;
    // One state per field, the way the Rust story makes one OtpState each.
    // The two that start with a value are Rust's `default_value`.
    Entity<OtpState> otp = {};
    Entity<OtpState> oneGroup = {};
    Entity<OtpState> threeGroups = {};
    Entity<OtpState> custom = {};
    Subscription otpSubscription = {};
    char otpValue[17] = {};
    bool seeded = false;

    static El* Render(OtpInputStory* self, Ctx* cx);
    static void OnOtp(OtpInputStory* self, Ctx* cx, const OtpEvent* ev);
};

void OtpInputStory::OnOtp(OtpInputStory* self, Ctx* cx, const OtpEvent* ev) {
    if (!self || ev->kind != OtpEventKind::Complete) {
        return;
    }
    OtpState* state = self->otp.Get(cx);
    if (!state) {
        return;
    }
    int n = state->len < (int)sizeof(self->otpValue) - 1
                ? state->len
                : (int)sizeof(self->otpValue) - 1;
    memcpy(self->otpValue, OtpValue(state).s, n);
    self->otpValue[n] = 0;
    Notify(cx);
}

static Entity<OtpState> SeedOtp(Ctx* cx, const char* value, int slots) {
    Entity<OtpState> e = EntityNewState<OtpState>(cx->app);
    OtpState* s = e.Get(cx);
    if (s) {
        s->length = slots;
        int n = 0;
        while (value && value[n] && n < slots) {
            n++;
        }
        OtpSetValue(s, Str((char*)value, n));
    }
    return e;
}

enum {
    OtpOptMasked = ToolbarOptMultiple
};

static void OtpToolbarAct(OtpInputStory* self, Ctx* cx, const ClickEvent*,
                          int64_t act) {
    if (act == OtpOptMasked) {
        self->masked = !self->masked;
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    Notify(cx);
}

El* OtpInputStory::Render(OtpInputStory* self, Ctx* cx) {
    Arena* a = cx->a;
    if (!self->seeded) {
        self->seeded = true;
        self->otp = SeedOtp(cx, nullptr, 6);
        self->oneGroup = SeedOtp(cx, "123456", 6);
        self->threeGroups = SeedOtp(cx, "012345", 6);
        self->custom = SeedOtp(cx, "654321", 4);
        self->otpSubscription = Subscribe(cx, self->otp, &OtpInputStory::OnOtp);
    }
    // The Options dropdown flips masking on every field at once, which is one
    // write per state now rather than a flag the elements read.
    for (Entity<OtpState> e :
         {self->otp, self->oneGroup, self->threeGroups, self->custom}) {
        if (OtpState* s = e.Get(cx)) {
            s->masked = self->masked;
        }
    }
    El* page = Div(a)->FlexCol()->Gap(20)->W(kFill);
    StoryToolbarOpt opts[1] = {{"Masked", self->masked, OtpOptMasked}};
    page->Child(
        StoryToolbarOptions(cx, self, opts, 1, Listen(cx, &OtpToolbarAct)));

    El* def = StorySection(cx, "Default",
                           "Six cells with masking and value updates.");
    StorySectionBody(def)->FlexCol();
    StorySectionAdd(def, component::OtpInput::New(cx, StrL("otp"), self->otp)
                             ->WithSize(self->toolbar.size)
                             ->IntoEl());
    if (self->otpValue[0]) {
        StorySectionAdd(def,
                        TextEl(a, StoryFmt(cx, "Value: %s", self->otpValue)));
    }
    page->Child(def);

    El* group = StorySection(cx, "Grouping",
                             "Cells can be shown as one or several groups.");
    StorySectionBody(group)->FlexCol()->Gap(16);
    StorySectionAdd(
        group, component::OtpInput::New(cx, StrL("otp-small"), self->oneGroup)
                   ->Groups(1)
                   ->WithSize(self->toolbar.size)
                   ->IntoEl());
    StorySectionAdd(group, component::OtpInput::New(cx, StrL("otp-large"),
                                                    self->threeGroups)
                               ->Groups(3)
                               ->WithSize(self->toolbar.size)
                               ->IntoEl());
    page->Child(group);

    El* csz = StorySection(cx, "Custom size", "Custom cell dimensions.");
    StorySectionAdd(
        csz, component::OtpInput::New(cx, StrL("otp-sized"), self->custom)
                 ->Groups(1)
                 ->CellSize(55)
                 ->IntoEl());
    page->Child(csz);

    El* dis = StorySection(cx, "Disabled", "Disabled input with a value.");
    StorySectionAdd(dis, component::OtpInput::New(cx, "123456", 6)
                             ->Id(StrL("otp-disabled"))
                             ->Masked(self->masked)
                             ->Disabled(true)
                             ->WithSize(self->toolbar.size)
                             ->IntoEl());
    page->Child(dis);
    return page;
}

STORY_PAGE(StoryOtpInput, OtpInputStory);
