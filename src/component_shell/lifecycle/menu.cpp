// crates/component-shell/src/shell/lifecycle/menu.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "base/global_state.h"
#include "shell/action.h"
#include "ui/menu.h"

#include <stdio.h>

namespace gpui::component_shell::lifecycle::menu {

// n value-initialized T from `a`.
template <class T>
static T* NewArray(Arena* a, int n) {
    T* items = (T*)Alloc(a, (int)sizeof(T) * n);
    for (int i = 0; i < n; i++) new (&items[i]) T();
    return items;
}

struct ItemSpec {
    Str label;
    Str action;
    bool disabled = false;
    bool checked = false;
};

struct Entry {
    enum Kind : uint8_t {
        Item,
        Separator,
    } kind = Item;
    ItemSpec item;
};

struct MenuSpec {
    Str label;
    bool disabled = false;
    ArenaVec<Entry> entries;
};

struct Label {
    Str value;
};

struct BoolOp {
    enum Kind : uint8_t {
        Disabled,
        Checked,
    } kind = Disabled;
    bool value = false;
};

// Rust folds BoolOp::Disabled here, but `disabled` is a registered common
// behavior: both runtimes validate the call against this descriptor and then
// record it as the common behavior, dropping the payload. So a MenuItem's (or
// a Menu's) `disabled(true)` never reaches this fold, in Rust or here; the
// fold is kept as Rust writes it.
static El* MaterializeItem(MaterializeRequest* request) {
    const ItemSpec* payload = request->PayloadAs<ItemSpec>();
    if (!payload) return request->Fail(StrL("MenuItem incompatible payload"));
    Ctx* cx = request->cx;
    Entry* entry = ArenaNew<Entry>(cx->a);
    entry->kind = Entry::Item;
    entry->item = *payload;
    EachMethod<BoolOp>(request, [&](const BoolOp& op) {
        if (op.kind == BoolOp::Disabled)
            entry->item.disabled = op.value;
        else
            entry->item.checked = op.value;
    });
    if (!RejectStyle(request, "MenuItem")) return nullptr;
    return CarrierOf(cx, entry);
}

static El* MaterializeSeparator(MaterializeRequest* request) {
    if (!RejectStyle(request, "MenuSeparator")) return nullptr;
    Entry* entry = ArenaNew<Entry>(request->cx->a);
    entry->kind = Entry::Separator;
    return CarrierOf(request->cx, entry);
}

static El* MaterializeMenu(MaterializeRequest* request) {
    const Label* label = request->PayloadAs<Label>();
    if (!label) return request->Fail(StrL("Menu incompatible payload"));
    Ctx* cx = request->cx;
    MenuSpec* spec = ArenaNew<MenuSpec>(cx->a);
    spec->label = label->value;
    EachMethod<BoolOp>(request, [&](const BoolOp& op) {
        if (op.kind == BoolOp::Disabled) spec->disabled = op.value;
    });
    if (!RejectStyle(request, "Menu")) return nullptr;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        const char* name = children[i].componentName;
        if (!name || (strcmp(name, "MenuItem") != 0 &&
                      strcmp(name, "MenuSeparator") != 0))
            return request
                ->Fail(StrL("Menu accepts only MenuItem or "
                            "MenuSeparator children"));
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        Entry* entry = TakeCarriedAs<Entry>(request, element, "Menu entry");
        if (!entry) return nullptr;
        spec->entries.Append(cx->a, *entry);
    }
    return CarrierOf(cx, spec);
}

// ─── The installed model ───────────────────────────────────────────────────

