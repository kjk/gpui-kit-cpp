#include "shell/dock.h"

#include "shell/scope.h"

namespace gpui::shell {

struct ScriptPanelRegistration {
    Str name = {};
    ShellPanelScript script = {};
};

struct ScriptPanelData {
    App* app = nullptr;
    ScriptPanelRegistration* registration = nullptr;
    Entity<ScriptView> view = {};
    FocusHandle focus = {};
    Str serialized = {};
    bool serializedIsJson = false;
};

struct ScriptPanelManager {
    Vec<ScriptPanelRegistration*> registrations;
    Vec<ScriptPanelData*> panels;

    ~ScriptPanelManager() {
        for (int i = 0; i < len(panels); i++) {
            StrFree(panels[i]->serialized);
            delete panels[i];
        }
        for (int i = 0; i < len(registrations); i++) {
            ScriptPanelRegistration* registration = registrations[i];
            if (registration->script.release)
                registration->script.release(registration->script.data);
            delete registration;
        }
        VecReset(panels);
        VecReset(registrations);
    }
};

struct PanelNameTable {
    Arena* arena = ArenaNew();
    Mutex mutex;
    Vec<Str> names;

    ~PanelNameTable() {
        VecReset(names);
        ArenaDelete(arena);
    }
};

static PanelNameTable& Names() {
    static PanelNameTable table;
    return table;
}

static Str InternPanelName(Str name) {
    PanelNameTable& table = Names();
    table.mutex.Lock();
    for (int i = 0; i < table.names.len; i++) {
        if (StrEq(table.names[i], name)) {
            Str found = table.names[i];
            table.mutex.Unlock();
            return found;
        }
    }
    Str interned = StrDup(table.arena, name);
    VecAppend(table.names, interned);
    table.mutex.Unlock();
    return interned;
}

Str ShellPanelName(Str application, Str panel) {
    StrBuilder name;
    name.Append(StrL("shell:"));
    name.Append(application);
    name.AppendChar('/');
    name.Append(panel);
    Str value = name.TakeStr();
    Str interned = InternPanelName(value);
    StrFree(value);
    return interned;
}

static Str QualifiedPanelName(Str name) {
    if (StrStartsWith(name, "shell:")) return InternPanelName(name);
    StrBuilder qualified;
    qualified.Append(StrL("shell:"));
    qualified.Append(name);
    Str value = qualified.TakeStr();
    Str interned = InternPanelName(value);
    StrFree(value);
    return interned;
}

static ScriptPanelManager* PanelManager(App* app) {
    return AppGlobalEnsure<ScriptPanelManager>(app);
}

static ScriptPanelData* NewPanelData(App* app,
                                     ScriptPanelRegistration* registration,
                                     Entity<ScriptView> view) {
    ScriptPanelManager* manager = PanelManager(app);
    if (!manager) return nullptr;
    auto* panel = new ScriptPanelData();
    panel->app = app;
    panel->registration = registration;
    panel->view = view;
    panel->focus = FocusHandleNew(app);
    VecAppend(manager->panels, panel);
    return panel;
}

static El* RenderPanel(Ctx* cx, void* data) {
    auto* panel = (ScriptPanelData*)data;
    El* root = Div(cx->a)
                   ->TrackFocus(panel ? panel->focus : FocusHandle{})
                   ->SizeFull();
    if (panel && panel->view.IsValid())
        root->Child(EntityRender(cx->app, cx->win, cx->a, panel->view.id));
    return root;
}

static void DumpPanel(void* data, PanelStateNode* out) {
    auto* panel = (ScriptPanelData*)data;
    if (!panel || !out) return;
    if (!panel->view.IsValid()) {
        out->info = panel->serialized;
        out->infoIsJson = panel->serializedIsJson;
        return;
    }
    ShellPanelScript* script =
        panel->registration ? &panel->registration->script : nullptr;
    if (!script || !script->serialize) return;
    StrBuilder encoded;
    if (!script->serialize(panel->view, panel->app, script->data, &encoded))
        return;
    Str json = encoded.TakeStr();
    Arena* check = ArenaNew();
    bool valid = JsonParse(check, json) != nullptr;
    ArenaDelete(check);
    if (!valid) {
        StrFree(json);
        return;
    }
    StrFree(panel->serialized);
    panel->serialized = json;
    panel->serializedIsJson = true;
    out->info = panel->serialized;
    out->infoIsJson = true;
}

static DockPanelDef PanelDef(ScriptPanelData* panel) {
    DockPanelDef def;
    if (!panel) return def;
    ShellPanelScript* script =
        panel->registration ? &panel->registration->script : nullptr;
    def.name =
        panel->registration ? panel->registration->name : StrL("shell:panel");
    def.title = def.name;
    def.render = RenderPanel;
    def.dump = DumpPanel;
    def.data = panel;
    def.closable = script ? script->closable : true;
    def.visible = script ? script->visible : true;
    def.canZoom = script ? script->zoomable : true;
    def.zoomable = def.canZoom ? DockPanelControl::Menu : DockPanelControl::No;
    return def;
}

DockPanelDef ScriptPanelNew(App* app, Str name, Entity<ScriptView> view,
                            const ShellPanelScript* script) {
    auto* registration = new ScriptPanelRegistration();
    registration->name = QualifiedPanelName(name);
    if (script) registration->script = *script;
    ScriptPanelManager* manager = PanelManager(app);
    if (!manager) {
        delete registration;
        return {};
    }
    VecAppend(manager->registrations, registration);
    return PanelDef(NewPanelData(app, registration, view));
}

static DockPanelDef BuildRegisteredPanel(const PanelBuildContext* context,
                                         Window* window, App* app, void* data) {
    auto* registration = (ScriptPanelRegistration*)data;
    Entity<ScriptView> view = {};
    if (registration && registration->script.build)
        view = registration->script
                   .build(window, app, registration->script.data);
    ScriptPanelData* panel = NewPanelData(app, registration, view);
    if (!panel) return {};
    if (context && context->state && context->state->info) {
        if (view.IsValid() && registration->script.deserialize) {
            registration->script.deserialize(view, context->state->info, window,
                                             app, registration->script.data);
        } else if (!view.IsValid()) {
            panel->serialized = StrDup(context->state->info);
            panel->serializedIsJson = context->state->infoIsJson;
        }
    }
    return PanelDef(panel);
}

Str ShellRegisterPanel(App* app, Str application, Str panel,
                       const ShellPanelScript& script) {
    ScriptPanelManager* manager = PanelManager(app);
    if (!manager) return {};
    Str name = ShellPanelName(application, panel);
    for (int i = 0; i < manager->registrations.len; i++) {
        ScriptPanelRegistration* registration = manager->registrations[i];
        if (!StrEq(registration->name, name)) continue;
        if (registration->script.release)
            registration->script.release(registration->script.data);
        registration->script = script;
        register_panel(app, name, BuildRegisteredPanel, registration);
        return name;
    }
    auto* registration = new ScriptPanelRegistration();
    registration->name = name;
    registration->script = script;
    VecAppend(manager->registrations, registration);
    register_panel(app, name, BuildRegisteredPanel, registration);
    return name;
}

static CallScopeGuard EnterLayout(Ctx* cx) {
    return ScopeEnter(cx ? cx->win : nullptr, cx ? cx->app : nullptr,
                      ScopePhase::Layout, ScopeCurrentView(),
                      ScopeCurrentPolicy(), ScopeCurrentRuntime(),
                      ScopeCurrentApplication());
}

// The container being drawn, and the dock's own content while the `dock` hook
// runs. Single-threaded by construction: the VM and the element tree are both
// the main thread's, and the two ends of each are a chrome hook and the
// materialization inside it, with base's own code in between.
static ShellDockChromeFrame gChromeFrame = {};
static El* gDockContent = nullptr;

const ShellDockChromeFrame* ShellDockCurrentChrome() {
    return gChromeFrame.dock ? &gChromeFrame : nullptr;
}

El* ShellDockTakeContent() {
    El* content = gDockContent;
    gDockContent = nullptr;
    return content;
}

namespace {

// The previous occupant is put back rather than cleared, so a dock area
// nested inside a panel of another one is no different from one on its own.
struct ChromeFrameGuard {
    ShellDockChromeFrame saved = {};

