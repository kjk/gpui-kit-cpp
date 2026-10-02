#ifndef GPUI_GPUI_KEYMAP_H_
#define GPUI_GPUI_KEYMAP_H_
/* Actions and the keymap — crates/gpui's `actions!`, `KeyBinding`,
   `key_context` and `on_action`, which gpui-kit builds every component's
   keyboard on.

   Rust dispatches a *type*: an action is a unit struct, a binding names it,
   and `on_action::<Copy>(..)` matches on it. There are no types to dispatch on
   here and no RTTI to ask, so an action is an id — the hash of its name, the
   way an element id is — and a handler is a Listener under that id.

   The rest is Rust's shape exactly. A binding may be scoped to a *key context
   predicate*, an element declares a key context for its subtree, and a
   keystroke is resolved against the contexts stacked along the focused
   element's ancestry, innermost first. The action that comes out is then
   offered to the handlers along that same chain, innermost first, until one
   keeps it; a handler that sets `propagate` passes it on, which is
   `cx.propagate()`. A binding may also be a *sequence* of chords, and a
   keystroke that begins one is held rather than dispatched. */

#include "gpui/gpui.h"

namespace gpui {

// The chord a binding is written as, once parsed. `vk` is the port's own key
// code, which is what a KeyEvent carries.
struct KeyChord {
    int vk = 0;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    // Command on macOS, the Windows/Super key elsewhere — GPUI's
    // `Modifiers::platform`, which is a modifier of its own and not a second
    // name for control.
    bool platform = false;
    // GPUI's `fn-` modifier. Native input only supplies it on macOS.
    bool function = false;
};

// Keystroke::parse. The modifiers Rust spells are `ctrl-`, `alt-`, `shift-`,
// `cmd-`, `fn-` and `secondary-`. `cmd-` is the platform key; `secondary-` is
// the
// shortcut modifier, which is the platform key on macOS and control
// everywhere else — so one `secondary-c` binding is Cmd-C on a Mac and
// Ctrl-C on the other two.
//
// They are not folded together. macOS binds `ctrl-backspace` and
// `cmd-backspace` to different actions in the same context, and
// `ctrl-cmd-space` needs both at once, so control and command have to stay
// apart. The key name is GPUI's:
// lowercase, with "enter", "escape", "tab", "space", "backspace", "delete",
// the four arrows, "home", "end", "pageup", "pagedown", "insert", "back",
// "forward", "menu", "f1".."f35", a letter or digit, or an ASCII punctuation
// key. Linux also names its XF86 cut/copy/paste/new/open/save keys and the
// operators on its numeric keypad.
//
// Answers false for a spec it cannot read, which is a programming mistake
// rather than something to handle.
bool KeyChordParse(Str spec, KeyChord* out);
bool KeyChordEq(const KeyChord& a, const KeyChord& b);

// How deep a stack of key contexts the matcher reads. Deeper than eight
// nested contexts is not a tree anybody builds, and the levels past that are
// the outermost ones — the window's, which a binding scoped to them could
// have gone unscoped instead.
const int kMaxContextDepth = 8;

// The longest sequence a binding may be written as. Rust's Vec has no bound;
// three is past what anything binds — Zed's own keymap stops at two.
const int kMaxStrokes = 3;

// A whole binding spec: the chords of a multi-stroke binding, space
// separated, the way Rust writes `KeyBinding::new("ctrl-k ctrl-o", ..)`.
// Answers how many were read, or 0 for a spec it cannot read.
int KeyChordsParse(Str spec, KeyChord* out, int maxChords);

// An action, which is its name hashed. Rust writes `actions!(root, [Tab])` and
// gets a type; this is the same name, spelled out.
uint32_t ActionOf(Str name);

// KeyContext::parse. A context is a set of identifiers and `key=value` pairs,
// space separated: "Editor", or "Editor mode=full". The id it answers is the
// whole spelling hashed — two elements that declare the same context agree on
// one — and the parse behind it is what a binding's predicate is read
// against. A context is remembered process-wide, since an element rebuilds
// its own every frame out of the same literal.
uint32_t KeyContextOf(Str name);

// `KeyBinding::new(stroke, action, context)`. A null context is Rust's `None`:
// the binding applies wherever focus is. The context is a
// KeyBindingContextPredicate, not just a name:
//
//   "Editor"                  the innermost context declares it
//   "Editor && mode == full"  and carries mode=full
//   "mode != full"            and does not
//   "!Editor"                 does not declare it
//   "Editor || Terminal"      either one
//   "Workspace > Editor"      an Editor whose parent context is a Workspace
//
// `>` binds loosest, then `||`, then `&&`, then `==` / `!=`, then `!`;
// parentheses group. That is Rust's precedence, so "Workspace > Editor &&
// mode == full" reads the way it does there.
struct KeyBinding {
    const char* stroke = nullptr;
    uint32_t action = 0;
    const char* context = nullptr;
    // What the action carries, for the actions that carry something:
    // `KeyBinding::new("secondary-enter", Confirm { secondary: true }, ..)`
    // is `{"secondary-enter", action::Confirm(), ctx, 1}`. It reaches the
    // handler as `ActionEvent::arg`.
    int64_t arg = 0;
};

// cx.bind_keys. The keymap is process-wide, the way the theme and the
// scrollbar mode are: Rust keeps one per App, and an App here is not a
// container for globals. Later bindings win over earlier ones for the same
// chord and context, which is what a keymap layered on top of a default one
// has to do.
void KeymapBind(const KeyBinding* bindings, int n);
void KeymapClear();

// Which keymap this is. A component binds its own keys once and then leaves
// them alone; this is how it tells that the map it bound into is gone — which
// is what clearing does, and what a test that clears it needs a component to
// notice. Never 0, so a "never bound" of 0 is always different.
uint32_t KeymapGeneration();

// KeyMatch. A chord either resolves to an action, or is the first half of a
// binding written as a sequence — `pending`, which means the matcher is
// holding it and the keystroke belongs to nobody else.
struct KeyMatch {
    uint32_t action = 0;
    // The matched binding's payload; 0 for an action that carries nothing.
    int64_t arg = 0;
    bool pending = false;
};

// The action a chord resolves to against a stack of contexts, innermost
// first. A binding with no context matches anywhere, but only after every
// scoped binding has been tried: a component's own binding beats the
// application's, which is what makes Escape close the innermost thing.
//
// Stateful, since a sequence is: the chord is appended to whatever is held,
// and a complete binding beats one that is only begun — which is Rust's rule,
// and why "ctrl-k" bound beside "ctrl-k ctrl-o" fires at once. A chord that
// neither completes nor continues anything drops what was held.
KeyMatch KeymapMatch(const KeyChord& chord, const uint32_t* contexts,
                     int nContexts);

// window.highest_precedence_binding_for_action, and the `_in_context`
// variant when a context stack is given: the chord a caller would press to
// reach `action`, which is what a menu row and a tooltip show beside a label.
//
// The search is the matcher's, run backwards: the last binding for an action
// wins, a scoped binding beats an unscoped one, and an inner context beats an
// outer. A binding written as a sequence answers with its first chord, the
// way Rust's `keystrokes().first()` does. False when nothing is bound.
//
// `contexts` may be null, which asks only about the bindings that named no
// context — Rust's `binding_for_action` with `None`.
bool KeymapBindingForAction(uint32_t action, const uint32_t* contexts,
                            int nContexts, KeyChord* out);

// The same, with the contexts a binding was scoped to ignored — Rust's
// `Keymap::bindings_for_action`, which is what a menu row's shortcut is
// looked up with. A row of the application's menu bar is outside every
// element, so there is no context stack to resolve against; the chord it
// shows is the one bound to the action wherever that binding applies, and
// pressing it dispatches the action to whatever has the focus, which is the
// same place the chord would have reached on its own.
bool KeymapAnyBindingForAction(uint32_t action, KeyChord* out);

// The name a key goes by in a binding spec — "c", "enter", "pagedown" — which
// is the inverse of what KeyChordParse reads. Empty for a key with no name,
// which is a key no binding could have named either.
Str KeyName(int vk);

// keystroke.rs is_printable_key, over the key codes: everything but the
// function keys, the editing and navigation keys and escape. A modifier on
// its own is not a key down in GPUI at all, so it is not printable either.
// A printable key that typed nothing, with no Ctrl, Alt, Cmd or Fn held, is
// Keystroke::is_ime_in_progress.
bool KeyIsPrintable(int vk);

// Whether a sequence is half-finished. The window asks before it offers a
// keystroke to the focused field: GPUI matches the keystroke before the text
// input is given it, which is what lets a sequence finish inside one.
bool KeymapPending();
// Drop a half-finished one. A window that loses the focus does this: the rest
// of the sequence is being typed somewhere else, and a chord left waiting
// would take the first keystroke of the window's next visit.
void KeymapClearPending();

} // namespace gpui
#endif // GPUI_GPUI_KEYMAP_H_
