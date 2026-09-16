/* C++ port of crates/story — GPUI Kit gallery. */

#include "gpui.h"

using namespace gpui;

enum {
    StoryWelcome = 0,
    StoryAccordion,
    StoryAlert,
    StoryAlertDialog,
    StoryAttachment,
    StoryAvatar,
    StoryBadge,
    StoryBreadcrumb,
    StoryBubble,
    StoryButton,
    StoryCarousel,
    StoryCalendar,
    StoryChart,
    StoryCheckbox,
    StoryClipboard,
    StoryCollapsible,
    StoryColorPicker,
    StoryCombobox,
    StoryCommand,
    StoryDataTable,
    StoryDatePicker,
    StoryDescriptionList,
    StoryDialog,
    StoryDock,
    StoryDropdownButton,
    StoryEditor,
    StoryEmpty,
    StoryForm,
    StoryGroupBox,
    StoryHoverCard,
    StoryIcon,
    StoryImage,
    StoryInput,
    StoryKbd,
    StoryLabel,
    StoryList,
    StoryMarker,
    StoryMenu,
    StoryMessage,
    StoryMessageScroller,
    StoryNativeMenu,
    StoryNotification,
    StoryNumberInput,
    StoryOtpInput,
    StoryPagination,
    StoryPopover,
    StoryProgress,
    StoryRadio,
    StoryRating,
    StoryResizable,
    StoryScrollbar,
    StorySearchableList,
    StorySelect,
    StorySeparator,
    StorySettings,
    StorySheet,
    StoryShimmer,
    StorySidebar,
    StorySkeleton,
    StorySlider,
    StorySpinner,
    StoryStatusBar,
    StoryStepper,
    StorySwitch,
    StoryTable,
    StoryTabs,
    StoryTag,
    StoryTextarea,
    StoryThemeColors,
    StoryToggle,
    StoryTooltip,
    StoryTree,
    StoryVirtualList,
    StoryCount,
};

struct StoryApp {
    static El* Render(StoryApp* self, Ctx* cx);

    int story = StoryWelcome;
    float scrollY = 0;
    float sideScrollY = 0;
    bool collapsed = false;
    InputState search;
    // One entity per story, created on first view. crates/story keeps the
    // same shape: Gallery holds a view per story, not their state.
    EntityId pages[StoryCount] = {};
    // ToggleFpsMonitor: AppState::show_fps_monitor, the HUD over the window.
    bool fpsMonitor = false;
    // AppState::show_app_menu_bar: whether the title bar draws the menus
    // itself. macOS puts an application's menus in the bar at the top of the
    // screen, so a Mac drawing them here as well would show two copies of
    // them; off there by default, and still switchable from the Appearance
    // menu so the component stays demoable on a Mac.
    bool appMenuBar = GPUI_OS_MAC == 0;
    // What the menus hashed to when they were last installed, so the OS bar
    // is rebuilt when a row moves and not once a frame.
    uint32_t menuHash = 0;
    bool seeded = false;
};

// The window's notification list, for a page that wants to push one.
Entity<component::NotificationListState> StoryNotifications(Ctx* cx);
// window.push_notification(message, cx): the one-line info toast the stories
// answer a dialog button with. `message` has to outlive the frame.
void StoryPushNotification(Ctx* cx, Str message);

struct StoryInfo {
    const char* slug;
    const char* title;
    const char* description;
};

const StoryInfo* StoryMeta(int i);
int StoryFromSlug(const char* slug);

Str StoryDup(Ctx* cx, const char* s);
inline Str StoryFmtArg(Str s) {
    return s;
}
inline Str StoryFmtArg(const char* s) {
    return Str(s);
}
inline Str StoryFmtArg(char* s) {
    return Str(s);
}
template <typename T>
inline T StoryFmtArg(T v) {
    return v;
}
template <typename... TArgs>
inline Str StoryFmt(Ctx* cx, const char* format, TArgs... args) {
    return StrDup(cx->a, fmt(format, StoryFmtArg(args)...));
}

El* StoryTxt(Ctx* cx, Str s, float px, Rgba c);
El* StorySection(Ctx* cx, const char* title, const char* desc);
El* StorySectionAdd(El* section, El* child);
// The row inside the section's pane — what `section()` returns in Rust, and
// so what a page styles when it writes `.w_128()`, `.v_flex()` or `.gap_5()`
// on its section.
El* StorySectionBody(El* section);
// section().sub_title(..): sits opposite the title, in the header row.
El* StorySectionSubTitle(El* section, El* sub);
El* StoryComingSoon(Ctx* cx, int story);

// story_toolbar(size): the Size dropdown, plus an Options dropdown for the
// pages that have one. Each page owns its copy.
struct StoryToolbarState {
    UiSize size = UiSize::Medium;
    bool sizeMenuOpen = false;
    bool optsOpen = false;
};

// accordion_story builds the Options dropdown; it is the only page with one.
struct StoryAccordionOptions {
    bool multiple = false;
    bool icon = false;
    bool disabled = false;
    bool bordered = false;
};

