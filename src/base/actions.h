#ifndef GPUI_BASE_ACTIONS_H_
#define GPUI_BASE_ACTIONS_H_
/* The actions every keyboard-driven control shares —
   crates/base/src/actions.rs.

   Rust declares them with `actions!(ui, [..])` and dispatches the *type*: a
   binding names it, `on_action::<SelectUp>(..)` matches on it. An action here
   is the hash of its name, so these are the names — Rust's own, namespace
   included, so "ui::SelectUp" is `ui::SelectUp`. One accessor each, because a
   name spelled two ways is two actions and nothing would say so.

   `Confirm` carries a `secondary` flag in Rust — one action type with two
   payloads, which is how a combobox tells enter from ctrl-enter. It is one
   action here too: the flag rides on the binding as `KeyBinding::arg` and
   reaches the handler as `ActionEvent::arg`, which is what
   `kConfirmSecondary` names. */

#include "gpui/gpui.h"

namespace gpui {

namespace action {

uint32_t Confirm();
// The payload `ui::Confirm { secondary: true }` carries. A binding writes it
// as its `arg`; a handler reads `ev->arg == kConfirmSecondary`.
constexpr int64_t kConfirmSecondary = 1;
uint32_t Cancel();
uint32_t SelectUp();
uint32_t SelectDown();
uint32_t SelectLeft();
uint32_t SelectRight();
uint32_t SelectFirst();
uint32_t SelectLast();
uint32_t SelectPrevColumn();
uint32_t SelectNextColumn();
uint32_t SelectPageUp();
uint32_t SelectPageDown();

} // namespace action

// An overlay whose only binding is escape — popover.rs, sheet.rs and
// color_picker.rs each bind Cancel in a context of their own and close on it.
// Rust's are views that own the flag they clear; the port's are builders
// whose caller owns it, so the listener waits in a keyed entity where an
// action arriving between frames can find it.
struct CancelKeys {
    Listener onCancel = {};

    static void OnAction(CancelKeys* self, Ctx* cx, const ActionEvent* ev);
};

// Bind escape to ui::Cancel in `context`, once per context per keymap.
void CancelInitKeys(const char* context);
// Declare `context` on `root` and run `onCancel` for the escape in it.
// `name` keys the entity the listener waits in, so two overlays of the same
// kind keep their own.
void CancelBindKeys(Ctx* cx, El* root, const char* context, Str name,
                    Listener onCancel);

} // namespace gpui
#endif // GPUI_BASE_ACTIONS_H_
