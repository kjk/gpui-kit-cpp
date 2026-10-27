#include "base/focus_trap.h"

namespace gpui {

void FocusTrapInit(App* app) {
    // Membership is frame-local in Window::focusEls, so unlike Rust's weak
    // handle map there is no global state to initialize or clean up.
    (void)app;
}

El* FocusTrapContainer::New(Ctx* cx, Str id, FocusHandle focus, El* child) {
    if (!cx || !child || !focus.IsValid()) {
        return child;
    }
    // In-place, so the element's accessibility role, label and description
    // stay on the node GPUI's wrapper would forward through a11y_role and
    // write_a11y_info. The wrapper's id is this element's id.
    return child->Id(id)->TrackFocus(focus)->TrapId(focus.id);
}

int FocusTrapId(Str name) {
    return HashClickId(name);
}

int FocusTrapOf(const Window* win, int focusId) {
    if (!focusId) {
        return 0;
    }
    for (int i = 0; i < win->focusEls.len; i++) {
        if (win->focusEls[i].id == focusId) {
            return win->focusEls[i].trapId;
        }
    }
    return 0;
}

int FocusTrapActive(const Window* win) {
    return FocusTrapOf(win, win->focusId);
}

// A rendered tab stop inside the trap. Non-stops still hold focus; they are
// not where Tab lands.
static bool TrapOffersTabStop(const Window* win, int trapId) {
    for (int i = 0; i < win->focusEls.len; i++) {
        if (win->focusEls[i].trapId == trapId && win->focusEls[i].tabStop) {
            return true;
        }
    }
    return false;
}

int FocusTrapTab(Window* win, bool backward) {
    // Root::advance_focus. The active trap is the innermost one: CollectFocus
    // writes the trap it is inside as it descends, so a child scope confines
    // Tab even when an ancestor was registered first. The walk is that trap's
    // own stops. There is no attempt cap — a page of background controls
    // cannot strand the key inside a modal, and those controls never take
    // focus on the way around.
    int trap = FocusTrapActive(win);
    if (!trap) {
        return FocusNext(win, 0, backward);
    }
    if (TrapOffersTabStop(win, trap)) {
        return FocusNext(win, trap, backward);
    }
    // The trap has no rendered tab stops. Its container remains a focus
    // target even when that container is excluded from ordinary tab order.
    if (win->focusId != trap) {
        for (int i = 0; i < win->focusEls.len; i++) {
            if (win->focusEls[i].id == trap) {
                WindowSetFocusId(win, trap);
                break;
            }
        }
    }
    return win->focusId;
}

bool FocusTrapEnter(Window* win, int trapId, bool backward) {
    if (!trapId) {
        return false;
    }
    int n = win->focusEls.len;
    for (int k = 0; k < n; k++) {
        int i = backward ? n - 1 - k : k;
        if (win->focusEls[i].trapId != trapId || !win->focusEls[i].tabStop) {
            continue;
        }
        if (win->focusEls[i].id == win->focusId) {
            return false;
        }
        WindowSetFocusId(win, win->focusEls[i].id);
        return true;
    }
    return false;
}

void FocusTrapArm(Window* win, int trapId, int hostFocusId) {
    if (win) {
        win->pendingTrap = trapId;
        win->pendingTrapHost = hostFocusId;
    }
}

void FocusTrapApplyPending(Window* win) {
    int trap = win->pendingTrap;
    int host = win->pendingTrapHost;
    if (!trap) {
        return;
    }
    // The open transition owns focus; later renders do not. This matches the
    // reverted dialog focus-reclaim change at upstream d5821f27.
    if (trap == win->previousTrap) {
        return;
    }
    // Focus that is already inside stays where it is — the trap keeps focus
    // in, it does not send it back to the top on every frame.
    if (FocusTrapActive(win) == trap) {
        return;
    }
    // The trap's own container first, which is where Rust puts it:
    // `track_focus(&self.focus).focus_trap(.., &self.focus)` on the dialog
    // root, and nothing focuses a control inside it. Focusing the first
    // control instead put a focus ring on a dialog's Cancel button the
    // moment it opened, which the Rust window never shows.
    if (host) {
        for (int i = 0; i < win->focusEls.len; i++) {
            if (win->focusEls[i].id == host) {
                WindowSetFocusId(win, host);
                return;
            }
        }
    }
    // No container of its own: the first thing in it that takes Tab, so the
    // trap still has focus to keep.
    FocusTrapEnter(win, trap);
}

} // namespace gpui
