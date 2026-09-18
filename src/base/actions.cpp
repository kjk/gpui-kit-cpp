#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

namespace action {

// The hash is cheap, but it is the same hash every frame for the life of the
// process, so each name is taken once and kept.
#define GPUI_ACTION(fn, name)          \
    uint32_t fn() {                    \
        static uint32_t id = 0;        \
        if (!id) {                     \
            id = ActionOf(StrL(name)); \
        }                              \
        return id;                     \
    }

GPUI_ACTION(Confirm, "ui::Confirm")
GPUI_ACTION(Cancel, "ui::Cancel")
GPUI_ACTION(SelectUp, "ui::SelectUp")
GPUI_ACTION(SelectDown, "ui::SelectDown")
GPUI_ACTION(SelectLeft, "ui::SelectLeft")
GPUI_ACTION(SelectRight, "ui::SelectRight")
GPUI_ACTION(SelectFirst, "ui::SelectFirst")
GPUI_ACTION(SelectLast, "ui::SelectLast")
GPUI_ACTION(SelectPrevColumn, "ui::SelectPrevColumn")
GPUI_ACTION(SelectNextColumn, "ui::SelectNextColumn")
GPUI_ACTION(SelectPageUp, "ui::SelectPageUp")
GPUI_ACTION(SelectPageDown, "ui::SelectPageDown")

#undef GPUI_ACTION

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
