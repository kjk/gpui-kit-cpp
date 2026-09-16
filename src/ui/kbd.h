#ifndef GPUI_UI_KBD_H_
#define GPUI_UI_KBD_H_
/* Themed kbd — crates/ui/src/kbd.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// gpui::Keystroke: the modifiers a binding holds down and the key it ends on.
// `key` is the name GPUI uses — "c", "enter", "left", "pagedown" — lowercase,
// which is what `Kbd::format` matches on.
struct Keystroke {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    // The platform key: Command on macOS, Windows everywhere else.
    bool platform = false;
    Str key = {};
};

// Kbd::format: how the platform spells a keystroke. macOS runs the modifiers
// together in ⌃⌥⇧⌘ order and uses its glyphs for the named keys; everything
// else spells them out and joins with "+", in Ctrl+Alt+Shift+Win order. A key
// with no name of its own is capitalised.
//
// Writes into `out` and answers how many bytes it wrote, not counting the
// terminator — which is always written when there is room for it.
int KbdFormat(Keystroke stroke, char* out, int cap);
// The same, into the frame arena.
Str KbdFormatStr(Ctx* cx, Keystroke stroke);

// Kbd::binding_for_action / binding_for_action_in. The chord bound to
// `action`, as a Keystroke this platform can spell — so a menu row and a
// tooltip show what is actually bound rather than what a caller remembered to
// type. False when nothing is bound to it.
//
// `context` is a key context spelling — "Input", "PopupMenu" — or null to ask
// only about the bindings that named no context.
bool KeystrokeForAction(uint32_t action, const char* context, Keystroke* out);
// The same lookup against the key-context path registered for an arbitrary
// focus handle in the previous frame. The handle need not itself be focused.
bool KeystrokeForActionAtFocus(Ctx* cx, uint32_t action, FocusHandle focus,
                               Keystroke* out);

struct Kbd {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str stroke = {};
    bool appearance = true;
    bool outline = false;

    static Kbd* New(Ctx* cx, Str stroke);
    // The keystroke, spelled the way this platform spells it.
    static Kbd* New(Ctx* cx, Keystroke stroke);
    // The chord bound to `action`, or null when nothing is — which is what a
    // menu row with no shortcut shows.
    static Kbd* ForAction(Ctx* cx, uint32_t action,
                          const char* context = nullptr);
    static Kbd* ForActionAtFocus(Ctx* cx, uint32_t action, FocusHandle focus);
    Kbd* Appearance(bool v);
    Kbd* Outline();
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_KBD_H_
