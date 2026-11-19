/* crates/base/tests/selection_focus.rs */

#include "Test.h"

enum class SelectionControl : uint8_t {
    Toggle,
    Checkbox,
    Radio,
    Switch
};

struct DisableSelectionDuringFocus {
    FocusHandle handles[3];
    SelectionControl control;
    bool disabled = false;
    int changes = 0;

    static void OnChange(DisableSelectionDuringFocus* self, Ctx* cx,
                         const ClickEvent*, int64_t) {
        self->disabled = true;
        self->changes++;
        Notify(cx);
    }

    static El* Render(DisableSelectionDuringFocus* self, Ctx* cx) {
        Str id = StrL("control");
        bool disabled = self->disabled;
        FocusHandle focus = self->handles[1];
        Listener change = Listen(cx, &DisableSelectionDuringFocus::OnChange);
        El* control = nullptr;
        switch (self->control) {
            case SelectionControl::Toggle:
                control = Toggle::New(cx, id, false, disabled, change, nullptr,
                                      nullptr, 0, true, focus);
                break;
            case SelectionControl::Checkbox:
                control =
                    Checkbox::New(cx, id, CheckboxState::Unchecked, disabled,
                                  change, nullptr, nullptr, {}, 0, true, focus);
                break;
            case SelectionControl::Radio:
                control = Radio::New(cx, id, false, disabled, change, nullptr,
                                     nullptr, 0, true, focus);
                break;
            case SelectionControl::Switch:
                control = Switch::New(cx, id, false, disabled, change, nullptr,
                                      nullptr, {}, 0, true, focus);
                break;
        }
        return Div(cx->a)
            ->SizeFull()
            ->FlexRow()
            ->Child(Button::New(cx, StrL("before"))
                        ->TrackFocus(self->handles[0])
                        ->W(60)
                        ->H(30))
            ->Child(control->W(60)->H(30))
            ->Child(Button::New(cx, StrL("after"))
                        ->TrackFocus(self->handles[2])
                        ->W(60)
                        ->H(30));
    }
};

static void CheckDisabledFocus(SelectionControl control) {
    for (bool backward : {false, true}) {
        App* app = TestAppNew();
        BaseInit(app);
        auto view = EntityNew<DisableSelectionDuringFocus>(app);
        auto* self = view.Get(app);
        self->control = control;
        for (FocusHandle& handle : self->handles) {
            handle = FocusHandleNew(app);
        }
        Window* win = TestWindowOpen(app, view);
        FocusHandleFocus(win, self->handles[0]);
        TestDraw(win);

        auto press = [&](int key, bool shift, int expected) {
            WindowKeyDown(win, key, shift, false, false);
            WindowKeyUp(win, key, shift, false, false);
            TestDraw(win);
            utassert(FocusHandleIsFocused(win, self->handles[expected]));
        };
        press(KeyTab, false, 1);
        press(KeySpace, false, 1);
        utassert(self->disabled && self->changes == 1);
        press(KeyReturn, false, 1);
        press(KeySpace, false, 1);
        utassert(self->changes == 1);

        press(KeyTab, backward, backward ? 0 : 2);
        press(KeyTab, !backward, backward ? 2 : 0);
        self->disabled = false;
        TestDraw(win);
        press(KeyTab, backward, 1);
        TestAppFree(app);
    }
}

void TestSelectionFocus() {
    TestSuite("selection focus");
    CheckDisabledFocus(SelectionControl::Toggle);
    CheckDisabledFocus(SelectionControl::Checkbox);
    CheckDisabledFocus(SelectionControl::Radio);
    CheckDisabledFocus(SelectionControl::Switch);
}