// What a toolbar row does, bound into its listener the way a Rust closure
// would capture it.
enum StoryToolbarAction {
    ToolbarOpenSize = 1,
    ToolbarOpenOpts,
    // The press that lands outside a dropdown's own trigger: PopupMenu's
    // on_mouse_down_out, which is what makes a menu go away when you click
    // past it rather than through it.
    ToolbarCloseAll,
    ToolbarSizeXs,
    ToolbarSizeSm,
    ToolbarSizeMd,
    ToolbarSizeLg,
    ToolbarOptMultiple,
    ToolbarOptIcon,
    ToolbarOptDisabled,
    ToolbarOptBordered,
    ToolbarOptHorizontal,
    ToolbarOptColumns,
};

// One row of the toolbar's Options dropdown. Each page names its own rows,
// the way the Rust story builds the menu inline.
struct StoryToolbarOpt {
    const char* label = nullptr;
    bool checked = false;
    int act = 0;
    // menu() rather than menu_with_check(): no check column.
    bool plain = false;
    // separator() before this row.
    bool sep = false;
};

// For a page whose toolbar is not one size button plus one Options menu: the
// group draws the frame, each dropdown adds a button and its menu.
El* StoryToolbarGroup(Ctx* cx);
El* StoryToolbarDropdown(Ctx* cx, Str id, Str label, bool open, Listener onOpen,
                         const StoryToolbarOpt* rows, int nrows,
                         Listener onAct);
El* StoryToolbarDivider(Ctx* cx);

void StoryToolbarApply(StoryToolbarState* st, StoryAccordionOptions* opts,
                       int act);
El* StoryToolbarCore(Ctx* cx, StoryToolbarState* st,
                     const StoryToolbarOpt* rows, int nrows, Listener onAct,
                     bool withSize = true);

template <typename T>
void StoryToolbarAct(T* self, Ctx* cx, const ClickEvent*, intptr_t act) {
    StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    Notify(cx);
}

template <typename T>
void StoryToolbarActOpts(T* self, Ctx* cx, const ClickEvent*, intptr_t act) {
    StoryToolbarApply(&self->toolbar, &self->options, (int)act);
    Notify(cx);
}

// story_toolbar(self.size): the page owns the state, the rows own their
// handlers.
template <typename T>
El* StoryToolbar(Ctx* cx, T* self) {
    return StoryToolbarCore(cx, &self->toolbar, nullptr, 0,
                            Listen(cx, &StoryToolbarAct<T>));
}

// The page passes the rows it wants; the handler is its own. A page that
// calls story_toolbar_group() rather than story_toolbar(size) has no size
// button, so withSize is false there.
template <typename T>
El* StoryToolbarOptions(Ctx* cx, T* self, const StoryToolbarOpt* rows,
                        int nrows, Listener onAct, bool withSize = true) {
    return StoryToolbarCore(cx, &self->toolbar, rows, nrows, onAct, withSize);
}

template <typename T>
El* StoryToolbarWithOptions(Ctx* cx, T* self) {
    StoryToolbarOpt rows[4] = {
        {"Multiple", self->options.multiple, ToolbarOptMultiple},
        {"Icons", self->options.icon, ToolbarOptIcon},
        {"Disabled", self->options.disabled, ToolbarOptDisabled},
        {"Bordered", self->options.bordered, ToolbarOptBordered},
    };
    return StoryToolbarCore(cx, &self->toolbar, rows, 4,
                            Listen(cx, &StoryToolbarActOpts<T>));
}

typedef EntityId (*StoryPageNewFn)(App* app);
// A page can subscribe to keys, the way a view calls WindowOnKey. Used by the
// pages with an overlay, so Esc closes it.
typedef void (*StoryPageKeyFn)(void* self, Ctx* cx, const KeyEvent* ev);

void StoryRegister(int story, StoryPageNewFn create,
                   StoryPageKeyFn onKey = nullptr);
El* StoryRenderRegistered(StoryApp* app, Ctx* cx);
void StoryKeyRegistered(StoryApp* app, Ctx* cx, const KeyEvent* ev);

#define STORY_PAGE(ID, TYPE)                               \
    namespace {                                            \
    EntityId _st_new_##ID(App* app) {                      \
        return EntityNew<TYPE>(app).id;                    \
    }                                                      \
    struct _StReg_##ID {                                   \
        _StReg_##ID() { StoryRegister(ID, _st_new_##ID); } \
    } _st_reg_##ID;                                        \
    }

// Same, for a page that also wants keys: TYPE::OnKey(TYPE*, Ctx*, KeyEvent*).
#define STORY_PAGE_KEYS(ID, TYPE)                                           \
    namespace {                                                             \
    EntityId _stk_new_##ID(App* app) {                                      \
        return EntityNew<TYPE>(app).id;                                     \
    }                                                                       \
    void _stk_key_##ID(void* self, Ctx* cx, const KeyEvent* ev) {           \
        TYPE::OnKey((TYPE*)self, cx, ev);                                   \
    }                                                                       \
    struct _StkReg_##ID {                                                   \
        _StkReg_##ID() { StoryRegister(ID, _stk_new_##ID, _stk_key_##ID); } \
    } _stk_reg_##ID;                                                        \
    }