    ChromeFrameGuard(ScriptDockSkin* skin, const DockTabGroup* group,
                     const DockCtx* dock) {
        saved = gChromeFrame;
        gChromeFrame.dock = skin ? skin->dock : 0;
        gChromeFrame.group = group;
        gChromeFrame.dockCtx = dock;
    }
    ~ChromeFrameGuard() { gChromeFrame = saved; }
};

struct ContentSlotGuard {
    El* saved = nullptr;

    explicit ContentSlotGuard(El* content) {
        saved = gDockContent;
        gDockContent = content;
    }
    ~ContentSlotGuard() { gDockContent = saved; }
};

// The script half of one hook: the payload, the handler in force for the
// frame, and the description the runtime replays for it. Null whenever the
// script asked to draw nothing, which is what leaves base's own default.
El* DrawScriptChrome(Ctx* cx, ScriptDockSkin* skin, DockChromeSlot slot,
                     uint64_t key, CallbackId handler, Str payload) {
    if (!skin || !skin->runtime || !handler) return nullptr;
    return ShellDockDrawChrome(cx, skin->runtime, skin->dock, slot, key,
                               handler, payload);
}

DockState* SkinState(ScriptDockSkin* skin, Ctx* cx) {
    if (!skin || !skin->runtime || !cx) return nullptr;
    RetainedEntry* entry = skin->runtime->Retained(skin->dock);
    if (!entry || entry->kind != RetainedKind::Dock) return nullptr;
    return entry->dock.Get(cx);
}

} // namespace

static El* SkinTabBar(Ctx* cx, void* data, const DockTabGroup* group) {
    auto* skin = (ScriptDockSkin*)data;
    CallScopeGuard guard = EnterLayout(cx);
    ChromeFrameGuard frame(skin, group, nullptr);
    if (skin && skin->chrome.tabBar)
        return skin->chrome.tabBar(cx, skin->chrome.data, group);
    if (!skin || !skin->hooks || !skin->hooks->tabBar) return nullptr;
    StrBuilder payload;
    ShellTabGroupData(group, &payload);
    Str json = payload.TakeStr();
    El* drawn = DrawScriptChrome(cx, skin, DockChromeSlot::TabBar,
                                 (uint64_t)(group ? group->node : -1),
                                 skin->hooks->tabBar, json);
    StrFree(json);
    return drawn;
}

static El* SkinEmptyGroup(Ctx* cx, void* data, const DockTabGroup* group) {
    auto* skin = (ScriptDockSkin*)data;
    CallScopeGuard guard = EnterLayout(cx);
    ChromeFrameGuard frame(skin, group, nullptr);
    if (skin && skin->chrome.emptyGroup)
        return skin->chrome.emptyGroup(cx, skin->chrome.data, group);
    if (!skin || !skin->hooks || !skin->hooks->emptyGroup) return nullptr;
    StrBuilder payload;
    ShellTabGroupData(group, &payload);
    Str json = payload.TakeStr();
    El* drawn = DrawScriptChrome(cx, skin, DockChromeSlot::EmptyGroup,
                                 (uint64_t)(group ? group->node : -1),
                                 skin->hooks->emptyGroup, json);
    StrFree(json);
    return drawn;
}

static El* SkinDropIndicator(Ctx* cx, void* data, Bounds bounds) {
    auto* skin = (ScriptDockSkin*)data;
    CallScopeGuard guard = EnterLayout(cx);
    ChromeFrameGuard frame(skin, nullptr, nullptr);
    if (skin && skin->chrome.dropIndicator)
        return skin->chrome.dropIndicator(cx, skin->chrome.data, bounds);
    if (!skin || !skin->hooks || !skin->hooks->dropIndicator) return nullptr;
    StrBuilder payload;
    ShellDropIndicatorData(SkinState(skin, cx), bounds, &payload);
    Str json = payload.TakeStr();
    El* drawn = DrawScriptChrome(cx, skin, DockChromeSlot::DropIndicator, 0,
                                 skin->hooks->dropIndicator, json);
    StrFree(json);
    return drawn;
}

static El* SkinDock(Ctx* cx, void* data, const DockCtx* dock, El* content) {
    auto* skin = (ScriptDockSkin*)data;
    CallScopeGuard guard = EnterLayout(cx);
    ChromeFrameGuard frame(skin, nullptr, dock);
    if (skin && skin->chrome.dock)
        return skin->chrome.dock(cx, skin->chrome.data, dock, content);
    if (!skin || !skin->hooks || !skin->hooks->dock) return content;

    StrBuilder payload;
    ShellDockData(dock, &payload);
    Str json = payload.TakeStr();
    El* drawn = nullptr;
    El* unplaced = nullptr;
    {
        ContentSlotGuard slot(content);
        drawn = DrawScriptChrome(cx, skin, DockChromeSlot::Dock,
                                 (uint64_t)(dock ? (int)dock->placement : 0),
                                 skin->hooks->dock, json);
        unplaced = ShellDockTakeContent();
    }
    StrFree(json);
    if (!drawn) return unplaced ? unplaced : content;
    if (unplaced) {
        // A chrome that never placed the content is not a chrome that wanted
        // it gone — it is one that forgot — so what is left over is drawn
        // after what the script returned.
        logf(
            "shell: a dock's chrome handler drew no dock_content(), so the "
            "dock's own panels are drawn after it\n");
        drawn = Div(cx->a)->SizeFull()->Child(drawn)->Child(unplaced);
    }
    // Chrome only. The dock's own box — its extent along its own axis and the
    // `flex_none` that holds it there — is `DockFrame`'s, applied by base
    // around whatever this returns, so a script cannot misplace a dock by not
    // knowing it had a box to draw and this must not state it twice.
    return drawn;
}

ScriptDockSkin::ScriptDockSkin() {
    renderer.data = this;
    renderer.tabBar = SkinTabBar;
    renderer.emptyGroup = SkinEmptyGroup;
    renderer.dropIndicator = SkinDropIndicator;
    renderer.dock = SkinDock;
}

ScriptDockSkin::ScriptDockSkin(ShellDockChrome value) : ScriptDockSkin() {
    chrome = value;
}

const DockRenderer* ScriptDockSkin::Renderer() {
    renderer.data = this;
    return &renderer;
}

static const char* PlacementName(DockPlacement placement) {
    switch (placement) {
        case DockPlacement::Left:
            return "left";
        case DockPlacement::Right:
            return "right";
        case DockPlacement::Bottom:
            return "bottom";
        default:
            return "center";
    }
}

void ShellTabGroupData(const DockTabGroup* group, StrBuilder* out) {
    JsonWriter json;
    json.out = out;
    DockState* state =
        group && group->cx ? group->state.Get(group->cx) : nullptr;
    int active = group ? DockGroupActiveIx(group) : -1;
    json.BeginObject();
    json.Number("node", group ? group->node : -1);
    json.Number("active_index", active);
    json.Bool("zoomed",
              state && state->zoomPanel >= 0 &&
                  DockNodeOfPanel(state, state->zoomPanel) == group->node);
    json.Bool("collapsed", group && group->collapsed);
    json.Bool("locked", !state || DockNodeLocked(state, group->node));
    json.Bool("draggable", state && DockNodeDraggable(state, group->node));
    json.Bool("droppable", state && DockNodeDroppable(state, group->node));
    json.Bool("closable", state && !DockIsLastPanel(state, group->node));
    json.BeginArray("tabs");
    int count = group ? DockGroupCount(group) : 0;
    for (int i = 0; i < count; i++) {
        const DockPanelDef* panel = DockGroupPanel(group, i);
        if (!panel) continue;
        json.BeginObject();
        json.Number("index", i);
        json.String("name", panel->name);
        json.Number("id", (double)panel->id.AsU64());
        json.Bool("active", i == active);
        json.Bool("visible", panel->visible);
        json.Bool("closable", panel->closable);
        json.Bool("zoomable", panel->canZoom);
        json.EndObject();
    }
    json.EndArray();
    json.EndObject();
}

void ShellDockData(const DockCtx* dock, StrBuilder* out) {
    JsonWriter json;
    json.out = out;
    json.BeginObject();
    json.String("placement", Str(PlacementName(dock ? dock->placement
                                                    : DockPlacement::Center)));
    json.Number("size", dock ? dock->size : 0);
    json.Bool("open", dock && dock->open);
    json.Bool("collapsible", dock && dock->collapsible);
    json.EndObject();
}

static const char* DropPlacementName(DockDrop drop) {
    switch (drop) {
        case DockDrop::Left:
            return "left";
        case DockDrop::Right:
            return "right";
        case DockDrop::Top:
            return "top";
        case DockDrop::Bottom:
            return "bottom";
        default:
            return nullptr;
    }
}

void ShellDropIndicatorData(const DockState* state, Bounds to,
                            StrBuilder* out) {
    // `bounds` is the hovered group's own box, in window coordinates; `from`
    // and `to` are relative to it, and are the two ends of the placeholder the
    // skin animates between. Base has already sprung and clamped `to`, so the
    // hook only paints. A `placement` of null means the drop merges into the
    // group's tabs rather than splitting beside it.
    Bounds group = {};
    const char* placement = nullptr;
    Bounds from = to;
    if (state && state->dropNode >= 0 && state->dropNode < state->nodes.len) {
        group = state->nodes[state->dropNode].bounds;
        placement = DropPlacementName(state->dropAt);
        from = state->dropFrom;
        from.x -= group.x;
        from.y -= group.y;
    }
    JsonWriter json;
    json.out = out;
    json.BeginObject();
    if (placement) {
        json.String("placement", Str(placement));
    } else {
        json.Null("placement");
    }
    json.BeginObject("bounds");
    json.Number("x", group.x);
    json.Number("y", group.y);
    json.Number("width", group.w);
    json.Number("height", group.h);
    json.EndObject();
    json.BeginObject("from");
    json.Number("x", from.x);
    json.Number("y", from.y);
    json.Number("width", from.w);
    json.Number("height", from.h);
    json.EndObject();
    json.BeginObject("to");
    json.Number("x", to.x);
    json.Number("y", to.y);
    json.Number("width", to.w);
    json.Number("height", to.h);
    json.EndObject();
    json.EndObject();
}

bool ShellIsScriptPanel(const DockPanelDef& def) {
    return def.render == RenderPanel;
}

} // namespace gpui::shell
