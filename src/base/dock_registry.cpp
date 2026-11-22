#include "base/dock_registry.h"

namespace gpui {

PanelRegistry::PanelRegistry() {
    arena = ArenaNew();
}

PanelRegistry::~PanelRegistry() {
    VecReset(items);
    if (arena) {
        ArenaDelete(arena);
    }
}

void PanelRegistry::Register(Str panelName, PanelRegistryBuild build,
                             void* data) {
    if (!panelName.s || !build) {
        return;
    }
    for (int i = 0; i < len(items); i++) {
        if (base::StrEq(items[i].name, panelName)) {
            items[i].build = build;
            items[i].data = data;
            return;
        }
    }
    PanelRegistryEntry entry;
    entry.name = StrDup(arena, panelName);
    entry.build = build;
    entry.data = data;
    VecAppend(items, entry);
}

bool PanelRegistry::BuildPanel(Str panelName, const PanelBuildContext* context,
                               Window* win, App* app, DockPanelDef* out) const {
    if (!out) {
        return false;
    }
    for (int i = 0; i < len(items); i++) {
        if (!base::StrEq(items[i].name, panelName)) {
            continue;
        }
        *out = items[i].build(context, win, app, items[i].data);
        if (!out->name.s) {
            out->name = items[i].name;
        }
        return true;
    }
    return false;
}

PanelRegistry* PanelRegistryGlobal(App* app) {
    return app ? AppGlobalEnsure<PanelRegistry>(app) : nullptr;
}

void register_panel(App* app, Str panelName, PanelRegistryBuild build,
                    void* data) {
    if (PanelRegistry* registry = PanelRegistryGlobal(app)) {
        registry->Register(panelName, build, data);
    }
}

} // namespace gpui
