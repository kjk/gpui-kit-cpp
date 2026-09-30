#include "ui/native_menu.h"
#include "gpui/platform.h"
#include "base/positioner.h"

namespace gpui {

namespace component {

NativeMenu* NativeMenu::New(Ctx* cx) {
    Arena* a = cx->a;
    NativeMenu* m = ArenaNew<NativeMenu>(a);
    m->a = a;
    m->cx = cx;
    return m;
}

static NativeMenuItem* PushItem(NativeMenu* m) {
    if (!m->items.Append(m->a, NativeMenuItem{})) {
        return nullptr;
    }
    return &m->items[m->items.len - 1];
}

NativeMenu* NativeMenu::Menu(Str label, intptr_t id) {
    return MenuWithDisabled(label, false, id);
}
NativeMenu* NativeMenu::MenuWithDisabled(Str label, bool disabled,
                                         intptr_t id) {
    NativeMenuItem* it = PushItem(this);
    if (it) {
        it->kind = NativeMenuItemKind::Item;
        it->label = label;
        it->disabled = disabled;
        it->id = id;
    }
    return this;
}
NativeMenu* NativeMenu::MenuWithCheck(Str label, bool checked, intptr_t id) {
    NativeMenuItem* it = PushItem(this);
    if (it) {
        it->kind = NativeMenuItemKind::Item;
        it->label = label;
        it->checked = checked;
        it->id = id;
    }
    return this;
}
NativeMenu* NativeMenu::MenuWithIcon(Str label, IconName icon, intptr_t id) {
    NativeMenuItem* it = PushItem(this);
    if (it) {
        it->kind = NativeMenuItemKind::Item;
        it->label = label;
        it->icon = icon;
        it->id = id;
    }
    return this;
}
NativeMenu* NativeMenu::MenuWithIcon(Str label, component::Icon* icon,
                                     intptr_t id) {
    NativeMenuItem* it = PushItem(this);
    if (it) {
        it->kind = NativeMenuItemKind::Item;
        it->label = label;
        it->id = id;
        if (icon) {
            it->icon = icon->name;
            if (icon->source == component::IconSource::Data) {
                it->iconSvg = icon->data;
            } else {
                it->iconPath = icon->path;
            }
        }
    }
    return this;
}
NativeMenu* NativeMenu::Separator() {
    NativeMenuItem* it = PushItem(this);
    if (it) {
        it->kind = NativeMenuItemKind::Separator;
    }
    return this;
}
NativeMenu* NativeMenu::Submenu(Str label, NativeMenu* menu) {
    NativeMenuItem* it = PushItem(this);
    if (it) {
        it->kind = NativeMenuItemKind::Submenu;
        it->label = label;
        it->submenu = menu;
    }
    return this;
}
NativeMenu* NativeMenu::OnSelect(Listener l) {
    onSelect = l;
    return this;
}

int NativeMenuSelectable(const NativeMenu* m, const NativeMenuItem** out,
                         int cap) {
    if (!m) {
        return 0;
    }
    int n = 0;
    for (const NativeMenuItem& it : m->items) {
        if (it.kind == NativeMenuItemKind::Separator) {
            continue;
        }
        if (it.kind == NativeMenuItemKind::Submenu) {
            // A submenu row reports nothing itself; the rows under it are
            // numbered where they are built, which is right after it.
            n += NativeMenuSelectable(it.submenu, out ? out + n : nullptr,
                                      cap - n);
            continue;
        }
        // A greyed row cannot be chosen, so it is given no id at all.
        if (it.disabled) {
            continue;
        }
        if (out && n < cap) {
            out[n] = &it;
        }
        n++;
    }
    return n;
}

// The rows as the platform takes them: the same tree, with every row that can
// be chosen numbered by its place in the selectable order, so the id the OS
// answers with is an index back into that table.
static PlatMenuItem* ToPlat(Arena* a, const NativeMenu* m, int* nextId) {
    if (!m || m->items.len == 0) {
        return nullptr;
    }
    auto* out = (PlatMenuItem*)a
                    ->Push((uint64_t)m->items.len * sizeof(PlatMenuItem),
                           alignof(PlatMenuItem), true);
    int i = -1;
    for (const NativeMenuItem& it : m->items) {
        PlatMenuItem& p = out[++i];
        p.label = StrDup(a, it.label).s;
        p.disabled = it.disabled;
        p.checked = it.checked;
        if (it.kind == NativeMenuItemKind::Separator) {
            p.separator = true;
            continue;
        }
        if (it.kind == NativeMenuItemKind::Submenu) {
            p.submenu = ToPlat(a, it.submenu, nextId);
            p.submenuN = it.submenu ? it.submenu->items.len : 0;
            continue;
        }
        // resolve_icon_image: SVG source is handed over as it is; a path, or
        // the name's path, is what the backend looks up.
        if (it.iconSvg.s) {
            p.iconSvg = StrDup(a, it.iconSvg).s;
            p.iconSvgLen = len(it.iconSvg);
        } else if (it.iconPath.s) {
            p.iconPath = StrDup(a, it.iconPath).s;
        } else if (it.icon != IconName::None) {
            p.iconPath = StrDup(a, IconNamePath(it.icon)).s;
        }
        if (!it.disabled) {
            p.id = (*nextId)++;
        }
    }
    return out;
}

// The OS's own menu, and onSelect for the row chosen in it.
static bool ShowNative(NativeMenu* m, float x, float y) {
    int nextId = 1;
    int nItems = m->items.len;
    PlatMenuItem* plat = ToPlat(m->a, m, &nextId);
    bool dark = ThemeGet(m->cx->app) == ThemeMode::Dark;
    Listener select = m->onSelect;
    App* app = m->cx->app;
    Window* win = m->cx->win;

    // Snapshot ids before PlatShowMenu: the OS tracking loop can paint, which
    // resets the frame arena this menu lives on.
    int count = NativeMenuSelectable(m, nullptr, 1 << 20);
    intptr_t* ids = nullptr;
    if (count > 0) {
        auto** table =
            (const NativeMenuItem**)malloc((size_t)count * sizeof(void*));
        ids = (intptr_t*)malloc((size_t)count * sizeof(intptr_t));
        if (!table || !ids) {
            free(table);
            free(ids);
            return false;
        }
        NativeMenuSelectable(m, table, count);
        for (int i = 0; i < count; i++) {
            ids[i] = table[i] ? table[i]->id : 0;
        }
        free(table);
    }

    int chosen = PlatShowMenu(win, plat, nItems, x, y, dark);
    intptr_t command = 0;
    if (chosen > 0 && chosen <= count && ids) {
        command = ids[chosen - 1];
    }
    free(ids);
    if (command == 0) {
        return true;
    }
    ClickEvent ev = {};
    ListenerCall(app, win, ListenerFill(select, command), &ev);
    return true;
}

bool NativeMenu::Show(float x, float y) {
    if (len(items) == 0 || !cx) {
        return false;
    }
    // native_menu/mod.rs show: macOS and Windows pop the OS menu; every other
    // target goes to fallback::show, the drawn menu Root holds.
    if (PlatHasMenu()) {
        return ShowNative(this, x, y);
    }
    return NativeMenuShowFallback(cx, this, x, y);
}

// ─── the drawn fallback — native_menu/fallback.rs ─────────────────────────

// The copy the overlay keeps: the builder is on the frame arena and the menu
// has to outlive the frame that asked for it, which is what Rust's overlay
// owning its `Vec<NativeMenuItem>` does. `order` collects every menu of the
// tree, preorder, the root first.
static NativeMenu* CopyMenu(Arena* a, const NativeMenu* m,
                            ArenaVec<const NativeMenu*>* order) {
    NativeMenu* out = ArenaNew<NativeMenu>(a);
    out->a = a;
    order->Append(a, out);
    for (const NativeMenuItem& it : m->items) {
        NativeMenuItem row = it;
        row.label = StrDup(a, it.label);
        row.iconPath = it.iconPath.s ? StrDup(a, it.iconPath) : Str{};
        row.iconSvg = it.iconSvg.s ? StrDup(a, it.iconSvg) : Str{};
        row.submenu = it.submenu ? CopyMenu(a, it.submenu, order) : nullptr;
        out->items.Append(a, row);
    }
    return out;
}

NativeMenuFallback::~NativeMenuFallback() {
    if (a) {
        ArenaDelete(a);
    }
}

NativeMenuFallback* NativeMenuFallbackOf(Window* win) {
    if (!win) {
        return nullptr;
    }
    // WindowState::native_menu_overlay: one per window, dropped with it.
    uint32_t key = (uint32_t)HashClickId(StrL("gpui-native-menu-fallback"));
    return (NativeMenuFallback*)WindowKeyedState(
        win, key, new NativeMenuFallback(), &EntityDropT<NativeMenuFallback>);
}

// One PopupMenu state per menu in the tree, keyed on the window rather than
// on an element path: the menu is shown from whichever handler asked and
// drawn from Root, and both have to reach the same state.
static Entity<PopupMenuState> FallbackState(Ctx* cx, int k) {
    uint32_t name =
        (uint32_t)HashClickId(Str(fmt("native-menu-fallback-%d", k).s));
    uint32_t kind = (uint32_t)HashClickId(StrL("gpui::PopupMenuState"));
    return KeyedEntity<PopupMenuState>(cx, KeyedKey(name, kind));
}

bool NativeMenuShowFallback(Ctx* cx, const NativeMenu* m, float x, float y) {
    NativeMenuFallback* f = cx ? NativeMenuFallbackOf(cx->win) : nullptr;
    if (!f || !m || len(m->items) == 0) {
        return false;
    }
    if (f->a) {
        ArenaDelete(f->a);
    }
    f->a = ArenaNew();
    f->menus = {};
    CopyMenu(f->a, m, &f->menus);
    f->nStates = len(f->menus);
    f->states = (Entity<PopupMenuState>*)Alloc(
        f->a, f->nStates * (int)sizeof(Entity<PopupMenuState>));
    for (int k = 0; k < len(f->menus); k++) {
        Entity<PopupMenuState> st = FallbackState(cx, k);
        f->states[k] = st;
        if (PopupMenuState* s = st.Get(cx)) {
            // Nothing the last menu left behind — a submenu the pointer was
            // in, a row it had selected — carries over into this one.
            s->open = false;
            s->selected = -1;
            s->openSubmenu = -1;
            s->scrollY = 0;
            s->parent = {};
            s->side = Side::Right;
        }
    }
    f->onSelect = m->onSelect;
    f->position = {x, y};
    // Rust dismisses on the next press outside the menu, the drawn menu here
    // on the next release outside it (PopupMenuState::OnPressOutside). The
    // press the menu was opened under or after — the story's on_mouse_down,
    // the input's on_mouse_up — is not one: only a release ending a later
    // press is.
    f->openedPress = cx->win->lastDownAt;
    f->open = true;
    // `menu.focus_handle(cx).focus(window, cx)`: the menu arms its focus trap
    // as it draws, and remembers where focus was so dismissal gives it back.
    if (PopupMenuState* root = f->states[0].Get(cx)) {
        PopupMenuOpen(root, cx);
    }
    Notify(cx);
    return true;
}

const NativeMenuItem* NativeMenuFallbackRow(const NativeMenuFallback* f,
                                            EntityId state, int row) {
    if (!f || !f->open) {
        return nullptr;
    }
    int k = -1;
    for (int i = 0; i < f->nStates; i++) {
        if (f->states[i].id == state) {
            k = i;
            break;
        }
    }
    if (k < 0 || row < 0 || row >= len(f->menus[k]->items)) {
        return nullptr;
    }
    const NativeMenuItem* it = &f->menus[k]->items[row];
    // build_popup maps `Item { action: None, .. }` to nothing, and a greyed
    // or submenu row has nothing to run: only the rows the OS would number.
    if (it->kind != NativeMenuItemKind::Item || it->disabled) {
        return nullptr;
    }
    return it;
}

// A row confirmed in one of the drawn menus, by click or Enter: every menu
// of the tree goes, then the row's value reaches the caller — Rust dispatches
// the row's action to the focus the menu was opened over, which dismissal
// has just given back.
static void OnFallbackConfirm(PopupMenuState* self, Ctx* cx, const ClickEvent*,
                              intptr_t row) {
    NativeMenuFallback* f = NativeMenuFallbackOf(cx->win);
    const NativeMenuItem* it = NativeMenuFallbackRow(f, cx->self, (int)row);
    if (!it) {
        return;
    }
    intptr_t id = it->id;
    Listener select = f->onSelect;
    PopupMenuDismissAll(self, cx);
    f->open = false;
    ClickEvent ev = {};
    ListenerCall(cx->app, cx->win, ListenerFill(select, id), &ev);
}

// on_mouse_down_out, less the release of the press the menu came up under.
static void OnFallbackPressOutside(PopupMenuState* self, Ctx* cx,
                                   const MouseUpEvent* ev) {
    NativeMenuFallback* f = NativeMenuFallbackOf(cx->win);
    if (f && f->openedPress == cx->win->lastDownAt) {
        return;
    }
    PopupMenuState::OnPressOutside(self, cx, ev);
}

// The rows, the way fallback.rs build_popup maps them: a separator, an item
// with its icon, check and greyed state, and a submenu built the same way.
using SubmenuFn = PopupMenu* (*)(Ctx * cx, const NativeMenu* sub, void* user);

static void AddRows(Ctx* cx, PopupMenu* menu, const NativeMenu* m,
                    SubmenuFn submenu, void* user) {
    for (const NativeMenuItem& it : m->items) {
        if (it.kind == NativeMenuItemKind::Separator) {
            menu->Separator();
            continue;
        }
        if (it.kind == NativeMenuItemKind::Submenu) {
            menu->Submenu(it.label,
                          it.submenu ? submenu(cx, it.submenu, user) : nullptr);
            menu->Disabled(it.disabled);
            continue;
        }
        if (it.iconSvg.s || it.iconPath.s) {
            component::Icon* icon = component::Icon::New(cx, it.icon);
            if (it.iconSvg.s) {
                icon->Data(it.iconSvg);
            } else {
                icon->Path(it.iconPath);
            }
            menu->Menu(it.label, icon);
        } else {
            menu->Menu(it.label, it.icon);
        }
        menu->Checked(it.checked);
        menu->Disabled(it.disabled);
    }
}

struct FallbackBuild {
    NativeMenuFallback* f = nullptr;
    int next = 0;
};

// The submenus come in the order CopyMenu numbered them, so the nth menu
// built is the nth state.
static PopupMenu* BuildFallbackMenu(Ctx* cx, const NativeMenu*, void* user) {
    auto* b = (FallbackBuild*)user;
    int k = b->next++;
    if (k >= b->f->nStates) {
        return nullptr;
    }
    Entity<PopupMenuState> st = b->f->states[k];
    if (PopupMenuState* s = st.Get(cx)) {
        s->onConfirm = ListenTo(st, &OnFallbackConfirm);
    }
    PopupMenu* menu = PopupMenu::New(
        cx, StrDup(cx->a, Str(fmt("native-menu-fallback-%d", k).s)), st);
    AddRows(cx, menu, b->f->menus[k], &BuildFallbackMenu, user);
    return menu;
}

El* NativeMenuFallbackOverlay(Ctx* cx) {
    NativeMenuFallback* f = cx ? NativeMenuFallbackOf(cx->win) : nullptr;
    if (!f || !f->open || f->nStates == 0) {
        return nullptr;
    }
    PopupMenuState* root = f->states[0].Get(cx);
    if (!root || !root->open) {
        // Dismissed: escape, a release outside, or a row chosen. Rust's
        // DismissEvent subscription clears `active` the same way.
        f->open = false;
        return nullptr;
    }
    FallbackBuild b;
    b.f = f;
    PopupMenu* menu = BuildFallbackMenu(cx, f->menus[0], &b);
    if (!menu) {
        return nullptr;
    }
    El* el = menu->IntoEl();
    el->OnMouseUpOut(ListenTo(f->states[0], &OnFallbackPressOutside));
    // deferred(anchored().position(p).snap_to_window_with_margin(px(8.)))
    // .with_priority(POPUP_PRIORITY)
    return Positioner::Corner(cx, Anchor::TopLeft, f->position)
        ->Margin(8.f)
        ->Child(el)
        ->IntoEl()
        ->DeferredLayer(kPaintLayerPopup);
}

struct IntoBuild {
    Str id;
    int next = 0;
};

static PopupMenu* IntoSubmenu(Ctx* cx, const NativeMenu* sub, void* user) {
    auto* b = (IntoBuild*)user;
    // A submenu is a menu of its own with state of its own: sharing its
    // parent's would open and select both together. They are numbered in
    // the order they are built, the root being 0.
    Str subId = StrDup(cx->a, Str(fmt("%s/%d", b->id, ++b->next).s));
    PopupMenu* menu = PopupMenu::New(cx, subId);
    AddRows(cx, menu, sub, &IntoSubmenu, user);
    return menu;
}

PopupMenu* NativeMenu::IntoPopupMenu(Str id) const {
    PopupMenu* menu = PopupMenu::New(cx, id);
    IntoBuild b;
    b.id = id;
    AddRows(cx, menu, this, &IntoSubmenu, &b);
    return menu;
}

} // namespace component
} // namespace gpui
