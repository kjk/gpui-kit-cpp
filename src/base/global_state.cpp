#include "base/global_state.h"

namespace gpui {

BaseGlobalState* BaseGlobalStateOf(App* app) {
    return AppGlobalEnsure<BaseGlobalState>(app);
}

void BaseGlobalStateInit(App* app) {
    (void)BaseGlobalStateOf(app);
}

void BaseSelectionFrameBegin(App* app) {
    if (BaseGlobalState* state = BaseGlobalStateOf(app)) {
        state->selectionDocumentOrder = 1;
    }
}

uint64_t BaseSelectionNextDocumentOrder(App* app) {
    BaseGlobalState* state = BaseGlobalStateOf(app);
    if (!state) {
        return 0;
    }
    return state->selectionDocumentOrder++;
}

void BaseTextViewStatePush(App* app, EntityId view) {
    if (BaseGlobalState* state = BaseGlobalStateOf(app)) {
        VecAppend(state->textViewStateStack, view);
    }
}

void BaseTextViewStatePop(App* app) {
    if (BaseGlobalState* state = AppGlobalGet<BaseGlobalState>(app)) {
        if (state->textViewStateStack.len > 0) {
            state->textViewStateStack.len--;
        }
    }
}

EntityId BaseTextViewStateCurrent(const App* app) {
    BaseGlobalState* state = AppGlobalGet<BaseGlobalState>(app);
    return state && state->textViewStateStack.len > 0
               ? state->textViewStateStack[state->textViewStateStack.len - 1]
               : EntityId{};
}

void BaseSuppressTextSelection(App* app) {
    if (BaseGlobalState* state = BaseGlobalStateOf(app)) {
        state->suppressTextSelection = true;
    }
}

void BaseResetTextSelectionSuppression(App* app) {
    if (BaseGlobalState* state = BaseGlobalStateOf(app)) {
        state->suppressTextSelection = false;
    }
}

bool BaseIsTextSelectionSuppressed(const App* app) {
    BaseGlobalState* state = AppGlobalGet<BaseGlobalState>(app);
    return state && state->suppressTextSelection;
}

void BaseNoteTouch(App* app) {
    if (BaseGlobalState* state = BaseGlobalStateOf(app)) {
        state->lastTouch = TimeNow();
    }
}

bool BaseIsTouchPress(const App* app) {
    BaseGlobalState* state = AppGlobalGet<BaseGlobalState>(app);
    // A double tap takes up to twice the tap interval.
    return state && state->lastTouch >= 0 && TimeNow() - state->lastTouch < 1.0;
}

static MenuRow* CopyMenuRows(Arena* a, const MenuRow* rows, int count) {
    if (!rows || count <= 0) {
        return nullptr;
    }
    MenuRow* copy = (MenuRow*)a->Push((uint64_t)count * sizeof(MenuRow),
                                      alignof(MenuRow), true);
    for (int i = 0; i < count; i++) {
        copy[i] = rows[i];
        copy[i].label = StrDup(a, rows[i].label);
        copy[i].submenu = CopyMenuRows(a, rows[i].submenu, rows[i].submenuN);
    }
    return copy;
}

const MenuDef* BaseAppMenus(const App* app, int* count) {
    BaseGlobalState* state = AppGlobalGet<BaseGlobalState>(app);
    if (count) {
        *count = state ? state->appMenus.len : 0;
    }
    return state && state->appMenus.len > 0 ? state->appMenus.els : nullptr;
}

void BaseSetAppMenus(App* app, const MenuDef* menus, int count) {
    BaseGlobalState* state = BaseGlobalStateOf(app);
    if (!state) {
        return;
    }
    if (!state->appMenuArena) {
        state->appMenuArena = ArenaNew();
    }
    VecClear(state->appMenus);
    state->appMenuArena->Reset();
    if (menus && count > 0) {
        VecReserve(state->appMenus, count);
        for (int i = 0; i < count; i++) {
            MenuDef copy = menus[i];
            copy.name = StrDup(state->appMenuArena, menus[i].name);
            copy.items =
                CopyMenuRows(state->appMenuArena, menus[i].items, menus[i].n);
            VecAppend(state->appMenus, copy);
        }
    }
    AppSetMenus(app, state->appMenus.els, state->appMenus.len);
}

void BaseDeferredPopoverSet(App* app, EntityId popover, bool open) {
    BaseGlobalState* state = BaseGlobalStateOf(app);
    if (!state || !popover.IsValid()) {
        return;
    }
    int found = -1;
    for (int i = 0; i < state->deferredPopovers.len; i++) {
        if (state->deferredPopovers[i] == popover) {
            found = i;
            break;
        }
    }
    if (open && found < 0) {
        VecAppend(state->deferredPopovers, popover);
    } else if (!open && found >= 0) {
        for (int i = found; i < state->deferredPopovers.len - 1; i++) {
            state->deferredPopovers[i] = state->deferredPopovers[i + 1];
        }
        state->deferredPopovers.len--;
    }
}

DeferredPopover BaseRegisterDeferredPopover(App* app, EntityId popover) {
    BaseDeferredPopoverSet(app, popover, true);
    return popover;
}

bool BaseIsInDeferredContext(App* app) {
    BaseGlobalState* state = AppGlobalGet<BaseGlobalState>(app);
    if (!state) {
        return false;
    }
    for (int i = state->deferredPopovers.len - 1; i >= 0; i--) {
        if (EntityGet(app, state->deferredPopovers[i])) {
            continue;
        }
        for (int j = i; j < state->deferredPopovers.len - 1; j++) {
            state->deferredPopovers[j] = state->deferredPopovers[j + 1];
        }
        state->deferredPopovers.len--;
    }
    return state->deferredPopovers.len > 0;
}

} // namespace gpui
