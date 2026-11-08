#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

namespace action {

GPUI_DEFINE_ACTION(Confirm, "ui::Confirm")
GPUI_DEFINE_ACTION(NoAction, "gpui::NoAction")
GPUI_DEFINE_ACTION(Cancel, "ui::Cancel")
GPUI_DEFINE_ACTION(SelectUp, "ui::SelectUp")
GPUI_DEFINE_ACTION(SelectDown, "ui::SelectDown")
GPUI_DEFINE_ACTION(SelectLeft, "ui::SelectLeft")
GPUI_DEFINE_ACTION(SelectRight, "ui::SelectRight")
GPUI_DEFINE_ACTION(SelectFirst, "ui::SelectFirst")
GPUI_DEFINE_ACTION(SelectLast, "ui::SelectLast")
GPUI_DEFINE_ACTION(SelectPrevColumn, "ui::SelectPrevColumn")
GPUI_DEFINE_ACTION(SelectNextColumn, "ui::SelectNextColumn")
GPUI_DEFINE_ACTION(SelectPageUp, "ui::SelectPageUp")
GPUI_DEFINE_ACTION(SelectPageDown, "ui::SelectPageDown")

} // namespace action

void CancelKeys::OnAction(CancelKeys* self, Ctx* cx, const ActionEvent* ev) {
    if (!self || ev->action != action::Cancel()) {
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    if (!self->onCancel.IsValid()) {
        return;
    }
    // The same handler the close button carries, called the way Rust's
    // on_action does: with a ClickEvent::default().
    ClickEvent click = {};
    ListenerCall(cx->app, cx->win, self->onCancel, &click);
}

// One entry per context that has been bound, and the keymap it was bound
// into. A handful of overlay kinds, so a linear scan is the whole index.
struct CancelBound {
    uint32_t context = 0;
    uint32_t generation = 0;
};
// The keymap is process-owned, so this registry is too. Rust's key context
// table has no arbitrary ceiling; neither does the port's.
static Vec<CancelBound> gCancelBound;

void CancelInitKeys(const char* context) {
    uint32_t id = KeyContextOf(Str(context));
    uint32_t gen = KeymapGeneration();
    for (int i = 0; i < len(gCancelBound); i++) {
        if (gCancelBound[i].context != id) {
            continue;
        }
        if (gCancelBound[i].generation == gen) {
            return;
        }
        gCancelBound[i].generation = gen;
        KeyBinding b = {"escape", action::Cancel(), context};
        KeymapBind(&b, 1);
        return;
    }
    CancelBound bound = {id, gen};
    VecAppend(gCancelBound, bound);
    KeyBinding b = {"escape", action::Cancel(), context};
    KeymapBind(&b, 1);
}

void CancelBindKeys(Ctx* cx, El* root, const char* context, Str name,
                    Listener onCancel) {
    if (!cx || !root || !onCancel.IsValid()) {
        return;
    }
    CancelInitKeys(context);
    Entity<CancelKeys> keys =
        ElementStateEntity<CancelKeys>(cx, name, StrL("gpui::CancelKeys"));
    if (CancelKeys* k = keys.Get(cx)) {
        k->onCancel = onCancel;
    }
    root->KeyContext(Str(context))
        ->OnAction(action::Cancel(), ListenTo(keys, &CancelKeys::OnAction));
}

} // namespace gpui
