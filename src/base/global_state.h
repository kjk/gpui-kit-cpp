#ifndef GPUI_BASE_GLOBAL_STATE_H_
#define GPUI_BASE_GLOBAL_STATE_H_
/* Application-wide Base behavior state — crates/base/src/global_state.rs */

#include "gpui/gpui.h"

namespace gpui {

struct BaseGlobalState {
    // The TextView being rendered, and the reading order the next selectable
    // run takes. Both came down from `crates/ui/src/global_state.rs` with the
    // rich text that uses them: a Base-only application renders a TextView
    // without a themed Root, so the stack has to live where TextView does.
    Vec<EntityId> textViewStateStack;
    uint64_t selectionDocumentOrder = 1;
    Vec<EntityId> deferredPopovers;
    // Rust keeps Vec<OwnedMenu> here so the system menu and the in-window
    // AppMenuBar read one model. MenuDef is the port's owned-menu vocabulary;
    // these pointers all belong to appMenuArena.
    Arena* appMenuArena = nullptr;
    Vec<MenuDef> appMenus;
    bool suppressTextSelection = false;
    // last_touch: when a finger last went down, on TimeNow's clock; negative
    // for never.
    double lastTouch = -1;

    ~BaseGlobalState() {
        VecReset(textViewStateStack);
        VecReset(deferredPopovers);
        VecReset(appMenus);
        if (appMenuArena) {
            ArenaDelete(appMenuArena);
        }
    }
};

// Exact public source names. Base is kept on the implementation name so the
// layer remains unambiguous beside component::UiGlobalState; these aliases
// are what `pub use global_state::{DeferredPopover, GlobalState}` projects.
using GlobalState = BaseGlobalState;
using DeferredPopover = EntityId;

BaseGlobalState* BaseGlobalStateOf(App* app);
void BaseGlobalStateInit(App* app);
void BaseSuppressTextSelection(App* app);
void BaseResetTextSelectionSuppression(App* app);
bool BaseIsTextSelectionSuppressed(const App* app);
// GlobalState::note_touch / is_touch_press: a finger went down, and whether
// the press being handled came from one — a touch recent enough (a second)
// that the mouse events of its tap, a double tap included, are still
// arriving.
void BaseNoteTouch(App* app);
bool BaseIsTouchPress(const App* app);

// GlobalState::app_menus / set_app_menus. The setter retains a deep copy and
// installs that same copy into the platform menu seam.
const MenuDef* BaseAppMenus(const App* app, int* count);
void BaseSetAppMenus(App* app, const MenuDef* menus, int count);

// `begin_selection_frame` / `next_selection_document_order`: the reading
// order a frame's selectable runs are numbered in. The TextSelectionLayer
// resets it as it prepaints, so a Base application needs no themed Root.
void BaseSelectionFrameBegin(App* app);
uint64_t BaseSelectionNextDocumentOrder(App* app);

// `text_view_state_stack`: the TextView whose document the runs being built
// belong to, innermost last.
void BaseTextViewStatePush(App* app, EntityId state);
void BaseTextViewStatePop(App* app);
EntityId BaseTextViewStateCurrent(const App* app);

// A live popover is represented by its generational entity handle. Stale
// handles are swept on read, which is the Rc<Weak<()>> lifetime Rust uses
// without requiring reference counting in this tree.
void BaseDeferredPopoverSet(App* app, EntityId popover, bool open);
DeferredPopover BaseRegisterDeferredPopover(App* app, EntityId popover);
bool BaseIsInDeferredContext(App* app);

} // namespace gpui
#endif // GPUI_BASE_GLOBAL_STATE_H_