// build_menu: the gpui::Menu a spec describes, each item dispatching its
// ShellAction.
static MenuDef BuildMenu(Arena* a, const MenuSpec& spec) {
    MenuDef menu;
    menu.name = StrDup(a, spec.label);
    menu.disabled = spec.disabled;
    int n = len(spec.entries);
    MenuRow* rows = n ? NewArray<MenuRow>(a, n) : nullptr;
    for (int i = 0; i < n; i++) {
        const Entry& entry = spec.entries[i];
        rows[i] = MenuRow{};
        if (entry.kind == Entry::Separator) {
            rows[i].separator = true;
            continue;
        }
        rows[i].label = StrDup(a, entry.item.label);
        rows[i].action = shell::ShellActionOf(entry.item.action);
        rows[i].disabled = entry.item.disabled;
        rows[i].checked = entry.item.checked;
    }
    menu.items = rows;
    menu.n = n;
    return menu;
}

// restore_menu's half: a deep copy of an installed model, submenus and all,
// so the previous menus survive the install that replaces them.
static const MenuRow* CopyRows(Arena* a, const MenuRow* rows, int n) {
    if (!rows || n <= 0) return nullptr;
    MenuRow* out = NewArray<MenuRow>(a, n);
    for (int i = 0; i < n; i++) {
        out[i] = rows[i];
        out[i].label = StrDup(a, rows[i].label);
        out[i].submenu = CopyRows(a, rows[i].submenu, rows[i].submenuN);
    }
    return out;
}

struct OwnedMenus {
    Arena* a = nullptr;
    MenuDef* menus = nullptr;
    int count = 0;
};

static OwnedMenus* CopyMenus(const MenuDef* menus, int count) {
    OwnedMenus* owned = new OwnedMenus();
    owned->a = ArenaNew();
    owned->count = count;
    owned->menus = count ? NewArray<MenuDef>(owned->a, count) : nullptr;
    for (int i = 0; i < count; i++) {
        owned->menus[i] = menus[i];
        owned->menus[i].name = StrDup(owned->a, menus[i].name);
        owned->menus[i].items = CopyRows(owned->a, menus[i].items, menus[i].n);
    }
    return owned;
}

static void DropOwnedMenus(void* user) {
    OwnedMenus* owned = (OwnedMenus*)user;
    if (!owned) return;
    ArenaDelete(owned->a);
    delete owned;
}

// The cleanup: `cx.set_menus(previous)` and
// `GlobalState::set_app_menus(previous)`, which BaseSetAppMenus does as one.
// The in-window bar reads the global each frame, which is its `reload`.
static void RestoreMenus(App* app, void* user) {
    OwnedMenus* previous = (OwnedMenus*)user;
    BaseSetAppMenus(app, previous->menus, previous->count);
}

// The install: remember what is installed, then install the specs.
// Rust reads the previous menus from `cx.get_menus()`; the port's platform
// seam has no getter, and BaseSetAppMenus keeps the platform and the global
// in step, so the global's copy is the same model.
static shell::ComponentAppEffectCleanup InstallMenus(App* app, void* user) {
    OwnedMenus* specs = (OwnedMenus*)user;
    int count = 0;
    const MenuDef* current = BaseAppMenus(app, &count);
    OwnedMenus* previous = CopyMenus(current, count);
    BaseSetAppMenus(app, specs->menus, specs->count);
    shell::ComponentAppEffectCleanup cleanup;
    cleanup.run = &RestoreMenus;
    cleanup.drop = &DropOwnedMenus;
    cleanup.user = previous;
    return cleanup;
}

// A revision only has to change when the menu does. Rust hashes the specs
// with std's DefaultHasher; this is FNV-1a over the same fields, lengths
// included, so the value differs from Rust's and only equality is the
// contract.
static void HashBytes(uint64_t* h, const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) {
        *h ^= p[i];
        *h *= 1099511628211ull;
    }
}
static void HashStr(uint64_t* h, Str s) {
    uint64_t n = (uint64_t)len(s);
    HashBytes(h, &n, sizeof(n));
    if (n) HashBytes(h, s.s, (size_t)n);
}
static void HashBool(uint64_t* h, bool v) {
    uint8_t b = v ? 1 : 0;
    HashBytes(h, &b, 1);
}

