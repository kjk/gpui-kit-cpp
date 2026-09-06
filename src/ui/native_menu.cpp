#include "ui/native_menu.h"
#include "gpui/platform.h"

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
            p.iconSvgLen = it.iconSvg.len;
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

bool NativeMenu::Show(float x, float y) {
    if (items.len == 0 || !PlatHasMenu()) {
        return false;
    }
    int nextId = 1;
    int nItems = items.len;
    PlatMenuItem* plat = ToPlat(a, this, &nextId);
    bool dark = ThemeGet(cx->app) == ThemeMode::Dark;
    Listener select = onSelect;
    App* app = cx->app;
    Window* win = cx->win;

    // Snapshot ids before PlatShowMenu: the OS tracking loop can paint, which
    // resets the frame arena this menu lives on.
    int count = NativeMenuSelectable(this, nullptr, 1 << 20);
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
        NativeMenuSelectable(this, table, count);
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

PopupMenu* NativeMenu::IntoPopupMenu(Str id) const {
    PopupMenu* menu = PopupMenu::New(cx, id);
    for (const NativeMenuItem& it : items) {
        if (it.kind == NativeMenuItemKind::Separator) {
            menu->Separator();
            continue;
        }
        if (it.kind == NativeMenuItemKind::Submenu) {
            menu->Submenu(it.label,
                          it.submenu ? it.submenu->IntoPopupMenu(id) : nullptr);
            menu->Disabled(it.disabled);
            continue;
        }
        if (it.checked) {
            menu->MenuWithCheck(it.label, it.checked);
        } else if (it.iconSvg.s || it.iconPath.s) {
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
        menu->Disabled(it.disabled);
    }
    return menu;
}

} // namespace component
} // namespace gpui
