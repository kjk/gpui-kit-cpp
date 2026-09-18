#ifndef GPUI_SHELL_RETAINED_H_
#define GPUI_SHELL_RETAINED_H_

#include "base/calendar.h"
#include "base/dock.h"
#include "base/input.h"
#include "base/otp_input.h"
#include "base/slider.h"
#include "base/virtual_list.h"
#include "shell/spec.h"

namespace gpui::shell {

using EntityHandle = uint64_t;

const int kMaxLiveEntities = 10000;

struct ScriptDockSkin;

enum class RetainedKind : uint8_t {
    Input,
    Textarea,
    Slider,
    Otp,
    // A calendar's month, view and selected date. Retained because the month
    // a script is looking at outlives the frame that drew it, and because the
    // day grid is derived from it: next_month moves the state and the next
    // month_days() answers a different grid.
    Calendar,
    Focus,
    // A dockable layout: its trees, its docks, its panels and the skin
    // drawing all of it. Retained for the reason nothing else here is quite:
    // the layout is what the *user* changed. A drag, a resize, a closed tab
    // and a collapsed dock all happen without a script render, and an area
    // rebuilt from a description would put every one of them back the way the
    // script last described it.
    Dock,
    VirtualScroll,
};

enum class RetainedEvent : uint8_t {
    InputChange,
    InputSubmit,
    InputFocus,
    InputBlur,
    SliderChange,
    SliderRelease,
    OtpChange,
    OtpComplete,
    OtpFocus,
    OtpBlur,
    CalendarChange,
    // The only dock event: every edit to the layout, including each step of a
    // drag, so a subscriber saves on a timer rather than on the event.
    DockLayoutChanged,
};

// Which script handler draws each piece of a dock's chrome, right now.
//
// One field per hook, and 0 means the script did not ask to draw that piece —
// so base's own no-chrome behavior stands. The indirection exists because the
// two ends move at different rates: a skin is installed once, when the area is
// created, while the handlers belong to whichever script render is currently
// published. Materialization writes this as it replays a `dock_area(...)`
// description, which is once per frame and before base asks the skin for
// anything.
// Which piece of chrome is being drawn. One entry per DockChrome hook, and
// the key that goes with it, so the same container asking again lands on the
// same cache entry.
enum class DockChromeSlot : uint8_t {
    TabBar,
    EmptyGroup,
    // The one slot with no container in its key, because an area never draws
    // two at once: a group raises its indicator only while its own bounds hold
    // the pointer, so the group under the pointer is the only one with an
    // indicator to draw.
    DropIndicator,
    Dock,
};

struct DockChromeHooks {
    CallbackId tabBar = 0;
    CallbackId emptyGroup = 0;
    CallbackId dropIndicator = 0;
    CallbackId dock = 0;
};

struct RetainedCallback {
    RetainedEvent event = RetainedEvent::InputChange;
    CallbackId callback = 0;
};

struct NumberInputConfig {
    bool hasStep = false;
    double step = 1;
    bool hasMin = false;
    double min = 0;
    bool hasMax = false;
    double max = 0;
};

struct RetainedEntry {
    uint32_t id = 0;
    RetainedKind kind = RetainedKind::Input;
    EntityId owner = {};
    void* application = nullptr;
    App* app = nullptr;
    InputState* input = nullptr;
    SliderState* slider = nullptr;
    Entity<OtpState> otp = {};
    FocusHandle focus = {};
    VirtualListScrollHandle scroll = {};
    Entity<CalendarState> calendar = {};
    Entity<DockState> dock = {};
    // The skin installed on the area, owned here: base takes a renderer at
    // construction and offers no way to replace one, so the skin outlives
    // every snapshot while the handlers it forwards to do not.
    ScriptDockSkin* dockSkin = nullptr;
    // A loaded layout's own strings. `DockLoad` keeps the panel names it was
    // handed, so the arena they were parsed into has to outlive the dock.
    Arena* dockArena = nullptr;
    // The one event subscription a calendar's `on("change", ...)` holds.
    // Replaced rather than appended, matching every other on(...) in this
    // API: registering twice means the second handler, not both of them.
    Subscription subscription = {};
    DockChromeHooks dockHooks = {};
    NumberInputConfig number = {};
    Vec<RetainedCallback> callbacks;
};

class RetainedStore {
  public:
    RetainedStore();
    RetainedStore(const RetainedStore&) = delete;
    RetainedStore& operator=(const RetainedStore&) = delete;
    ~RetainedStore();

    int Len() const { return entries.len; }
    uint32_t Checkpoint() const { return nextId; }

    EntityHandle CreateInput(bool textarea, Str placeholder, Str value,
                             int rows, App* app, EntityId owner,
                             void* application);
    EntityHandle CreateSlider(float min, float max, float step,
                              SliderScale scale, SliderValue value, App* app,
                              EntityId owner, void* application);
    EntityHandle CreateOtp(int length, Str value, bool masked, App* app,
                           EntityId owner, void* application);
    EntityHandle CreateCalendar(Ctx* cx, EntityId owner, void* application);
    // The skin is installed here rather than left to the caller because
    // `DockArea::New` takes a renderer and base offers no way to replace one
    // afterwards. What *is* replaceable is the set of script handlers the
    // skin forwards to, which is what `dockHooks` carries — so one skin built
    // once serves every snapshot the script publishes.
    EntityHandle CreateDock(Str id, bool hasVersion, int version, Ctx* cx,
                            EntityId owner, void* application);
    EntityHandle CreateFocus(App* app, EntityId owner, void* application);
    EntityHandle CreateVirtualScroll(App* app, EntityId owner,
                                     void* application);

    RetainedEntry* Find(EntityHandle handle) const;
    RetainedEntry* FindLocal(uint32_t id) const;
    bool AddCallback(EntityHandle handle, RetainedEvent event,
                     CallbackId callback, bool replace,
                     CallbackId* replaced = nullptr);
    bool Release(EntityHandle handle, Vec<CallbackId>* callbacks = nullptr);
    void ReleaseOwner(EntityId owner, Vec<CallbackId>* callbacks = nullptr);
    void ReleaseApplication(void* application,
                            Vec<CallbackId>* callbacks = nullptr);
    void Rollback(uint32_t checkpoint, Vec<CallbackId>* callbacks = nullptr);
    void Clear(Vec<CallbackId>* callbacks = nullptr);

  private:
    uint32_t storeId = 0;
    uint32_t nextId = 1;
    Vec<RetainedEntry*> entries;

    EntityHandle Push(RetainedEntry* entry);
    bool Belongs(EntityHandle handle, uint32_t* id) const;
    static void Destroy(RetainedEntry* entry, Vec<CallbackId>* callbacks);
};

} // namespace gpui::shell
#endif // GPUI_SHELL_RETAINED_H_