static TempStr RevisionTemp(MenuSpec* const* specs, int count) {
    uint64_t h = 14695981039346656037ull;
    uint64_t n = (uint64_t)count;
    HashBytes(&h, &n, sizeof(n));
    for (int i = 0; i < count; i++) {
        HashStr(&h, specs[i]->label);
        HashBool(&h, specs[i]->disabled);
        uint64_t entries = (uint64_t)len(specs[i]->entries);
        HashBytes(&h, &entries, sizeof(entries));
        for (const Entry& entry : specs[i]->entries) {
            uint8_t kind = (uint8_t)entry.kind;
            HashBytes(&h, &kind, 1);
            if (entry.kind != Entry::Item) continue;
            HashStr(&h, entry.item.label);
            HashStr(&h, entry.item.action);
            HashBool(&h, entry.item.disabled);
            HashBool(&h, entry.item.checked);
        }
    }
    return fmt("%016llx", (unsigned long long)h);
}

// AppMenu::new over the global's menus: each installed menu as a PopupMenu
// whose rows dispatch their action.
static component::PopupMenu* PopupFor(Ctx* cx, Str id, const MenuRow* rows,
                                      int n) {
    component::PopupMenu* popup = component::PopupMenu::New(cx, id);
    for (int i = 0; i < n; i++) {
        const MenuRow& row = rows[i];
        if (row.separator) {
            popup->Separator();
        } else if (row.submenu) {
            popup->Submenu(row.label,
                           PopupFor(cx, StrDup(cx->a, fmt("%s-%d", id, i)),
                                    row.submenu, row.submenuN));
        } else {
            popup->MenuWithAction(row.label, row.action, row.arg)
                ->Disabled(row.disabled)
                ->Checked(row.checked);
        }
    }
    return popup;
}

static El* MaterializeBar(MaterializeRequest* request) {
    const Label* label = request->PayloadAs<Label>();
    if (!label) return request->Fail(StrL("MenuBar incompatible payload"));
    Str id = label->value;
    shell::ComponentAppEffects effects;
    if (!request->AppEffects(&effects)) return nullptr;
    Ctx* cx = request->cx;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    MenuSpec** specs = count ? NewArray<MenuSpec*>(cx->a, count) : nullptr;
    for (int i = 0; i < count; i++) {
        const char* name = children[i].componentName;
        if (!name || strcmp(name, "Menu") != 0)
            return request->Fail(StrL("MenuBar accepts only Menu children"));
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        specs[i] = TakeCarriedAs<MenuSpec>(request, element, "Menu");
        if (!specs[i]) return nullptr;
    }
    TempStr revision = RevisionTemp(specs, count);

    // window.use_keyed_state("shell-menu-bar:{id}", AppMenuBar::new).
    Str stateKey = StrDup(cx->a, fmt("shell-menu-bar:%s", id));
    Entity<component::AppMenuBarState> state =
        KeyedEntity<component::AppMenuBarState>(cx, KeyedName(cx, stateKey));

    OwnedMenus* install = new OwnedMenus();
    install->a = ArenaNew();
    install->count = count;
    install->menus = count ? NewArray<MenuDef>(install->a, count) : nullptr;
    for (int i = 0; i < count; i++)
        install->menus[i] = BuildMenu(install->a, *specs[i]);
    shell::ComponentAppEffectInstall effect;
    effect.run = &InstallMenus;
    effect.drop = &DropOwnedMenus;
    effect.user = install;
    Str error;
    if (!effects.Replace(fmt("menu-bar:%s", id), revision, cx->win, cx->app,
                         effect, &error, cx->a))
        return request->Fail(error);

    component::AppMenuBar* bar =
        component::AppMenuBar::New(cx, stateKey, state);
    int installed = 0;
    const MenuDef* menus = BaseAppMenus(cx->app, &installed);
    {
        IdScope scope(cx, stateKey);
        for (int i = 0; i < installed; i++) {
            bar->Menu(menus[i].name,
                      PopupFor(cx, StrDup(cx->a, fmt("menu-%d", i)),
                               menus[i].items, menus[i].n));
        }
    }
    return request->Finish(Div(cx->a)->Child(bar->IntoEl()));
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool ConstructItem(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::String ||
        len(StrTrim(args[0].string)) == 0 || len(StrTrim(args[1].string)) == 0)
        return build->Fail(StrL("MenuItem expects non-empty label and action"));
    ItemSpec* item = build->New<ItemSpec>();
    item->label = args[0].string;
    item->action = args[1].string;
    return true;
}

