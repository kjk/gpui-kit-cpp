#include "shell/retained.h"

#include "shell/dock.h"

#include <string.h>

namespace gpui::shell {

static uint32_t gNextRetainedStoreId = 1;

RetainedStore::RetainedStore() {
    storeId = gNextRetainedStoreId++;
    if (storeId == 0 || storeId >= (1u << 21)) {
        storeId = 0;
        gNextRetainedStoreId = 0;
    }
}

RetainedStore::~RetainedStore() {
    Clear();
}

bool RetainedStore::Belongs(EntityHandle handle, uint32_t* id) const {
    uint32_t store = (uint32_t)(handle >> 32);
    if (store != storeId) return false;
    if (id) *id = (uint32_t)handle;
    return true;
}

RetainedEntry* RetainedStore::Find(EntityHandle handle) const {
    uint32_t id = 0;
    if (!Belongs(handle, &id)) return nullptr;
    for (int i = 0; i < entries.len; i++) {
        if (entries[i]->id == id) return entries[i];
    }
    return nullptr;
}

RetainedEntry* RetainedStore::FindLocal(uint32_t id) const {
    for (int i = 0; i < entries.len; i++) {
        if (entries[i]->id == id) return entries[i];
    }
    return nullptr;
}

EntityHandle RetainedStore::Push(RetainedEntry* entry) {
    if (!entry || storeId == 0 || entries.len >= kMaxLiveEntities ||
        nextId == 0) {
        delete entry;
        return 0;
    }
    entry->id = nextId++;
    VecAppend(entries, entry);
    return ((uint64_t)storeId << 32) | entry->id;
}

EntityHandle RetainedStore::CreateInput(bool textarea, Str placeholder,
                                        Str value, int rows, App* app,
                                        EntityId owner, void* application) {
    RetainedEntry* entry = new RetainedEntry();
    entry->kind = textarea ? RetainedKind::Textarea : RetainedKind::Input;
    entry->owner = owner;
    entry->application = application;
    entry->app = app;
    entry->input = new InputState();
    if (textarea) {
        entry->input->kind = InputKind::Textarea;
        entry->input->mode.kind = LayoutModeKind::PlainText;
        if (rows > 0) LayoutModeSetRows(&entry->input->mode, rows);
    }
    entry->input->focus = FocusHandleNew(app);
    if (placeholder) InputSetPlaceholder(entry->input, placeholder);
    if (value) InputSetValue(entry->input, value);
    return Push(entry);
}

EntityHandle RetainedStore::CreateSlider(float min, float max, float step,
                                         SliderScale scale, SliderValue value,
                                         App* app, EntityId owner,
                                         void* application) {
    RetainedEntry* entry = new RetainedEntry();
    entry->kind = RetainedKind::Slider;
    entry->owner = owner;
    entry->application = application;
    entry->app = app;
    entry->slider =
        new SliderState(SliderStateNew(min, max, value, step, scale));
    return Push(entry);
}

EntityHandle RetainedStore::CreateOtp(int length, Str value, bool masked,
                                      App* app, EntityId owner,
                                      void* application) {
    if (!app) return 0;
    RetainedEntry* entry = new RetainedEntry();
    entry->kind = RetainedKind::Otp;
    entry->owner = owner;
    entry->application = application;
    entry->app = app;
    entry->otp = EntityNewState<OtpState>(app);
    OtpState* state = entry->otp.Get(app);
    if (!state) {
        delete entry;
        return 0;
    }
    state->self = entry->otp;
    state->length = length;
    state->masked = masked;
    state->focus = FocusHandleNew(app);
    int n = value.len;
    if (n > (int)sizeof(state->value) - 1) n = (int)sizeof(state->value) - 1;
    if (n > 0) memcpy(state->value, value.s, (size_t)n);
    state->len = n;
    state->value[n] = 0;
    return Push(entry);
}

EntityHandle RetainedStore::CreateCalendar(Ctx* cx, EntityId owner,
                                           void* application) {
    if (!cx || !cx->app) return 0;
    RetainedEntry* entry = new RetainedEntry();
    entry->kind = RetainedKind::Calendar;
    entry->owner = owner;
    entry->application = application;
    entry->app = cx->app;
    entry->calendar = CalendarStateNew(cx);
    if (!entry->calendar.IsValid()) {
        delete entry;
        return 0;
    }
    return Push(entry);
}

EntityHandle RetainedStore::CreateDock(Str id, bool hasVersion, int version,
                                       Ctx* cx, EntityId owner,
                                       void* application) {
    if (!cx || !cx->app) return 0;
    RetainedEntry* entry = new RetainedEntry();
    entry->kind = RetainedKind::Dock;
    entry->owner = owner;
    entry->application = application;
    entry->app = cx->app;
    entry->dock = EntityNewState<DockState>(cx->app);
    DockState* state = entry->dock.Get(cx);
    if (!state) {
        delete entry;
        return 0;
    }
    state->hasVersion = hasVersion;
    state->version = version;
    entry->dockSkin = new ScriptDockSkin();
    entry->dockSkin->id = StrDup(id);
    EntityHandle handle = Push(entry);
    if (!handle) return 0;
    entry->dockSkin->hooks = &entry->dockHooks;
    entry->dockSkin->dock = handle;
    return handle;
}

EntityHandle RetainedStore::CreateFocus(App* app, EntityId owner,
                                        void* application) {
    RetainedEntry* entry = new RetainedEntry();
    entry->kind = RetainedKind::Focus;
    entry->owner = owner;
    entry->application = application;
    entry->app = app;
    entry->focus = FocusHandleNew(app);
    return Push(entry);
}

EntityHandle RetainedStore::CreateVirtualScroll(App* app, EntityId owner,
                                                void* application) {
    RetainedEntry* entry = new RetainedEntry();
    entry->kind = RetainedKind::VirtualScroll;
    entry->owner = owner;
    entry->application = application;
    entry->app = app;
    return Push(entry);
}

bool RetainedStore::AddCallback(EntityHandle handle, RetainedEvent event,
                                CallbackId callback, bool replace,
                                CallbackId* replaced) {
    RetainedEntry* entry = Find(handle);
    if (!entry || callback == 0) return false;
    if (replace) {
        for (int i = 0; i < entry->callbacks.len; i++) {
            if (entry->callbacks[i].event == event) {
                if (replaced) *replaced = entry->callbacks[i].callback;
                entry->callbacks[i].callback = callback;
                return true;
            }
        }
    }
    VecAppend(entry->callbacks, {event, callback});
    return true;
}

void RetainedStore::Destroy(RetainedEntry* entry, Vec<CallbackId>* callbacks) {
    if (!entry) return;
    if (callbacks) {
        for (int i = 0; i < len(entry->callbacks); i++) {
            VecAppend(*callbacks, entry->callbacks[i].callback);
        }
    }
    VecReset(entry->callbacks);
    if (entry->input) {
        if (entry->app && entry->input->blink.IsValid()) {
            EntityDrop(entry->app, entry->input->blink);
        }
        delete entry->input;
    }
    delete entry->slider;
    if (entry->subscription.IsValid() && entry->app) {
        EntityUnsubscribe(entry->app, entry->subscription);
    }
    if (entry->calendar.IsValid() && entry->app) {
        EntityDrop(entry->app, entry->calendar.id);
    }
    if (entry->dock.IsValid() && entry->app) {
        EntityDrop(entry->app, entry->dock.id);
    }
    if (entry->dockSkin) {
        StrFree(entry->dockSkin->id);
        delete entry->dockSkin;
    }
    if (entry->dockArena) ArenaDelete(entry->dockArena);
    if (entry->otp.IsValid() && entry->app) {
        OtpState* otp = entry->otp.Get(entry->app);
        if (otp && otp->blink.IsValid()) EntityDrop(entry->app, otp->blink);
        EntityDrop(entry->app, entry->otp.id);
    }
    delete entry;
}

bool RetainedStore::Release(EntityHandle handle, Vec<CallbackId>* callbacks) {
    RetainedEntry* entry = Find(handle);
    if (!entry) return false;
    for (int i = 0; i < entries.len; i++) {
        if (entries[i] != entry) continue;
        for (int j = i + 1; j < entries.len; j++) entries[j - 1] = entries[j];
        entries.len--;
        Destroy(entry, callbacks);
        return true;
    }
    return false;
}

void RetainedStore::ReleaseOwner(EntityId owner, Vec<CallbackId>* callbacks) {
    if (!owner.IsValid()) return;
    for (int i = entries.len - 1; i >= 0; i--) {
        if (entries[i]->owner != owner) continue;
        RetainedEntry* entry = entries[i];
        for (int j = i + 1; j < entries.len; j++) entries[j - 1] = entries[j];
        entries.len--;
        Destroy(entry, callbacks);
    }
}

void RetainedStore::ReleaseApplication(void* application,
                                       Vec<CallbackId>* callbacks) {
    if (!application) return;
    for (int i = entries.len - 1; i >= 0; i--) {
        if (entries[i]->application != application) continue;
        RetainedEntry* entry = entries[i];
        for (int j = i + 1; j < entries.len; j++) entries[j - 1] = entries[j];
        entries.len--;
        Destroy(entry, callbacks);
    }
}

void RetainedStore::Rollback(uint32_t checkpoint, Vec<CallbackId>* callbacks) {
    for (int i = entries.len - 1; i >= 0; i--) {
        if (entries[i]->id < checkpoint) continue;
        RetainedEntry* entry = entries[i];
        entries.len = i;
        Destroy(entry, callbacks);
    }
}

void RetainedStore::Clear(Vec<CallbackId>* callbacks) {
    for (int i = 0; i < entries.len; i++) Destroy(entries[i], callbacks);
    VecReset(entries);
}

} // namespace gpui::shell