static bool ConstructSeparator(PayloadBuild* build, const ComponentArgument*,
                               int count) {
    if (count != 0)
        return build->Fail(StrL("MenuSeparator expects no arguments"));
    return build->Mark<Empty>();
}

// label_constructor(name).
template <int Which>
static bool ConstructLabel(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrim(args[0].string)) == 0)
        return build->Fail(Which == 0 ? StrL("Menu expects a non-empty label")
                                      : StrL("MenuBar expects a non-empty "
                                             "label"));
    build->New<Label>()->value = args[0].string;
    return true;
}

// bool_method("Menu", name, ..): MenuItem's methods name "Menu" too.
template <BoolOp::Kind Kind>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(Kind == BoolOp::Disabled
                               ? StrL("Menu.disabled expects one boolean")
                               : StrL("Menu.checked expects one boolean"));
    BoolOp* op = build->New<BoolOp>();
    op->kind = Kind;
    op->value = args[0].boolean;
    return true;
}

static constexpr ArgumentDescriptor kItemArgs[] = {{"label", SchemaString()},
                                                   {"action", SchemaString()}};
static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kCheckedArgs[] = {
    {"checked", SchemaBoolean()}};

static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"MenuItem", kItemArgs, &ConstructItem}};
static constexpr MethodDescriptor kItemMethods[] = {
    {"disabled", kDisabledArgs, "Sets native menu item state.",
     &RecordBool<BoolOp::Disabled>},
    {"checked", kCheckedArgs, "Sets native menu item state.",
     &RecordBool<BoolOp::Checked>},
};
static constexpr ComponentDescriptor kMenuItem = {
    "MenuItem", kItemConstructors, kItemMethods,
    "Typed application-menu action data.", &MaterializeItem};

static constexpr ConstructorDescriptor kSeparatorConstructors[] = {
    {"MenuSeparator", {}, &ConstructSeparator}};
static constexpr ComponentDescriptor kMenuSeparator = {
    "MenuSeparator",
    kSeparatorConstructors,
    {},
    "Typed application-menu separator data.",
    &MaterializeSeparator};

static constexpr ConstructorDescriptor kMenuConstructors[] = {
    {"Menu", kLabelArgs, &ConstructLabel<0>}};
static constexpr MethodDescriptor kMenuMethods[] = {
    {"disabled", kDisabledArgs, "Disables the whole menu.",
     &RecordBool<BoolOp::Disabled>},
};
static constexpr ComponentDescriptor kMenu = {
    "Menu", kMenuConstructors, kMenuMethods,
    "Typed top-level application menu data.", &MaterializeMenu};

static constexpr ConstructorDescriptor kBarConstructors[] = {
    {"MenuBar", kLabelArgs, &ConstructLabel<1>}};
static constexpr ComponentDescriptor kMenuBar = {
    "MenuBar",
    kBarConstructors,
    {},
    "A generation-owned native and in-window application menu bar.",
    &MaterializeBar};

} // namespace gpui::component_shell::lifecycle::menu

namespace gpui::component_shell {

bool RegisterLifecycleMenu(shell::ComponentRegistry* registry,
                           shell::RegistryError* error) {
    return registry->Register(&lifecycle::menu::kMenuItem, error) &&
           registry->Register(&lifecycle::menu::kMenuSeparator, error) &&
           registry->Register(&lifecycle::menu::kMenu, error) &&
           registry->Register(&lifecycle::menu::kMenuBar, error);
}

} // namespace gpui::component_shell
