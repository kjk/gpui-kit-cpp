#include "shell/materialize.h"

#include "base/accordion.h"
#include "gpui/image.h"
#include "base/avatar.h"
#include "base/button.h"
#include "base/actions.h"
#include "base/pagination.h"
#include "base/checkbox.h"
#include "base/collapsible.h"
#include "base/combobox.h"
#include "base/date_picker.h"
#include "base/hover_card.h"
#include "base/input.h"
#include "base/link.h"
#include "base/motion.h"
#include "base/number_input.h"
#include "base/otp_input.h"
#include "base/popover.h"
#include "base/popup.h"
#include "base/progress.h"
#include "base/radio.h"
#include "base/radio_group.h"
#include "base/resizable.h"
#include "base/scrollbar.h"
#include "base/select.h"
#include "base/slider.h"
#include "base/state_style.h"
#include "base/switch.h"
#include "base/table.h"
#include "base/tabs.h"
#include "base/text.h"
#include "base/toggle.h"
#include "base/toggle_group.h"
#include "fps/fps.h"
#include "shell/a11y.h"
#include "shell/component_registry.h"
#include "shell/action.h"
#include "shell/dock.h"
#include "shell/fetch.h"
#include "shell/policy.h"
#include "shell/style.h"
#include "shell/view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gpui {

struct MaterialBehavior {
    Str key;
    Str accessibilityLabel;
    Str href;
    bool disabled = false;
    bool selected = false;
    bool checked = false;
    bool pressed = false;
    bool indeterminate = false;
    bool open = false;
    bool hasOpen = false;
    bool defaultOpen = false;
    bool overlayClosable = true;
    bool controlsRight = false;
    bool start = false;
    Axis axis = Axis::Horizontal;
    bool scrollX = false;
    bool scrollY = false;
    bool scrollbar = false;
    bool hasScrollbarMode = false;
    ScrollbarMode scrollbarMode = ScrollbarMode::Scrolling;
    bool viewportFromLayout = false;
    Size scrollSize = {};
    bool hasScrollSize = false;
    bool panelVisible = true;
    bool hasPanelVisible = false;
    float panelSize = 0;
    bool hasPanelSize = false;
    float panelMin = kResizablePanelMinSize;
    float panelMax = 0;
    bool hasSizeRange = false;
    int positionInSet = 0;
    int sizeOfSet = 0;
    bool hasPosition = false;
    int itemToMeasure = 0;
    bool hasItemToMeasure = false;
    PopupAnchor anchor = PopupAnchor::TopLeft;
    bool hasAnchor = false;
    float fpsFrameBudget = 0;
    MouseButton mouseButton = MouseButton::Left;
    int openDelayMs = 600;
    int closeDelayMs = 300;
    shell::EntityHandle focus = 0;
    shell::EntityHandle contentFocus = 0;
    Str role;
    bool ariaSelected = false;
    bool hasAriaSelected = false;
    bool ariaActiveDescendant = false;
    Str tooltip;
    float value = 0;
    int rowCount = -1;
    int columnCount = -1;
    int tabIndex = 0;
    bool tabStop = true;
    shell::CallbackId onClick = 0;
    shell::CallbackId onChange = 0;
    shell::CallbackId onHover = 0;
    shell::CallbackId onMouseMove = 0;
    shell::CallbackId onOpenChange = 0;
    shell::CallbackId onConfirm = 0;
    shell::CallbackId onDismiss = 0;
    shell::CallbackId onStep = 0;
    shell::CallbackId onResize = 0;
    shell::CallbackId onItemClick = 0;
    shell::CallbackId onLinkClick = 0;
    // Reports a secondary press on a virtual list row, with the row's key and
    // the press itself. Registered on the list for the same reason
    // on_item_click is: the rows are rebuilt every frame.
    shell::CallbackId onItemSecondaryClick = 0;
    shell::CallbackId onToken = 0;
    shell::CallbackId onTokenClick = 0;
    shell::EntityHandle virtualScroll = 0;
    // Reports a key press or release that reached this element. GPUI routes a
    // key event down the focus path, so an element only hears one while it —
    // or something inside it — holds the keyboard, which makes
    // track_focus(handle) half of the registration rather than an unrelated
    // call.
    shell::CallbackId onKeyDown = 0;
    shell::CallbackId onKeyUp = 0;
    // Presses and releases, one entry per button listened for: GPUI takes the
    // button as an argument and an element may well want two of them — a left
    // press that selects and a right press that opens a menu are one
    // element's job.
    ShellMouseButtonBinding mouseDown = {};
    ShellMouseButtonBinding mouseUp = {};
    bool hasMouseDown = false;
    bool hasMouseUp = false;
    shell::CallbackId onMouseDownOut = 0;
    shell::CallbackId onScrollWheel = 0;
    // The key-binding context this element and its subtree sit in, and the
    // handlers for the actions it claims.
    Str keyContext;
    ShellActionBinding actions[8] = {};
    int actionCount = 0;
    // A heading's announced level, for an AccordionHeader, and whether a shut
    // AccordionPanel stays in the tree.
    int ariaLevel = 0;
    bool keepMounted = false;
    bool selectable = false;
    bool hasSelectable = false;
    bool textScrollable = false;
    bool hasTextScrollable = false;
    // Which script handler draws each piece of a dock_area's chrome. Six
    // handlers in one field rather than six fields, because they are written
    // together: they leave here for the slots the skin reads, and a skin reads
    // all of them or none.
    shell::DockChromeHooks dockChrome = {};
    // The dock commands a chrome element carries — what base is asked to do
    // when it is clicked or dragged. A list, because one element often carries
    // two: a tab both selects and drags.
    const shell::SpecOp* dockCommands[8] = {};
    int dockCommandCount = 0;
};

static const shell::Bridged* Arg(const shell::SpecOp& op, int at) {
    return at >= 0 && at < op.argCount ? &op.args[at] : nullptr;
}

static bool AsBool(const shell::SpecOp& op, int at, bool fallback = false) {
    const shell::Bridged* value = Arg(op, at);
    return value ? shell::BridgedIsTruthy(*value) : fallback;
}

static float AsNumber(const shell::SpecOp& op, int at, float fallback = 0) {
    const shell::Bridged* value = Arg(op, at);
    if (!value || value->kind != shell::BridgedKind::Number ||
        !isfinite(value->number)) {
        return fallback;
    }
    return (float)value->number;
}

static Str AsString(const shell::SpecOp& op, int at) {
    const shell::Bridged* value = Arg(op, at);
    return value && value->kind == shell::BridgedKind::String ? value->string
                                                              : Str{};
}

static shell::EntityHandle AsHandle(const shell::SpecOp& op, int at) {
    const shell::Bridged* value = Arg(op, at);
    if (!value || value->kind != shell::BridgedKind::Number ||
        value->number < 0 || !isfinite(value->number)) {
        return 0;
    }
    return (shell::EntityHandle)value->number;
}

static PopupAnchor AnchorOf(Str name, bool* found) {
    static const char names[] =
        "top_left\0top_center\0top_right\0bottom_left\0bottom_center\0"
        "bottom_right\0left_center\0right_center\0";
    int ix = SeqStrIndex(names, name);
    *found = ix >= 0;
    return ix < 0 ? PopupAnchor::TopLeft : (PopupAnchor)ix;
}

// A dock command carries no script value: it names a container in the area and
// what to ask it. That is why a tab can report its click at all — a chrome
// handler runs once per frame for as long as the dock is on screen, so a
// callback registered inside one would pile up the way a virtual list's row
// handlers would.
static DockPlacement DockPlacementOfName(Str name) {
    static const char names[] = "center\0left\0bottom\0right\0";
    int ix = SeqStrIndex(names, name);
    return ix < 0 ? DockPlacement::Center : (DockPlacement)ix;
}

static bool IsDockCommandName(Str name) {
    static const char names[] =
        "select_tab\0close_panel\0toggle_zoom\0drag_tab\0drop_tab\0"
        "toggle_dock\0resize_dock\0";
    for (const char* at = names; *at; at += strlen(at) + 1) {
        if (StrEq(name, at)) return true;
    }
    return false;
}

static void ResolveBehavior(const shell::SpecNode* node,
                            MaterialBehavior* out) {
    for (const shell::SpecOp& op : node->ops) {
        if (op.kind == shell::SpecOpKind::Callback) {
            if (StrEq(op.name, StrL("on_click")))
                out->onClick = op.callback;
            else if (StrEq(op.name, StrL("on_change")))
                out->onChange = op.callback;
            else if (StrEq(op.name, StrL("on_hover")))
                out->onHover = op.callback;
            else if (StrEq(op.name, StrL("on_mouse_move")))
                out->onMouseMove = op.callback;
            else if (StrEq(op.name, StrL("on_open_change")))
                out->onOpenChange = op.callback;
            else if (StrEq(op.name, StrL("on_confirm")))
                out->onConfirm = op.callback;
            else if (StrEq(op.name, StrL("on_dismiss")))
                out->onDismiss = op.callback;
            else if (StrEq(op.name, StrL("on_step")))
                out->onStep = op.callback;
            else if (StrEq(op.name, StrL("on_resize")))
                out->onResize = op.callback;
            else if (StrEq(op.name, StrL("on_item_click")))
                out->onItemClick = op.callback;
            else if (StrEq(op.name, StrL("on_link_click")))
                out->onLinkClick = op.callback;
            else if (StrEq(op.name, StrL("token")))
                out->onToken = op.callback;
            else if (StrEq(op.name, StrL("on_token_click")))
                out->onTokenClick = op.callback;
            else if (StrEq(op.name, StrL("on_item_secondary_click")))
                out->onItemSecondaryClick = op.callback;
            else if (StrEq(op.name, StrL("on_key_down")))
                out->onKeyDown = op.callback;
            else if (StrEq(op.name, StrL("on_key_up")))
                out->onKeyUp = op.callback;
            // The button is carried in the op name rather than beside it:
            // three fixed names cost nothing next to widening every op to
            // carry an argument only these six use.
            else if (StrEq(op.name, StrL("on_mouse_down_left"))) {
                out->mouseDown.left = op.callback;
                out->hasMouseDown = true;
            } else if (StrEq(op.name, StrL("on_mouse_down_right"))) {
                out->mouseDown.right = op.callback;
                out->hasMouseDown = true;
            } else if (StrEq(op.name, StrL("on_mouse_down_middle"))) {
                out->mouseDown.middle = op.callback;
                out->hasMouseDown = true;
            } else if (StrEq(op.name, StrL("on_mouse_up_left"))) {
                out->mouseUp.left = op.callback;
                out->hasMouseUp = true;
            } else if (StrEq(op.name, StrL("on_mouse_up_right"))) {
                out->mouseUp.right = op.callback;
                out->hasMouseUp = true;
            } else if (StrEq(op.name, StrL("on_mouse_up_middle"))) {
                out->mouseUp.middle = op.callback;
                out->hasMouseUp = true;
            } else if (StrEq(op.name, StrL("on_mouse_down_out"))) {
                out->onMouseDownOut = op.callback;
            } else if (StrEq(op.name, StrL("on_scroll_wheel"))) {
                out->onScrollWheel = op.callback;
            } else if (StrEq(op.name, StrL("tab_bar"))) {
                out->dockChrome.tabBar = op.callback;
            } else if (StrEq(op.name, StrL("empty_group"))) {
                out->dockChrome.emptyGroup = op.callback;
            } else if (StrEq(op.name, StrL("drop_indicator"))) {
                out->dockChrome.dropIndicator = op.callback;
            } else if (StrEq(op.name, StrL("dock"))) {
                out->dockChrome.dock = op.callback;
            }
            continue;
        }
        if (op.kind == shell::SpecOpKind::ActionCallback) {
            if (out->actionCount <
                (int)(sizeof(out->actions) / sizeof(out->actions[0]))) {
                out->actions[out->actionCount]
                    .action = shell::ShellActionOf(op.name);
                out->actions[out->actionCount].callback = op.callback;
                out->actionCount++;
            }
            continue;
        }
        if (op.kind != shell::SpecOpKind::Method) continue;
        if (IsDockCommandName(op.name)) {
            if (out->dockCommandCount < (int)(sizeof(out->dockCommands) /
                                              sizeof(out->dockCommands[0]))) {
                out->dockCommands[out->dockCommandCount++] = &op;
            }
            continue;
        }
        if (StrEq(op.name, StrL("id")))
            out->key = AsString(op, 0);
        else if (StrEq(op.name, StrL("accessibility_label")) ||
                 StrEq(op.name, StrL("aria_label")))
            out->accessibilityLabel = AsString(op, 0);
        else if (StrEq(op.name, StrL("href")))
            out->href = AsString(op, 0);
        else if (StrEq(op.name, StrL("disabled")))
            out->disabled = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("selected")))
            out->selected = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("checked")))
            out->checked = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("pressed")))
            out->pressed = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("indeterminate")))
            out->indeterminate = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("open"))) {
            out->open = AsBool(op, 0, true);
            out->hasOpen = true;
        } else if (StrEq(op.name, StrL("default_open")))
            out->defaultOpen = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("overlay_closable")))
            out->overlayClosable = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("controls_right")))
            out->controlsRight = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("start")))
            out->start = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("value")))
            out->value = AsNumber(op, 0);
        else if (StrEq(op.name, StrL("row_count")))
            out->rowCount = (int)AsNumber(op, 0, -1);
        else if (StrEq(op.name, StrL("column_count")))
            out->columnCount = (int)AsNumber(op, 0, -1);
        else if (StrEq(op.name, StrL("tab_index")))
            out->tabIndex = (int)AsNumber(op, 0);
        else if (StrEq(op.name, StrL("tab_stop")))
            out->tabStop = AsBool(op, 0, true);
        else if (StrEq(op.name, StrL("overflow_scroll")))
            out->scrollX = out->scrollY = true;
        else if (StrEq(op.name, StrL("overflow_x_scroll")))
            out->scrollX = true;
        else if (StrEq(op.name, StrL("overflow_y_scroll")))
            out->scrollY = true;
        else if (StrEq(op.name, StrL("overflow_scrollbar"))) {
            out->scrollX = out->scrollY = out->scrollbar = true;
        } else if (StrEq(op.name, StrL("overflow_x_scrollbar"))) {
            out->scrollX = out->scrollbar = true;
        } else if (StrEq(op.name, StrL("overflow_y_scrollbar"))) {
            out->scrollY = out->scrollbar = true;
        } else if (StrEq(op.name, StrL("mode"))) {
            Str mode = AsString(op, 0);
            out->hasScrollbarMode = true;
            if (StrEq(mode, StrL("hover")))
                out->scrollbarMode = ScrollbarMode::Hover;
            else if (StrEq(mode, StrL("always")))
                out->scrollbarMode = ScrollbarMode::Always;
            else
                out->scrollbarMode = ScrollbarMode::Scrolling;
        } else if (StrEq(op.name, StrL("viewport_from_layout"))) {
            out->viewportFromLayout = true;
        } else if (StrEq(op.name, StrL("scroll_size"))) {
            out->scrollSize = {AsNumber(op, 0), AsNumber(op, 1)};
            out->hasScrollSize = true;
        } else if (StrEq(op.name, StrL("panel_visible"))) {
            out->panelVisible = AsBool(op, 0, true);
            out->hasPanelVisible = true;
        } else if (StrEq(op.name, StrL("panel_size"))) {
            out->panelSize = AsNumber(op, 0);
            out->hasPanelSize = true;
        } else if (StrEq(op.name, StrL("size_range"))) {
            out->panelMin = AsNumber(op, 0, kResizablePanelMinSize);
            out->panelMax = AsNumber(op, 1, 0);
            out->hasSizeRange = true;
        } else if (StrEq(op.name, StrL("set_position"))) {
            out->positionInSet = (int)AsNumber(op, 0);
            out->sizeOfSet = (int)AsNumber(op, 1);
            out->hasPosition = true;
        } else if (StrEq(op.name, StrL("anchor"))) {
            out->anchor = AnchorOf(AsString(op, 0), &out->hasAnchor);
        } else if (StrEq(op.name, StrL("frame_budget"))) {
            out->fpsFrameBudget = AsNumber(op, 0) / 1000.f;
        } else if (StrEq(op.name, StrL("mouse_button"))) {
            Str button = AsString(op, 0);
            out->mouseButton =
                StrEq(button, StrL("right"))
                    ? MouseButton::Right
                    : (StrEq(button, StrL("middle")) ? MouseButton::Middle
                                                     : MouseButton::Left);
        } else if (StrEq(op.name, StrL("open_delay"))) {
            out->openDelayMs = (int)AsNumber(op, 0, 600);
        } else if (StrEq(op.name, StrL("close_delay"))) {
            out->closeDelayMs = (int)AsNumber(op, 0, 300);
        } else if (StrEq(op.name, StrL("track_focus"))) {
            out->focus = AsHandle(op, 0);
        } else if (StrEq(op.name, StrL("content_focus_handle"))) {
            out->contentFocus = AsHandle(op, 0);
        } else if (StrEq(op.name, StrL("role"))) {
            out->role = AsString(op, 0);
        } else if (StrEq(op.name, StrL("aria_selected"))) {
            out->ariaSelected = AsBool(op, 0, true);
            out->hasAriaSelected = true;
        } else if (StrEq(op.name, StrL("aria_active_descendant"))) {
            out->ariaActiveDescendant = true;
        } else if (StrEq(op.name, StrL("tooltip"))) {
            out->tooltip = AsString(op, 0);
        } else if (StrEq(op.name, StrL("track_scroll"))) {
            const shell::Bridged* handle = Arg(op, 0);
            if (handle && handle->kind == shell::BridgedKind::Number &&
                handle->number >= 0)
                out->virtualScroll = (shell::EntityHandle)handle->number;
        } else if (StrEq(op.name, StrL("axis"))) {
            out->axis = StrEq(AsString(op, 0), StrL("vertical"))
                            ? Axis::Vertical
                            : Axis::Horizontal;
        } else if (StrEq(op.name, StrL("with_item_to_measure_index"))) {
            out->itemToMeasure = (int)AsNumber(op, 0);
            out->hasItemToMeasure = true;
        } else if (StrEq(op.name, StrL("key_context"))) {
            out->keyContext = AsString(op, 0);
        } else if (StrEq(op.name, StrL("aria_level"))) {
            // Announced, not drawn, and base defaults it to 3.
            float level = AsNumber(op, 0, 3);
            out->ariaLevel = (int)(level < 1 ? 1 : level);
        } else if (StrEq(op.name, StrL("keep_mounted"))) {
            out->keepMounted = AsBool(op, 0, true);
        } else if (StrEq(op.name, StrL("selectable"))) {
            out->selectable = AsBool(op, 0, true);
            out->hasSelectable = true;
        } else if (StrEq(op.name, StrL("scrollable"))) {
            out->textScrollable = AsBool(op, 0, true);
            out->hasTextScrollable = true;
        }
    }
}

static bool ParseNumber(Str text, float* out) {
    if (!text || len(text) <= 0 || len(text) >= 64) return false;
    TempStr value = StrDupTemp(text);
    char* end = nullptr;
    double number = strtod(value.s, &end);
    if (!end || end == value.s || !isfinite(number)) return false;
    while (*end == ' ' || *end == '\t') end++;
    if (*end) return false;
    *out = (float)number;
    return true;
}

static Str TrimSpace(Str value) {
    while (StrStartsWithAny(value, " \t\r\n")) {
        value.s++;
        value.len--;
    }
    while (len(value) > 0 &&
           (value.s[len(value) - 1] == ' ' || value.s[len(value) - 1] == '\t' ||
            value.s[len(value) - 1] == '\r' ||
            value.s[len(value) - 1] == '\n')) {
        value.len--;
    }
    return value;
}

// The style vocabulary is shell/style.cpp, Rust's style.rs.
static bool ApplyNullary(El* element, Str name) {
    return shell::ApplyNullaryStyle(element, name);
}

static bool ApplyParam(El* e, const shell::SpecOp& op, ShellError* error) {
    return shell::ApplyParamStyle(e, op, error);
}

static uint32_t StyleFieldsFor(Str name) {
    return shell::StyleFieldsOf(name);
}

static bool ApplyStyleNode(Arena* arena, const shell::SpecNode* node,
                           El* target, uint32_t* fields, ShellError* error) {
    if (!node || !target) return false;
    for (const shell::SpecOp& op : node->ops) {
        bool recognized = true;
        if (op.kind == shell::SpecOpKind::NullaryStyle) {
            recognized = ApplyNullary(target, op.name);
        } else if (op.kind == shell::SpecOpKind::ParamStyle) {
            recognized = ApplyParam(target, op, error);
        } else {
            continue;
        }
        if (!recognized) {
            if (error && !error->IsSet())
                ShellErrorSet(error,
                              fmt("invalid state style call `%s`", op.name));
            continue;
        }
        if (fields) *fields |= StyleFieldsFor(op.name);
    }
    (void)arena;
    return true;
}

static const shell::SpecNode* StateNode(const shell::SpecArena* specs,
                                        const shell::SpecNode* owner,
                                        Str name) {
    if (!specs || !owner) return nullptr;
    for (const shell::SpecOp& op : owner->ops) {
        if (op.kind == shell::SpecOpKind::StateStyle && StrEq(op.name, name))
            return specs->Node(op.node);
    }
    return nullptr;
}

static void ApplyStateNode(Ctx* cx, const shell::SpecNode* state, El* target,
                           Str kind, ShellError* error) {
    if (!state || !target) return;
    El* resolved = Div(cx->a);
    uint32_t fields = 0;
    ApplyStyleNode(cx->a, state, resolved, &fields, error);
    StateStyle style;
    style.style = resolved->style;
    style.set = fields;
    if (StrEq(kind, StrL("hover")))
        target->Hover(style);
    else if (StrEq(kind, StrL("active")))
        target->Active(style);
    else if (StrEq(kind, StrL("focus")))
        target->Focus(style);
}

static void ApplyStateStyles(Ctx* cx, const shell::SpecArena* specs,
                             const shell::SpecNode* owner, El* target,
                             ShellError* error) {
    ApplyStateNode(cx, StateNode(specs, owner, StrL("hover")), target,
                   StrL("hover"), error);
    ApplyStateNode(cx, StateNode(specs, owner, StrL("active")), target,
                   StrL("active"), error);
    ApplyStateNode(cx, StateNode(specs, owner, StrL("focus")), target,
                   StrL("focus"), error);
}

struct MaterialPath {
    const shell::SpecNode* node = nullptr;
};

static bool PathCoordinate(const shell::Bridged* value, float origin,
                           float extent, float* out) {
    if (!value || !out) return false;
    if (value->kind == shell::BridgedKind::Number && isfinite(value->number)) {
        *out = origin + (float)value->number;
        return true;
    }
    if (value->kind != shell::BridgedKind::String) return false;
    Str text = TrimSpace(value->string);
    if (!StrEndsWith(text, "%")) return false;
    text.len--;
    float percentage = 0;
    if (!ParseNumber(text, &percentage)) return false;
    *out = origin + extent * percentage / 100.f;
    return true;
}

static bool PathPoint(const shell::SpecOp& op, int at, Bounds bounds,
                      Point* out) {
    return PathCoordinate(Arg(op, at), bounds.x, bounds.w, &out->x) &&
           PathCoordinate(Arg(op, at + 1), bounds.y, bounds.h, &out->y);
}

static Point EllipsePoint(float cx, float cy, float rx, float ry, float cosine,
                          float sine, float angle) {
    float x = rx * cosf(angle), y = ry * sinf(angle);
    return {cx + cosine * x - sine * y, cy + sine * x + cosine * y};
}

static Point EllipseDerivative(float rx, float ry, float cosine, float sine,
                               float angle) {
    float x = -rx * sinf(angle), y = ry * cosf(angle);
    return {cosine * x - sine * y, sine * x + cosine * y};
}

static float VectorAngle(float ux, float uy, float vx, float vy) {
    float dot = ux * vx + uy * vy;
    float cross = ux * vy - uy * vx;
    return atan2f(cross, dot);
}

static void PathEllipticalArc(Path* path, Point from, Point to, float rx,
                              float ry, float rotationDegrees, bool largeArc,
                              bool sweep) {
    rx = fabsf(rx);
    ry = fabsf(ry);
    if (rx <= 0 || ry <= 0 || (from.x == to.x && from.y == to.y)) {
        if (from.x != to.x || from.y != to.y) PathLineTo(path, to.x, to.y);
        return;
    }
    constexpr float kArcPi = 3.14159265358979323846f;
    float phi = rotationDegrees * kArcPi / 180.f;
    float cosine = cosf(phi), sine = sinf(phi);
    float dx = (from.x - to.x) * 0.5f;
    float dy = (from.y - to.y) * 0.5f;
    float x1 = cosine * dx + sine * dy;
    float y1 = -sine * dx + cosine * dy;
    float scale = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
    if (scale > 1.f) {
        float grow = sqrtf(scale);
        rx *= grow;
        ry *= grow;
    }
    float rx2 = rx * rx, ry2 = ry * ry;
    float denominator = rx2 * y1 * y1 + ry2 * x1 * x1;
    float numerator = rx2 * ry2 - denominator;
    float coefficient =
        denominator > 0 ? sqrtf(fmaxf(0.f, numerator / denominator)) : 0.f;
    if (largeArc == sweep) coefficient = -coefficient;
    float centerPrimeX = coefficient * rx * y1 / ry;
    float centerPrimeY = -coefficient * ry * x1 / rx;
    float centerX =
        cosine * centerPrimeX - sine * centerPrimeY + (from.x + to.x) * 0.5f;
    float centerY =
        sine * centerPrimeX + cosine * centerPrimeY + (from.y + to.y) * 0.5f;
    float ux = (x1 - centerPrimeX) / rx;
    float uy = (y1 - centerPrimeY) / ry;
    float vx = (-x1 - centerPrimeX) / rx;
    float vy = (-y1 - centerPrimeY) / ry;
    float start = atan2f(uy, ux);
    float delta = VectorAngle(ux, uy, vx, vy);
    if (!sweep && delta > 0) delta -= 2.f * kArcPi;
    if (sweep && delta < 0) delta += 2.f * kArcPi;
    int segments = (int)ceilf(fabsf(delta) / (kArcPi * 0.5f));
    if (segments < 1) segments = 1;
    float part = delta / (float)segments;
    float angle = start;
    for (int i = 0; i < segments; i++) {
        float next = angle + part;
        float alpha = 4.f / 3.f * tanf(part * 0.25f);
        Point p0 = EllipsePoint(centerX, centerY, rx, ry, cosine, sine, angle);
        Point p1 = EllipsePoint(centerX, centerY, rx, ry, cosine, sine, next);
        Point d0 = EllipseDerivative(rx, ry, cosine, sine, angle);
        Point d1 = EllipseDerivative(rx, ry, cosine, sine, next);
        PathCubicTo(path, p0.x + alpha * d0.x, p0.y + alpha * d0.y,
                    p1.x - alpha * d1.x, p1.y - alpha * d1.y, p1.x, p1.y);
        angle = next;
    }
}

static bool MaterialPathBackground(const shell::BackgroundSpec& spec,
                                   Background* out) {
    Hsla first = {}, second = {};
    shell::Bridged value = shell::Bridged::String(
        spec.kind == shell::BackgroundKind::LinearGradient ? spec.fromColor
                                                           : spec.color);
    if (!shell::BridgedAsColor(value, &first)) return false;
    Background result = HslaToRgba(first);
    if (spec.kind == shell::BackgroundKind::LinearGradient) {
        if (!shell::BridgedAsColor(shell::Bridged::String(spec.toColor),
                                   &second))
            return false;
        result = BackgroundLinear(
            spec.angle, ColorStopAt(HslaToRgba(first), spec.fromPosition),
            ColorStopAt(HslaToRgba(second), spec.toPosition));
    }
    // The renderer's Background currently has solid and linear-gradient
    // fills. PatternSlash and Checkerboard retain their geometry in the
    // snapshot for a future patterned path brush and use their declared
    // colour as the portable fallback today.
    *out = BackgroundOpacity(result, spec.opacity);
    return true;
}

static void PaintMaterialPath(PaintCtx* ctx, El* element, void* user) {
    MaterialPath* described = (MaterialPath*)user;
    if (!ctx || !element || !described || !described->node) return;
    const shell::SpecNode* node = described->node;
    Background background;
    if (!MaterialPathBackground(node->component.background, &background))
        return;
    Path* path = PathNew(ctx, true);
    if (!path) return;
    Bounds bounds = element->Bounds();
    Point current = {}, start = {};
    bool hasCurrent = false;
    for (const shell::SpecOp& op : node->ops) {
        if (StrEq(op.name, StrL("move_to"))) {
            Point point;
            if (!PathPoint(op, 0, bounds, &point)) continue;
            PathMoveTo(path, point.x, point.y);
            current = start = point;
            hasCurrent = true;
        } else if (StrEq(op.name, StrL("line_to"))) {
            Point point;
            if (!PathPoint(op, 0, bounds, &point)) continue;
            PathLineTo(path, point.x, point.y);
            current = point;
            if (!hasCurrent) start = point;
            hasCurrent = true;
        } else if (StrEq(op.name, StrL("curve_to")) && hasCurrent) {
            Point to, control;
            if (!PathPoint(op, 0, bounds, &to) ||
                !PathPoint(op, 2, bounds, &control))
                continue;
            Point a = {current.x + (control.x - current.x) * 2.f / 3.f,
                       current.y + (control.y - current.y) * 2.f / 3.f};
            Point b = {to.x + (control.x - to.x) * 2.f / 3.f,
                       to.y + (control.y - to.y) * 2.f / 3.f};
            PathCubicTo(path, a.x, a.y, b.x, b.y, to.x, to.y);
            current = to;
        } else if (StrEq(op.name, StrL("cubic_bezier_to")) && hasCurrent) {
            Point to, a, b;
            if (!PathPoint(op, 0, bounds, &to) ||
                !PathPoint(op, 2, bounds, &a) || !PathPoint(op, 4, bounds, &b))
                continue;
            PathCubicTo(path, a.x, a.y, b.x, b.y, to.x, to.y);
            current = to;
        } else if (StrEq(op.name, StrL("arc_to")) && hasCurrent) {
            float rx = 0, ry = 0;
            Point to;
            if (!PathCoordinate(Arg(op, 0), 0, bounds.w, &rx) ||
                !PathCoordinate(Arg(op, 1), 0, bounds.h, &ry) ||
                !PathPoint(op, 5, bounds, &to))
                continue;
            PathEllipticalArc(path, current, to, rx, ry, AsNumber(op, 2),
                              AsBool(op, 3), AsBool(op, 4));
            current = to;
        } else if (StrEq(op.name, StrL("close")) && hasCurrent) {
            PathClose(path);
            current = start;
        }
    }
    if (node->component.kind == shell::ComponentKind::PathFill) {
        if (background.gradient) {
            Point from, to;
            BackgroundLine(background, bounds, &from, &to);
            PathFillGradient(ctx, path, from.x, from.y, to.x, to.y,
                             background.from.color, background.to.color);
        } else {
            PathFill(ctx, path, background.color);
        }
    } else {
        PathStroke(ctx, path, node->component.strokeWidth, background.color);
    }
    PathFree(path);
}

static const shell::SpecOp* MotionFor(const shell::SpecNode* node,
                                      Str property) {
    const shell::SpecOp* found = nullptr;
    if (!node) return nullptr;
    for (const shell::SpecOp& op : node->ops) {
        if ((!StrEq(op.name, StrL("transition")) &&
             !StrEq(op.name, StrL("spring"))) ||
            !StrEq(AsString(op, 0), property))
            continue;
        found = &op;
    }
    return found;
}

static bool MotionTarget(const shell::SpecNode* node, Str property,
                         const Style& style, float* target) {
    bool declared = false;
    for (const shell::SpecOp& op : node->ops) {
        if ((StrEq(property, StrL("opacity")) &&
             StrEq(op.name, StrL("opacity"))) ||
            (StrEq(property, StrL("width")) &&
             (StrEq(op.name, StrL("w")) || StrEq(op.name, StrL("size")))) ||
            (StrEq(property, StrL("height")) &&
             (StrEq(op.name, StrL("h")) || StrEq(op.name, StrL("size")))) ||
            (StrEq(property, StrL("left")) && StrEq(op.name, StrL("left"))) ||
            (StrEq(property, StrL("top")) && StrEq(op.name, StrL("top"))))
            declared = true;
    }
    if (!declared) return false;
    if (StrEq(property, StrL("opacity")))
        *target = style.opacity;
    else if (StrEq(property, StrL("width"))) {
        if (style.width == kAuto || style.width == kFill ||
            style.widthFrac != 0)
            return false;
        *target = style.width;
    } else if (StrEq(property, StrL("height"))) {
        if (style.height == kAuto || style.height == kFill) return false;
        *target = style.height;
    } else if (StrEq(property, StrL("left"))) {
        if (style.absLeft == kAuto || style.absLeftRel != 0) return false;
        *target = style.absLeft;
    } else if (StrEq(property, StrL("top"))) {
        if (style.absTop == kAuto || style.absTopRel != 0) return false;
        *target = style.absTop;
    } else {
        return false;
    }
    return true;
}

static Str MotionIdentity(Ctx* cx, const shell::SpecNode* node,
                          shell::SpecId specId,
                          const MaterialBehavior& behavior) {
    if (behavior.key) return behavior.key;
    const shell::Component& component = node->component;
    switch (component.kind) {
        case shell::ComponentKind::Button:
        case shell::ComponentKind::TextView:
        case shell::ComponentKind::Link:
        case shell::ComponentKind::Checkbox:
        case shell::ComponentKind::Switch:
        case shell::ComponentKind::Tabs:
        case shell::ComponentKind::Tab:
        case shell::ComponentKind::Progress:
        case shell::ComponentKind::Radio:
        case shell::ComponentKind::Toggle:
        case shell::ComponentKind::RadioGroup:
        case shell::ComponentKind::ToggleGroup:
        case shell::ComponentKind::Table:
        case shell::ComponentKind::TableHeader:
        case shell::ComponentKind::TableBody:
        case shell::ComponentKind::TableRow:
        case shell::ComponentKind::TableHead:
        case shell::ComponentKind::TableCell:
        case shell::ComponentKind::TableCaption:
        case shell::ComponentKind::Scrollbar:
        case shell::ComponentKind::HResizable:
        case shell::ComponentKind::VResizable:
        case shell::ComponentKind::Popover:
        case shell::ComponentKind::HoverCard:
        case shell::ComponentKind::Popup:
        case shell::ComponentKind::Select:
        case shell::ComponentKind::Combobox:
        case shell::ComponentKind::DatePicker:
            if (component.text) return component.text;
            break;
        case shell::ComponentKind::VVirtualList:
        case shell::ComponentKind::HVirtualList:
            if (component.virtualList && component.virtualList->id)
                return component.virtualList->id;
            break;
        case shell::ComponentKind::List:
        case shell::ComponentKind::UniformList:
            if (component.list && component.list->id) return component.list->id;
            break;
        case shell::ComponentKind::Slider:
            return StrDup(cx->a,
                          fmt("gpui-shell-slider:%llu", component.handle));
        case shell::ComponentKind::SliderTrack:
            return StrDup(
                cx->a, fmt("gpui-shell-slider-track:%llu", component.handle));
        case shell::ComponentKind::SliderIndicator:
            return StrDup(cx->a, fmt("gpui-shell-slider-indicator:%llu",
                                     component.handle));
        case shell::ComponentKind::Input:
            return StrDup(cx->a,
                          fmt("gpui-shell-input:%llu", component.handle));
        case shell::ComponentKind::Textarea:
            return StrDup(cx->a,
                          fmt("gpui-shell-textarea:%llu", component.handle));
        case shell::ComponentKind::NumberInput:
            return StrDup(
                cx->a, fmt("gpui-shell-number-input:%llu", component.handle));
        case shell::ComponentKind::OtpInput:
            return StrDup(cx->a,
                          fmt("gpui-shell-otp-input:%llu", component.handle));
        default:
            break;
    }
    return StrDup(cx->a, fmt("gpui-shell-spec-%u", specId));
}

static void ApplyMotions(Ctx* cx, const shell::SpecNode* node,
                         shell::SpecId specId, const MaterialBehavior& behavior,
                         El* element) {
    static const Str properties[] = {StrL("opacity"), StrL("width"),
                                     StrL("height"), StrL("left"), StrL("top")};
    Str identity = MotionIdentity(cx, node, specId, behavior);
    for (Str property : properties) {
        const shell::SpecOp* op = MotionFor(node, property);
        float target = 0;
        if (!op || !MotionTarget(node, property, element->style, &target))
            continue;
        uint32_t key = MotionId(identity, property);
        float sampled = target;
        if (StrEq(op->name, StrL("spring"))) {
            Spring spring = SpringNew(AsNumber(*op, 1, 250));
            spring.damping = AsNumber(*op, 2, 1);
            spring.epsilon = AsNumber(*op, 3, 0.001f);
            sampled = SpringValue(cx, key, target, spring);
        } else {
            motion::Transition policy =
                motion::Transition::New(AsNumber(*op, 1));
            policy.delayMs = AsNumber(*op, 2);
            Str easing = AsString(*op, 3);
            if (StrEq(easing, StrL("linear")))
                policy.easing = Easing::Custom(EaseLinear);
            else if (StrEq(easing, StrL("ease-in")))
                policy.easing = Easing::Custom(EaseInCubic);
            else if (StrEq(easing, StrL("ease-in-out")))
                policy.easing = Easing::Custom(EaseInOutCubic);
            sampled = MotionValue(cx, key, target, policy);
        }
        if (StrEq(property, StrL("opacity")))
            element->style.opacity = sampled;
        else if (StrEq(property, StrL("width")))
            element->style.width = sampled;
        else if (StrEq(property, StrL("height")))
            element->style.height = sampled;
        else if (StrEq(property, StrL("left")))
            element->style.absLeft = sampled;
        else
            element->style.absTop = sampled;
    }
}

static El* MaterializeNode(Ctx* cx, ShellRuntime* runtime,
                           const shell::SpecArena* specs, shell::SpecId id,
                           ShellError* error);

static El* Slot(Ctx* cx, ShellRuntime* runtime, const shell::SpecArena* specs,
                const shell::SpecNode* node, const char* name,
                ShellError* error) {
    for (const shell::SpecOp& op : node->ops) {
        if (op.kind == shell::SpecOpKind::Slot && StrEq(op.name, name)) {
            return MaterializeNode(cx, runtime, specs, op.node, error);
        }
    }
    return nullptr;
}

static shell::SpecId SlotId(const shell::SpecNode* node, const char* name) {
    if (!node) return 0;
    for (const shell::SpecOp& op : node->ops) {
        if (op.kind == shell::SpecOpKind::Slot && StrEq(op.name, name))
            return op.node;
    }
    return 0;
}

static El* NumberStepButton(Ctx* cx, ShellRuntime* runtime,
                            const shell::SpecArena* specs,
                            shell::SpecId decoration, Str id, bool disabled,
                            Func0 step, ShellError* error) {
    El* button = Button::New(cx, id, disabled, {}, false);
    if (!disabled) button->OnClick(step);
    const shell::SpecNode* node =
        decoration && specs ? specs->Node(decoration) : nullptr;
    if (!node) return button;
    if (node->component.kind == shell::ComponentKind::HFlex)
        button->FlexRow();
    else if (node->component.kind == shell::ComponentKind::VFlex)
        button->FlexCol();
    uint32_t ignored = 0;
    ApplyStyleNode(cx->a, node, button, &ignored, error);
    ApplyStateStyles(cx, specs, node, button, error);
    MaterialBehavior behavior = {};
    ResolveBehavior(node, &behavior);
    if (behavior.accessibilityLabel)
        button->AriaLabel(behavior.accessibilityLabel);
    for (shell::SpecId child : node->children)
        button->Child(MaterializeNode(cx, runtime, specs, child, error));
    return button;
}

static Listener ClickListener(Ctx* cx, shell::CallbackId callback) {
    return callback ? Listen(cx, &ScriptView::OnClick, (intptr_t)callback)
                    : Listener{};
}

static Listener ChangeListener(Ctx* cx) {
    return Listen(cx, &ScriptView::OnChange);
}

struct MaterialOpenUrl {
    Str href;
};

static void MaterialOpenUrlRun(MaterialOpenUrl* call) {
    if (call && call->href) OpenUrl(call->href);
}

static Func0 OpenUrlCallback(Ctx* cx, Str href) {
    MaterialOpenUrl* call = ArenaNew<MaterialOpenUrl>(cx->a);
    call->href = href;
    return MkFunc0(&MaterialOpenUrlRun, call);
}

static FocusHandle RetainedFocus(ShellRuntime* runtime,
                                 shell::EntityHandle handle) {
    shell::RetainedEntry* entry =
        runtime && handle ? runtime->Retained(handle) : nullptr;
    return entry && entry->kind == shell::RetainedKind::Focus ? entry->focus
                                                              : FocusHandle{};
}

struct ShellTokenUser {
    ShellRuntime* runtime = nullptr;
    shell::CallbackId render = 0;
    shell::CallbackId click = 0;
    InputState* state = nullptr;
};

static El* ShellRenderToken(Ctx* cx, const InlineTokenContext* ctx,
                            void* user) {
    ShellTokenUser* values = (ShellTokenUser*)user;
    // No script renderer: Base's own chip (input.rs's default), which a
    // null answer asks for.
    if (!values || !values->runtime || !values->render) return nullptr;
    El* el = values->runtime->RenderInlineToken(values->render, ctx,
                                                InputValue(values->state), cx);
    if (el) return el;
    // input_tokens.rs InlineTokenCallbacks: a renderer that failed falls back
    // to the token's label, not to a component library's chip.
    return Div(cx->a)->Child(TextEl(cx->a, ctx->span.token.label));
}

static void ShellClickToken(const InlineTokenClickEvent* ev, Ctx* cx,
                            void* user) {
    ShellTokenUser* values = (ShellTokenUser*)user;
    if (values && values->runtime && values->click) {
        values->runtime->DispatchTokenClick(values->click, ev,
                                            InputValue(values->state), cx);
    }
}

static void InstallShellTokens(Ctx* cx, ShellRuntime* runtime,
                               InputState* state, const MaterialBehavior& b,
                               bool secret) {
    if (!state) return;
    if (!b.onToken && !b.onTokenClick) return;
    ShellTokenUser* user = ArenaNew<ShellTokenUser>(cx->a);
    user->runtime = runtime;
    user->render = b.onToken;
    user->click = b.onTokenClick;
    user->state = state;
    InputSetTokenPresentation(state, &ShellRenderToken, user,
                              b.onTokenClick ? &ShellClickToken : nullptr, user,
                              secret);
}

struct MaterialVirtualUser {
    ShellRuntime* runtime = nullptr;
    shell::CallbackId render = 0;
    shell::CallbackId getKey = 0;
    shell::CallbackId onItemClick = 0;
    shell::CallbackId onItemSecondaryClick = 0;
};

static void MaterialVirtualRange(void* user, Ctx* cx, int first, int end,
                                 El** out) {
    MaterialVirtualUser* values = (MaterialVirtualUser*)user;
    if (values && values->runtime) {
        values->runtime->RenderVirtualItems(
            values->render, values->getKey, values->onItemClick,
            values->onItemSecondaryClick, first, end, cx, out);
    }
}

// ─── `list` and `uniform_list`: GPUI's own lazy lists, driven from script ──
//
// A VirtualList is base's: the script states every item's extent and base
// places the items by the table. These two are GPUI's, and the difference is
// who measures. `uniform_list` measures one item and places every row by it;
// `list` measures each item it draws and keeps the sizes, so rows of unequal,
// unstated height still scroll as one collection. Both draw only what is on
// screen, and both reach the script the way the virtual list does — through
// RenderVirtualItems, from inside the build — so the confinement recorded on
// that path holds for them unchanged.
//
// Neither takes a VirtualListScrollHandle: the position is the list's own
// state, kept under the id the list was built with. That id is also the name
// a `Scrollbar` pairs with.

// How far past the viewport a `list` draws and measures, in pixels.
//
// GPUI's list can only scroll into what it has measured, and it measures by
// drawing: with nothing drawn past the viewport, a list whose last drawn row
// ends exactly at the bottom edge has nowhere to scroll to and never asks for
// more. A band below the fold keeps a wheel notch or a bar drag inside
// measured ground, and each frame it moves measures the next band. Kept to a
// few rows rather than the screenful GPUI's own callers use: every item in the
// band is a script render per frame, and the whole point of the list is to
// leave an item a screen away undrawn.
static const float kListOverdraw = 160;

// What a lazy list keeps between frames under its name — Rust's
// `UniformListScrollHandle` for one list and `ListState` for the other, which
// upstream files under `use_keyed_state` so they outlive the description.
// Window keyed state under the list's id does the same here.
struct LazyListState {
    // ListState's logical top: the item at the viewport's start and how far
    // into it. Pixel offset is derived from measured heights, so unmeasured
    // items are not guessed at the mean of the rest.
    int topItem = 0;
    float topInto = 0;
    float offset = 0;
    int itemCount = 0;
    // `list` only: the height each item measured at, 0 until it is drawn.
    Vec<float> measured;
    // `uniform_list` only: the height the measured item came out at.
    float rowH = 0;

    ~LazyListState() { VecReset(measured); }
};

static uint32_t LazyListKey(Str id) {
    return KeyedKey((uint32_t)HashClickId(id),
                    (uint32_t)HashClickId(StrL("gpui-shell-lazy-list")));
}

// The wheel, or a bar the list shares its name with, moved the list. The
// window has already asked for a repaint; the description is untouched, so
// the next frame re-materializes the same snapshot at the new offset.
static void OnLazyListScroll(ScriptView*, Ctx* cx, const ScrollEvent* event,
                             intptr_t key) {
    LazyListState* state = KeyedState<LazyListState>(cx, (uint32_t)key);
    if (!state || !event) return;
    state->offset = event->offsetY;
    VirtualListLogicalFromPixel(
        state->measured.len ? state->measured.els : nullptr, state->itemCount,
        state->rowH, event->offsetY, &state->topItem, &state->topInto);
}

static El* LazyListElement(Ctx* cx, ShellRuntime* runtime,
                           const shell::Component& component,
                           const MaterialBehavior& behavior) {
    const shell::ListSpec* spec = component.list;
    bool uniform = component.kind == shell::ComponentKind::UniformList;
    Str name = uniform ? StrL("uniform_list") : StrL("list");
    if (!spec || !runtime) return Div(cx->a);
    if (behavior.virtualScroll) {
        logf(
            "shell: track_scroll is ignored on a %s: its scroll position is "
            "GPUI's own, filed under the id it was built with, which is where "
            "a Scrollbar of that name finds it\n",
            name);
    }
    if (!uniform && behavior.hasItemToMeasure) {
        logf(
            "shell: with_item_to_measure_index is ignored on a list: it "
            "measures every item it draws, so there is no one item the rest "
            "are sized from. It is uniform_list that takes one\n");
    }

    uint32_t key = LazyListKey(spec->id);
    LazyListState* state = KeyedState<LazyListState>(cx, key);
    if (!state) return Div(cx->a);
    int count = spec->itemCount;
    int scrollId = HashClickId(spec->id);
    PaintCtx* paint = cx->win ? &cx->win->paint : nullptr;
    Listener onScroll = Listen(cx, &OnLazyListScroll, (intptr_t)key);

    MaterialVirtualUser* user = ArenaNew<MaterialVirtualUser>(cx->a);
    user->runtime = runtime;
    user->render = spec->renderItems;
    user->getKey = spec->getKey;
    user->onItemClick = behavior.onItemClick;
    user->onItemSecondaryClick = behavior.onItemSecondaryClick;

    if (uniform) {
        // One row is measured and every row is placed by it: the first, or
        // the one `with_item_to_measure_index` names. Rust measures at
        // MinContent on both axes and so does `MeasureEl`; the probe is
        // thrown away, since what is wanted is the number. A row that
        // measures nothing keeps whatever the state had.
        if (count > 0) {
            int measureIx =
                behavior.hasItemToMeasure ? behavior.itemToMeasure : 0;
            if (measureIx < 0) measureIx = 0;
            if (measureIx >= count) measureIx = count - 1;
            El* probe = nullptr;
            runtime->RenderVirtualItems(spec->renderItems, spec->getKey, 0, 0,
                                        measureIx, measureIx + 1, cx, &probe);
            if (probe) {
                float got = MeasureEl(paint, probe).h;
                if (got > 0) state->rowH = got;
            }
        }
        VirtualListOpts opts;
        opts.count = count;
        opts.rowH = state->rowH;
        opts.scrollY = state->offset;
        opts.range = MaterialVirtualRange;
        opts.user = user;
        opts.scrollId = scrollId;
        opts.onScroll = onScroll;
        return VirtualList::New(cx, spec->id, opts)->SizeFull();
    }

    if (state->itemCount != count) {
        VecClear(state->measured);
        for (int i = 0; i < count; i++) VecAppend(state->measured, 0.f);
        state->itemCount = count;
        if (state->topItem >= count) state->topItem = 0;
    }
    VirtualListOpts opts;
    opts.count = count;
    opts.sizes = state->measured.len ? state->measured.els : nullptr;
    opts.scrollY = state->offset;
    opts.logicalScroll = true;
    opts.topItem = state->topItem;
    opts.topInto = state->topInto;
    opts.overdraw = kListOverdraw;
    opts.range = MaterialVirtualRange;
    opts.user = user;
    opts.scrollId = scrollId;
    opts.onScroll = onScroll;
    return VirtualList::New(cx, spec->id, opts)->SizeFull();
}

// `dock_area` — the one element whose contents the description does not
// contain.
//
// Every other component here is the whole of what it draws. A dock area is
// not, and cannot be: the layout is what the *user* changed. The layout
// therefore lives in a retained entity and this node mounts it. Two things do
// cross from the description into that entity, and both happen here: the
// chrome handlers in force for the frame, and the runtime they are asked
// through. Written every frame rather than only when they change, because a
// callback id is only meaningful while the snapshot that registered it lives,
// and materialization is the one place that always runs against the live one.
static El* DockAreaElement(Ctx* cx, ShellRuntime* runtime,
                           const shell::SpecNode* node,
                           const MaterialBehavior& behavior) {
    shell::RetainedEntry* entry =
        runtime ? runtime->Retained(node->component.handle) : nullptr;
    if (!entry || entry->kind != shell::RetainedKind::Dock ||
        !entry->dockSkin) {
        logf("shell: this dock area is no longer live\n");
        return Div(cx->a);
    }
    if (node->children.len > 0) {
        logf(
            "shell: children are dropped on a dock_area: what it draws is "
            "the panels in its layout, which are added with add_panel(...) "
            "rather than described\n");
    }
    entry->dockHooks = behavior.dockChrome;
    entry->dockSkin->runtime = runtime;
    entry->dockSkin->hooks = &entry->dockHooks;
    entry->dockSkin->dock = node->component.handle;
    // `SizeFull` before the script's own refinement, so it is a default rather
    // than an override: an area with no size draws nothing at all, which is a
    // failure with no visible cause on screen.
    return Div(cx->a)->SizeFull()->Child(DockArea::New(
        cx, entry->dockSkin->id, entry->dock, entry->dockSkin->Renderer()));
}

// The dock commands a chrome element carries, wired onto it.
//
// Only reachable while a chrome handler is running, which is where the live
// group or dock is. Upstream files each context away as it goes past and
// resolves the command later, because its callbacks live only for the length
// of one chrome call; base's own `DockBind*` calls put the behavior on the
// element instead, so the resolution happens here and nothing has to be kept.
static El* WireDockCommands(Ctx* cx, El* element,
                            const MaterialBehavior& behavior) {
    (void)cx;
    if (behavior.dockCommandCount == 0 || !element) return element;
    const shell::ShellDockChromeFrame* frame = shell::ShellDockCurrentChrome();
    if (!frame) {
        logf(
            "shell: a dock command was used outside a dock's chrome "
            "handler; the command is dropped\n");
        return element;
    }
    for (int i = 0; i < behavior.dockCommandCount; i++) {
        const shell::SpecOp& op = *behavior.dockCommands[i];
        // Read as a double rather than through AsNumber: a retained handle
        // packs a store id above the low 32 bits, which a float cannot hold.
        shell::EntityHandle dock = AsHandle(op, 0);
        if (dock != frame->dock) {
            logf(
                "shell: `%s` names a dock area other than the one being "
                "drawn; the command is dropped\n",
                op.name);
            continue;
        }
        const DockTabGroup* group = frame->group;
        const DockCtx* region = frame->dockCtx;
        if (StrEq(op.name, StrL("select_tab")) && group) {
            element = DockBindTab(group, (int)AsNumber(op, 2, -1), element);
        } else if (StrEq(op.name, StrL("close_panel")) && group) {
            // The script holds the panel id the area reported; base binds by
            // position in the row, and the two are one lookup apart.
            // A panel id is a whole 64-bit number; float would round it.
            const shell::Bridged* value = Arg(op, 2);
            double id = value && value->kind == shell::BridgedKind::Number
                            ? value->number
                            : -1;
            int at = -1;
            for (int ix = 0; ix < DockGroupCount(group); ix++) {
                const DockPanelDef* def = DockGroupPanel(group, ix);
                if (def && (double)def->id.AsU64() == id) {
                    at = ix;
                    break;
                }
            }
            if (at >= 0) element = DockBindClose(group, at, element);
        } else if (StrEq(op.name, StrL("toggle_zoom")) && group) {
            int active = DockGroupActiveIx(group);
            if (active >= 0) element = DockBindZoom(group, active, element);
        } else if (StrEq(op.name, StrL("drag_tab")) && group) {
            element =
                DockBindTitleDrag(group, (int)AsNumber(op, 2, -1), element);
        } else if (StrEq(op.name, StrL("drop_tab")) && group) {
            // A tab bar that names no slot means "append", which is what a
            // drop past the last tab is.
            float at = AsNumber(op, 2, -1);
            element = at < 0 ? DockBindTabRest(group, element)
                             : DockBindTab(group, (int)at, element);
        } else if (StrEq(op.name, StrL("toggle_dock")) && (group || region)) {
            DockPlacement placement = DockPlacementOfName(AsString(op, 1));
            if (group) {
                element = DockBindToggle(group, placement, element);
            } else {
                // The same binding as base's, from a dock's own chrome rather
                // than from a group's tab bar: `DockBindToggle` wants a group
                // and a dock has none of its own, but the listener it hangs
                // needs only the area and the placement, which the DockCtx
                // carries.
                element
                    ->OnClick(ListenTo(region->state, &DockState::OnToggleSide,
                                       (intptr_t)placement));
                element->TabStop(false);
            }
        } else if (StrEq(op.name, StrL("resize_dock")) && region) {
            element = DockBindResizeStrip(region, element);
        } else {
            logf(
                "shell: `%s` did not name a container in the dock area "
                "being drawn; the command is dropped\n",
                op.name);
        }
    }
    return element;
}

// The item's `open` and `disabled`, while its two halves are being built.
//
// Rust's `AccordionItem::render` passes them down to the header and the panel,
// so a script sets them once on the item rather than three times in agreement
// with itself. The C++ parts are elements rather than concrete types, so the
// push-down happens while the slot is materialized instead of afterwards.
static struct {
    bool active = false;
    bool open = false;
    bool disabled = false;
} gAccordionItem;

struct AccordionItemScope {
    bool active = false;
    bool open = false;
    bool disabled = false;

    AccordionItemScope(bool isOpen, bool isDisabled) {
        active = gAccordionItem.active;
        open = gAccordionItem.open;
        disabled = gAccordionItem.disabled;
        gAccordionItem.active = true;
        gAccordionItem.open = isOpen;
        gAccordionItem.disabled = isDisabled;
    }
    ~AccordionItemScope() {
        gAccordionItem.active = active;
        gAccordionItem.open = open;
        gAccordionItem.disabled = disabled;
    }
};

// ─── materialize/text_view.rs — document images under the script's policy ──
//
// A script's TextView fetches its images with the grant of the script that
// described it, not with the host's unrestricted image loader. Every document
// image resolves to a source this file answers: a URL the policy does not
// grant a GET to — relative, scheme-less, data:, file:, a custom scheme, one
// carrying credentials — fails without a request, and a granted one is
// fetched here, re-authorizing every redirect hop, and handed to the image
// cache as the fetched bytes, so the cache never sees the URL and never
// starts a second, ungated request of its own. A load that is pending or has
// failed never falls back to the document's URL.
//
// Rust keys the owner by the view's element id and the policy's identity, so
// two documents under different grants never share a load; the keyed state
// here is the same key, and like GPUI's element state it lives as long as
// frames keep building the view: the first frame that does not drops it,
// and with it the fetched bytes and what the app decoded from them
// (`cx.drop_image`, here ImageDropEncoded). A load still in flight then
// lands on nothing. One 30-second deadline covers the whole load, every
// redirect hop included, as Rust's race against IMAGE_TIMEOUT does: the walk
// fails a hop that lands after it, and a load still pending when it passes
// is drawn as failed.

namespace shell {

bool TextViewImageUrl(const Capabilities& capabilities, Str url) {
    // is_openable_url, then the GET grant. FetchAuthorize already refuses
    // anything but an absolute http(s) URL, a missing host and userinfo, so
    // document-supplied credentials never become HTTP credentials.
    return IsOpenableUrl(url) &&
           FetchAuthorize(url, StrL("GET"), capabilities, nullptr);
}

struct TextViewImageFetch {
    Func1<TextViewImageResponse> done;
};

static void TextViewImageFetched(TextViewImageFetch* fetch,
                                 FetchAsyncResult landed) {
    TextViewImageResponse response;
    FetchResult* result = landed.result;
    // A redirect with no Location, one too many, one the grant does not
    // cover and an HTTPS downgrade all end the walk as a failure; so does
    // anything but a success status, and a body over the limit.
    if (landed.ok && result && result->status >= 200 && result->status < 300 &&
        len(result->body) <= kFetchMaxBody) {
        response.ok = true;
        response.bytes = result->body;
    }
    fetch->done.Call(response);
    delete fetch;
}

bool TextViewImageRequest(const Capabilities& capabilities, Str url,
                          Func1<TextViewImageResponse> done, double deadline) {
    if (!TextViewImageUrl(capabilities, url)) return false;
    FetchRequest request;
    request.deadline =
        deadline != 0 ? deadline : TimeNow() + kTextViewImageTimeoutSecs;
    // url.set_fragment(None): the fragment is the document's, not the
    // server's.
    int end = 0;
    while (end < len(url) && url.s[end] != '#') end++;
    request.url = StrDup(Str(url.s, end));
    request.method = StrDup(StrL("GET"));
    TextViewImageFetch* fetch = new TextViewImageFetch();
    fetch->done = done;
    // FetchSendAsync asks the transport not to follow redirects and walks
    // them itself, authorizing each Location against the same grant, at most
    // kFetchMaxRedirects of them, and never from HTTPS onto HTTP.
    bool started = request.url.s && request.method.s &&
                   FetchSendAsync(request, capabilities,
                                  MkFunc1(TextViewImageFetched, fetch));
    request.Free();
    if (!started) delete fetch;
    return started;
}

static bool SvgAttrIsHref(Str name) {
    return StrEqI(name, StrL("href")) || StrEqI(name, StrL("xlink:href"));
}

bool TextViewSvgReferencesFile(Str bytes) {
    // refuse_svg_file_references: GPUI draws an SVG `<image>` whose href is
    // not a data URL from the file it names. This tree's SVG renderer draws
    // no `<image>` at all, but a document image that asks for one is refused
    // the same, so a script sees the same answer on both.
    const char* s = bytes.s;
    int n = len(bytes);
    for (int i = 0; s && i + 6 < n; i++) {
        if (s[i] != '<' ||
            !StrStartsWithI(Str(s + i + 1, n - i - 1), StrL("image"))) {
            continue;
        }
        char after = s[i + 6];
        if (after != ' ' && after != '\t' && after != '\r' && after != '\n' &&
            after != '/' && after != '>') {
            continue;
        }
        int j = i + 6;
        while (j < n && s[j] != '>') {
            while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' ||
                             s[j] == '\n' || s[j] == '/')) {
                j++;
            }
            int nameStart = j;
            while (j < n && s[j] != '=' && s[j] != '>' && s[j] != ' ' &&
                   s[j] != '\t' && s[j] != '\r' && s[j] != '\n' &&
                   s[j] != '/') {
                j++;
            }
            Str name(s + nameStart, j - nameStart);
            if (j >= n || s[j] != '=') {
                if (j == nameStart && j < n && s[j] != '>') j++;
                continue;
            }
            j++;
            char quote = j < n ? s[j] : 0;
            if (quote != '"' && quote != '\'') continue;
            int valueStart = ++j;
            while (j < n && s[j] != quote) j++;
            Str value(s + valueStart, j - valueStart);
            if (j < n) j++;
            if (SvgAttrIsHref(name) && !StrStartsWithI(value, StrL("data:"))) {
                return true;
            }
        }
    }
    return false;
}

} // namespace shell

// The owner's handle on one URL's load. The fetch callback holds a reference
// too, so a view released mid-load only orphans it.
struct DocumentImages;

struct DocumentImage {
    Str uri = {};
    Str bytes = {};
    ImageLoadState state = ImageLoadState::Loading;
    DocumentImages* owner = nullptr;
    // IMAGE_TIMEOUT from when the load started, over every hop.
    double deadline = 0;
    int refs = 1;
};

// The race against the timer: a load still pending past its deadline has
// failed, whatever lands later.
static void DocumentImageExpire(DocumentImage* image) {
    if (image && image->state == ImageLoadState::Loading &&
        TimeNow() >= image->deadline) {
        image->state = ImageLoadState::Failed;
    }
}

static void DocumentImageRelease(DocumentImage* image) {
    if (!image || --image->refs > 0) return;
    StrFree(image->uri);
    StrFree(image->bytes);
    delete image;
}

struct DocumentImages {
    // Retaining the immutable policy also keeps its identity, which is half
    // the key, from being reused.
    Policy* policy = nullptr;
    App* app = nullptr;
    // Where to arm the redraw that shows a load which missed its deadline,
    // and the view it redraws.
    Window* win = nullptr;
    Listener deadline = {};
    Vec<DocumentImage*> used;

    // on_release: what the app decoded from each image goes with the view.
    ~DocumentImages() {
        for (int i = 0; i < used.len; i++) {
            DocumentImage* image = used[i];
            if (image->bytes.s) {
                ImageDropEncoded(app, (const uint8_t*)image->bytes.s,
                                 len(image->bytes));
            }
            image->owner = nullptr;
            DocumentImageRelease(image);
        }
        VecReset(used);
        if (policy) PolicyRelease(policy);
    }
};

static void DocumentImageLanded(DocumentImage* image,
                                shell::TextViewImageResponse response) {
    DocumentImages* owner = image->owner;
    DocumentImageExpire(image);
    if (owner && image->state == ImageLoadState::Loading) {
        bool ok =
            response.ok && !shell::TextViewSvgReferencesFile(response.bytes);
        if (ok) image->bytes = StrDup(response.bytes);
        image->state = ok && image->bytes.s ? ImageLoadState::Ready
                                            : ImageLoadState::Failed;
    }
    if (owner && owner->app) NotifyApp(owner->app);
    DocumentImageRelease(image);
}

// ImageSource::Custom's answer while there is nothing to draw: loading, or
// failed for good.
static ImageLoadState DocumentImageState(PaintApp*, void* user,
                                         RenderImage** image) {
    *image = nullptr;
    DocumentImage* entry = (DocumentImage*)user;
    DocumentImageExpire(entry);
    return entry ? entry->state : ImageLoadState::Failed;
}

static ImageLoadState DocumentImageDenied(PaintApp*, void*,
                                          RenderImage** image) {
    *image = nullptr;
    return ImageLoadState::Failed;
}

static DocumentImage* DocumentImageFor(DocumentImages* images, Str uri) {
    for (int i = 0; i < images->used.len; i++) {
        if (StrEq(images->used[i]->uri, uri)) return images->used[i];
    }
    DocumentImage* image = new DocumentImage();
    image->uri = StrDup(uri);
    image->owner = images;
    if (!image->uri.s || !VecAppend(images->used, image)) {
        DocumentImageRelease(image);
        return nullptr;
    }
    image->refs++;
    image->deadline = TimeNow() + shell::kTextViewImageTimeoutSecs;
    if (!shell::TextViewImageRequest(PolicyCapabilities(images->policy), uri,
                                     MkFunc1(DocumentImageLanded, image),
                                     image->deadline)) {
        image->refs--;
        image->state = ImageLoadState::Failed;
    } else if (images->win && images->deadline.IsValid()) {
        // A hop that never answers lands no sooner than the transport gives
        // up on it, so the deadline asks for its own redraw.
        WindowSetTimeout(images->win,
                         (int)(shell::kTextViewImageTimeoutSecs * 1000) + 1,
                         images->deadline);
    }
    return image;
}

static ImageSource DocumentImageSource(Str uri, void* data) {
    DocumentImages* images = (DocumentImages*)data;
    DocumentImage* image = images ? DocumentImageFor(images, uri) : nullptr;
    if (!image) return ImageSource::FromCustom(DocumentImageDenied, nullptr);
    DocumentImageExpire(image);
    if (image->state == ImageLoadState::Ready) {
        // The fetched bytes, never the URL: decoding them cannot start a
        // request of its own.
        return ImageSource::FromImage((const uint8_t*)image->bytes.s,
                                      len(image->bytes));
    }
    return ImageSource::FromCustom(DocumentImageState, image);
}

// image_sources: the owner is keyed by the view's id (already on the id
// stack) and the policy's identity, and lives while frames build the view.
// Without a window — a check that only constructs elements — there is no
// owner and nothing loads.
static DocumentImages* DocumentImagesFor(Ctx* cx, Policy* policy) {
    if (!cx || !cx->win || !policy) return nullptr;
    char name[48];
    snprintf(name, sizeof(name), "shell-images/%p", (void*)policy);
    DocumentImages* images = FrameKeyedState<DocumentImages>(
        cx, KeyedKey(KeyedName(cx, Str(name)),
                     HashClickId(StrL("DocumentImages"))));
    if (images && !images->policy) {
        images->policy = PolicyRetain(policy);
        images->app = cx->app;
    }
    if (images) {
        images->win = cx->win;
        images->deadline = Listen(cx, &ScriptView::OnImageDeadline);
    }
    return images;
}

static El* Construct(Ctx* cx, ShellRuntime* runtime,
                     const shell::Component& component,
                     const MaterialBehavior& behavior) {
    Str id = component.text;
    Listener click = ClickListener(cx, behavior.onClick);
    switch (component.kind) {
        case shell::ComponentKind::Div:
            return Div(cx->a);
        // base's h_flex(): flex().flex_row().items_center() — a row
        // centres its children across, where v_flex() leaves them to
        // stretch.
        case shell::ComponentKind::HFlex:
            return Div(cx->a)->FlexRow()->ItemsCenter();
        case shell::ComponentKind::VFlex:
            return Div(cx->a)->FlexCol();
        case shell::ComponentKind::Text:
            // A GPUI string element: it wraps at the width it is given, as
            // white_space's default `normal` says, unless an element above
            // it named whitespace_nowrap or truncate (PrepareEl cascades it).
            return TextEl(cx->a, component.text)->Wrap();
        case shell::ComponentKind::TextView: {
            IdScope scope(cx, id);
            TextView* view =
                component.textViewFormat == shell::TextViewFormat::Html
                    ? TextView::NewHtml(cx, component.value)
                    : TextView::New(cx, component.value);
            if (behavior.hasSelectable) view->Selectable(behavior.selectable);
            if (behavior.hasTextScrollable)
                view->Scrollable(behavior.textScrollable);
            view->OnLinkWithContext(Listen(cx, &ScriptView::OnTextLink),
                                    (intptr_t)behavior.onLinkClick);
            // text_view::with_policy: every document image, embedded data
            // URLs included, loads under the describing script's grant.
            view->ImageSource(DocumentImageSource,
                              DocumentImagesFor(cx, component.policy));
            return view->IntoEl();
        }
        case shell::ComponentKind::Button:
            return Button::New(cx, id, behavior.disabled, click, true, nullptr,
                               behavior.selected);
        case shell::ComponentKind::Link: {
            El* link = Link::New(cx, id, behavior.disabled, click);
            if (!behavior.disabled && behavior.href)
                link->OnClick(OpenUrlCallback(cx, behavior.href));
            return link;
        }
        case shell::ComponentKind::Checkbox: {
            CheckboxState state =
                behavior.indeterminate
                    ? CheckboxState::Indeterminate
                    : (behavior.checked ? CheckboxState::Checked
                                        : CheckboxState::Unchecked);
            El* e = Checkbox::New(
                cx, id, state, behavior.disabled,
                behavior.onChange ? ChangeListener(cx) : Listener{}, nullptr,
                nullptr, behavior.accessibilityLabel, behavior.tabIndex,
                behavior.tabStop);
            if (behavior.onChange && behavior.onChange <= INT32_MAX)
                e->Click((int)behavior.onChange);
            return e;
        }
        case shell::ComponentKind::Switch: {
            El* e =
                Switch::New(cx, id, behavior.checked, behavior.disabled,
                            behavior.onChange ? ChangeListener(cx) : Listener{},
                            nullptr, nullptr, behavior.accessibilityLabel,
                            behavior.tabIndex, behavior.tabStop);
            if (behavior.onChange && behavior.onChange <= INT32_MAX)
                e->Click((int)behavior.onChange);
            return e;
        }
        case shell::ComponentKind::Tabs:
            return Tabs::New(cx, id);
        case shell::ComponentKind::Tab:
            return Tab::New(cx, id, behavior.disabled, click, behavior.selected,
                            behavior.accessibilityLabel);
        case shell::ComponentKind::Progress:
            return Progress::New(cx, id, behavior.value, behavior.indeterminate,
                                 behavior.accessibilityLabel);
        // The accordion root and the pagination root are groups: identity and
        // the announced landmark, and nothing else on screen. A pagination's
        // page buttons are the script's own elements; what base contributes
        // that a script could not write for itself is the ellipsis layout,
        // and that is a calculation — exported as `pagination_items(...)`
        // rather than as a component.
        case shell::ComponentKind::Accordion:
            return Accordion::New(cx, id);
        case shell::ComponentKind::Pagination:
            return Pagination::New(cx, id);
        // Reached outside the arrangement they belong to: each exists to be
        // resolved by its root, so one used as an ordinary child is a mistake
        // worth naming rather than an empty box worth puzzling over.
        case shell::ComponentKind::AccordionHeader:
        case shell::ComponentKind::AccordionPanel:
        case shell::ComponentKind::AccordionTrigger:
            logf(
                "shell: an %s belongs in an AccordionItem's slots; used as "
                "an ordinary child it draws nothing\n",
                Str(shell::ComponentName(component)));
            return Div(cx->a);
        case shell::ComponentKind::AvatarImage:
        case shell::ComponentKind::AvatarFallback:
            logf(
                "shell: an %s belongs in an Avatar's slot — "
                "`Avatar.new().image(...)` or `.fallback(...)`; used as an "
                "ordinary child it draws nothing\n",
                Str(shell::ComponentName(component)));
            return Div(cx->a);
        case shell::ComponentKind::ProgressTrack:
            return ProgressTrack::New(cx);
        case shell::ComponentKind::ProgressIndicator:
            return ProgressIndicator::New(cx);
        case shell::ComponentKind::FpsMonitor: {
            FpsOverlayOpts opts;
            opts.frameBudget = behavior.fpsFrameBudget;
            return FpsMonitorEl(cx, opts);
        }
        case shell::ComponentKind::Radio: {
            El* e =
                Radio::New(cx, id, behavior.checked, behavior.disabled,
                           behavior.onChange ? ChangeListener(cx) : Listener{});
            if (behavior.onChange && behavior.onChange <= INT32_MAX)
                e->Click((int)behavior.onChange);
            return e;
        }
        case shell::ComponentKind::Toggle: {
            El* e = Toggle::New(
                cx, id, behavior.pressed, behavior.disabled,
                behavior.onChange ? ChangeListener(cx) : Listener{});
            if (behavior.onChange && behavior.onChange <= INT32_MAX)
                e->Click((int)behavior.onChange);
            return e;
        }
        case shell::ComponentKind::RadioGroup:
            return RadioGroup::New(cx, id, behavior.axis);
        case shell::ComponentKind::ToggleGroup:
            return ToggleGroup::New(cx, id, behavior.axis);
        case shell::ComponentKind::Table:
            return Table::New(cx, id, behavior.rowCount, behavior.columnCount,
                              behavior.accessibilityLabel);
        case shell::ComponentKind::TableHeader:
            return TableHeader::New(cx, id);
        case shell::ComponentKind::TableBody:
            return TableBody::New(cx, id);
        case shell::ComponentKind::TableRow:
            return TableRow::New(cx, id, (int)component.index);
        case shell::ComponentKind::TableHead:
            return TableHead::New(cx, id, (int)component.index);
        case shell::ComponentKind::TableCell:
            return TableCell::New(cx, id, (int)component.index);
        case shell::ComponentKind::TableCaption:
            return TableCaption::New(cx, id);
        case shell::ComponentKind::Select:
        case shell::ComponentKind::Combobox: {
            bool open = behavior.hasOpen ? behavior.open : false;
            El* root =
                Select::New(cx, id, open, behavior.disabled,
                            component.kind == shell::ComponentKind::Select
                                ? behavior.accessibilityLabel
                                : Str{});
            ShellSelectBinding* binding = ArenaNew<ShellSelectBinding>(cx->a);
            binding->onOpenChange = behavior.onOpenChange;
            binding->onConfirm = behavior.onConfirm;
            binding->onDismiss = behavior.onDismiss;
            binding->open = open;
            binding->disabled = behavior.disabled;
            binding->triggerFocus = RetainedFocus(runtime, behavior.focus);
            binding
                ->contentFocus = RetainedFocus(runtime, behavior.contentFocus);
            if (binding->triggerFocus.IsValid())
                root->TrackFocus(binding->triggerFocus);
            SelectInitKeys();
            Listener action =
                Listen(cx, &ScriptView::OnSelectAction, (intptr_t)binding);
            root->KeyContext(SelectContext())
                ->OnAction(action::SelectUp(), action)
                ->OnAction(action::SelectDown(), action)
                ->OnAction(action::Confirm(), action)
                ->OnAction(action::Cancel(), action);
            // The enabled root exposes activation whether open or closed:
            // it opens a closed select and closes an open one through the
            // same steps Cancel takes.
            if (!behavior.disabled && behavior.onOpenChange)
                root->OnAccessibilityDefault(Listen(
                    cx, &ScriptView::OnSelectActivate, (intptr_t)binding));
            return root;
        }
        case shell::ComponentKind::DatePicker: {
            bool open = behavior.hasOpen ? behavior.open : false;
            El* root = DatePicker::New(cx, id, behavior.disabled, open);
            FocusHandle focus = RetainedFocus(runtime, component.handle);
            if (!focus.IsValid()) return Div(cx->a);
            root->TrackFocus(focus)->TabStop(!behavior.disabled);
            if (behavior.onOpenChange) {
                ShellBoolBinding* toggle = ArenaNew<ShellBoolBinding>(cx->a);
                toggle->callback = behavior.onOpenChange;
                toggle->value = !open;
                DatePickerBindKeys(
                    cx, root, id,
                    Listen(cx, &ScriptView::OnBoundBool, (intptr_t)toggle),
                    Listener{}, open, behavior.disabled);
            }
            return root;
        }
        case shell::ComponentKind::Scrollbar:
            return Scrollbar::New(cx);
        case shell::ComponentKind::Svg: {
            El* icon = IconEl(cx->a, IconName::None);
            icon->iconPath = component.text;
            return icon;
        }
        case shell::ComponentKind::Image:
            return ImageEl(cx->a, component.text);
        case shell::ComponentKind::Slider:
        case shell::ComponentKind::SliderTrack:
        case shell::ComponentKind::SliderIndicator:
        case shell::ComponentKind::SliderThumb: {
            shell::RetainedEntry* retained =
                runtime ? runtime->Retained(component.handle) : nullptr;
            SliderState* state =
                retained && retained->kind == shell::RetainedKind::Slider
                    ? retained->slider
                    : nullptr;
            if (state && !behavior.disabled) {
                state->onChange = Listen(cx, &ScriptView::OnSliderEvent,
                                         (intptr_t)(uint32_t)retained->id);
            }
            if (component.kind == shell::ComponentKind::Slider)
                return Slider::New(cx, behavior.disabled ? nullptr : state,
                                   behavior.axis);
            if (component.kind == shell::ComponentKind::SliderTrack)
                return SliderTrack::New(cx, behavior.disabled ? nullptr : state,
                                        behavior.axis);
            if (component.kind == shell::ComponentKind::SliderIndicator)
                return SliderIndicator::New(cx, state);
            return SliderThumb::New(cx);
        }
        case shell::ComponentKind::Input:
        case shell::ComponentKind::Textarea: {
            shell::RetainedEntry* retained =
                runtime ? runtime->Retained(component.handle) : nullptr;
            bool textarea = component.kind == shell::ComponentKind::Textarea;
            InputState* state =
                retained && retained->kind ==
                                (textarea ? shell::RetainedKind::Textarea
                                          : shell::RetainedKind::Input)
                    ? retained->input
                    : nullptr;
            if (!state) return Div(cx->a);
            state->disabled = behavior.disabled;
            InstallShellTokens(cx, runtime, state, behavior, false);
            state->onChange = Listen(cx, &ScriptView::OnInputEvent,
                                     (intptr_t)(uint32_t)retained->id);
            Str nativeId =
                StrDup(cx->a, fmt("gpui-shell-%s-%u",
                                  textarea ? StrL("textarea") : StrL("input"),
                                  retained->id));
            El* frame =
                InputBase::New(cx, nativeId, !behavior.disabled,
                               textarea ? AccessibilityRole::MultilineTextInput
                                        : AccessibilityRole::TextInput)
                    ->BindInput(behavior.disabled ? nullptr : state)
                    ->Flex()
                    ->W(kFill);
            if (textarea) {
                frame->ItemsStart()->Child(Textarea::New(cx, state));
            } else {
                frame->ItemsCenter()->Child(Input::New(cx, state));
            }
            return frame;
        }
        case shell::ComponentKind::NumberInput: {
            shell::RetainedEntry* retained =
                runtime ? runtime->Retained(component.handle) : nullptr;
            InputState* state =
                retained && retained->kind == shell::RetainedKind::Input
                    ? retained->input
                    : nullptr;
            if (!state) return Div(cx->a);
            state->disabled = behavior.disabled;
            state->onChange = Listen(cx, &ScriptView::OnInputEvent,
                                     (intptr_t)(uint32_t)retained->id);
            Str nativeId =
                StrDup(cx->a, fmt("gpui-shell-number-input-%u", retained->id));
            return NumberInput::New(cx, nativeId, state);
        }
        case shell::ComponentKind::OtpInput: {
            shell::RetainedEntry* retained =
                runtime ? runtime->Retained(component.handle) : nullptr;
            if (!retained || retained->kind != shell::RetainedKind::Otp)
                return Div(cx->a);
            OtpState* state = retained->otp.Get(cx);
            if (!state) return Div(cx->a);
            state->disabled = behavior.disabled;
            state->onChange = Listen(cx, &ScriptView::OnOtpEvent,
                                     (intptr_t)(uint32_t)retained->id);
            Str nativeId =
                StrDup(cx->a, fmt("gpui-shell-otp-%u", retained->id));
            return OtpInput::New(cx, nativeId, retained->otp);
        }
        case shell::ComponentKind::VVirtualList:
        case shell::ComponentKind::HVirtualList: {
            const shell::VirtualListSpec* list = component.virtualList;
            if (!list || !runtime) return Div(cx->a);
            float* sizes =
                list->sizeCount > 0
                    ? (float*)Alloc(
                          cx->a, (int)(sizeof(float) * (size_t)list->sizeCount))
                    : nullptr;
            for (int i = 0; sizes && i < list->sizeCount; i++) {
                sizes[i] = list->axis == Axis::Horizontal ? list->sizes[i].w
                                                          : list->sizes[i].h;
            }
            MaterialVirtualUser* user = ArenaNew<MaterialVirtualUser>(cx->a);
            user->runtime = runtime;
            user->render = list->renderItems;
            user->getKey = list->getKey;
            user->onItemClick = behavior.onItemClick;
            user->onItemSecondaryClick = behavior.onItemSecondaryClick;
            VirtualListOpts opts;
            opts.count = list->sizeCount;
            opts.sizes = sizes;
            opts.layoutAxis = list->axis;
            opts.range = MaterialVirtualRange;
            opts.user = user;
            if (behavior.virtualScroll) {
                shell::RetainedEntry* scroll =
                    runtime->Retained(behavior.virtualScroll);
                if (scroll &&
                    scroll->kind == shell::RetainedKind::VirtualScroll)
                    opts.handle = &scroll->scroll;
            }
            // Without track_scroll the list's position is its own, in
            // element state under its id, as GPUI keeps it: the wheel over
            // it writes it back.
            if (!opts.handle && cx->win) {
                ShellScrollPosition* at = KeyedState<ShellScrollPosition>(
                    cx, KeyedKey((uint32_t)HashClickId(list->id),
                                 (uint32_t)HashClickId(StrL("ShellScroll"))));
                if (at) {
                    opts.scrollX = at->x;
                    opts.scrollY = at->y;
                    opts.scrollId = HashClickId(list->id);
                    opts.onScroll =
                        Listen(cx, &ScriptView::OnScrollPosition, (intptr_t)at);
                }
            }
            return VirtualList::New(cx, list->id, opts);
        }
        case shell::ComponentKind::List:
        case shell::ComponentKind::UniformList:
            return LazyListElement(cx, runtime, component, behavior);
        case shell::ComponentKind::ChildView: {
            EntityId child =
                runtime ? runtime->NestedView(component.handle, cx->app)
                        : EntityId{};
            El* rendered = child.IsValid()
                               ? EntityRender(cx->app, cx->win, cx->a, child)
                               : nullptr;
            return rendered ? rendered : Div(cx->a);
        }
        case shell::ComponentKind::PathFill:
        case shell::ComponentKind::PathStroke:
            return Div(cx->a);
        case shell::ComponentKind::HResizable:
            return Div(cx->a)->FlexRow();
        case shell::ComponentKind::VResizable:
            return Div(cx->a)->FlexCol();
        case shell::ComponentKind::ResizablePanel:
            return Div(cx->a)->Flex1();
        case shell::ComponentKind::Collapsible:
        case shell::ComponentKind::Popover:
        case shell::ComponentKind::HoverCard:
        case shell::ComponentKind::Popup:
            return Div(cx->a);
        // MaterializeNode builds these four itself and never reaches here.
        // They are named rather than left to the fallback so that a kind added
        // later still fails the exhaustiveness check instead of silently
        // materializing as an empty box.
        case shell::ComponentKind::Avatar:
        case shell::ComponentKind::AccordionItem:
        case shell::ComponentKind::DockArea:
        case shell::ComponentKind::DockContent:
        case shell::ComponentKind::Registered:
            return Div(cx->a);
    }
    return Div(cx->a);
}

// Applies a node's own style — the style methods and the motions that move
// them — to `target`: what a registered component's request hands its
// materializer as Rust's refinement.
static void ApplyOwnStyle(Ctx* cx, const shell::SpecNode* node,
                          shell::SpecId id, const MaterialBehavior& behavior,
                          El* target, ShellError* error) {
    for (const shell::SpecOp& op : node->ops) {
        if (op.kind == shell::SpecOpKind::NullaryStyle) {
            if (!ApplyNullary(target, op.name) && error && !error->IsSet())
                ShellErrorSet(error, shell::UnknownElementMethodTemp(op.name));
        } else if (op.kind == shell::SpecOpKind::ParamStyle) {
            if (!ApplyParam(target, op, error) && error && !error->IsSet())
                ShellErrorSet(error, fmt("invalid style call `%s`", op.name));
        }
    }
    ApplyMotions(cx, node, id, behavior, target);
}

void ShellApplyNodeStyle(Ctx* cx, const shell::SpecArena* specs,
                         shell::SpecId id, El* target, ShellError* error) {
    const shell::SpecNode* node = specs ? specs->Node(id) : nullptr;
    if (!node || !target) return;
    MaterialBehavior behavior = {};
    ResolveBehavior(node, &behavior);
    ApplyOwnStyle(cx, node, id, behavior, target, error);
}

// materialize.rs FACTORY_MATERIALIZE_ERRORS: the error frame a
// ShellTryMaterialize holds open, which the first registered component that
// fails under it fills. Only ShellTryMaterialize sets it. Rust also opens one
// around each slot factory build; here a factory's failure is noticed by its
// adapter through ShellRuntime::ComponentFailureCount, so under a check it
// lands in the check's frame instead of in a nested one.
static ShellError* gMaterializeErrorFrame = nullptr;

// materialize.rs materialize_registered_component: builds the request, hands
// it to the descriptor's materializer, and reports what it left unread.
static El* MaterializeRegistered(Ctx* cx, ShellRuntime* runtime,
                                 const shell::SpecArena* specs,
                                 shell::SpecId id, const shell::SpecNode* node,
                                 const MaterialBehavior& behavior,
                                 ShellError* error) {
    const shell::FrozenComponentRegistry* components =
        runtime ? runtime->Components() : nullptr;
    const shell::ComponentDescriptor* descriptor =
        components ? components->Descriptor(node->component.index) : nullptr;
    Str name = node->component.text;
    if (!descriptor || !descriptor->materialize) {
        logf(
            "shell: registered component `%s` has an unknown registry id "
            "%u\n",
            name, node->component.index);
        return Div(cx->a)->Child(
            TextEl(cx->a, StrDup(cx->a, fmt("Unknown component: %s", name))));
    }
    shell::MaterializeRequest request;
    request.cx = cx;
    request.runtime = runtime;
    request.specs = specs;
    request.node = node;
    request.id = id;
    request.descriptor = descriptor;
    request.payload = node->component.payload;
    request.elementId = behavior.key
                            ? behavior.key
                            : StrDup(cx->a, fmt("gpui-shell-spec-%u", id));
    request.disabled = behavior.disabled;
    request.selected = behavior.selected;
    request.onClick = behavior.onClick;
    request.error = error;
    // Filling the same slot twice replaces it, the way resolve() folds the
    // slot specs: the last call in the chain is what the script meant, and
    // the ones it replaced are never built. They count as read.
    int opCount = 0;
    for (const shell::SpecOp& op : node->ops) {
        (void)op;
        opCount++;
    }
    request.slotTaken = (bool*)Alloc(cx->a, (int)sizeof(bool) * (opCount + 1));
    memset(request.slotTaken, 0, sizeof(bool) * (size_t)(opCount + 1));
    int slotAt = 0;
    for (const shell::SpecOp& op : node->ops) {
        int at = slotAt++;
        if (op.kind != shell::SpecOpKind::Slot) continue;
        int laterAt = 0;
        for (const shell::SpecOp& later : node->ops) {
            if (laterAt++ > at && later.kind == shell::SpecOpKind::Slot &&
                StrEq(later.name, op.name)) {
                request.slotTaken[at] = true;
                break;
            }
        }
    }
    El* element = descriptor->materialize(&request);
    if (!element) {
        Str why = request.failure.s ? request.failure : StrL("no element");
        logf("shell: failed to materialize `%s`: %s\n", name, why);
        if (runtime) runtime->NoteComponentFailure(why);
        if (gMaterializeErrorFrame && !gMaterializeErrorFrame->IsSet()) {
            ShellErrorSet(gMaterializeErrorFrame,
                          fmt("failed to materialize `%s`: %s", name, why));
        }
        return Div(cx->a)->Child(
            TextEl(cx->a, StrDup(cx->a, fmt("Failed to render %s", name))));
    }
    if (!request.styleTaken && request.HasStyle()) {
        logf(
            "shell: %s did not consume its style; call "
            "MaterializeRequest::Finish or ApplyStyle\n",
            name);
    }
    int unread = request.ChildrenLen();
    if (unread != 0) {
        logf("shell: %s did not consume %d child element(s)\n", name, unread);
    }
    int index = 0;
    for (const shell::SpecOp& op : node->ops) {
        int at = index++;
        if (op.kind != shell::SpecOpKind::Slot) continue;
        if (request.slotTaken && request.slotTaken[at]) continue;
        logf(
            "shell: %s has no `%s` slot, so the element given to it is not "
            "rendered at all\n",
            name, op.name);
    }
    return element;
}

static El* MaterializeNode(Ctx* cx, ShellRuntime* runtime,
                           const shell::SpecArena* specs, shell::SpecId id,
                           ShellError* error) {
    const shell::SpecNode* node = specs ? specs->Node(id) : nullptr;
    if (!node) return Div(cx->a);
    MaterialBehavior behavior = {};
    ResolveBehavior(node, &behavior);
    if (node->component.kind == shell::ComponentKind::Registered) {
        return MaterializeRegistered(cx, runtime, specs, id, node, behavior,
                                     error);
    }

    El* element = nullptr;
    bool childrenConsumed = false;
    if (node->component.kind == shell::ComponentKind::OtpInput) {
        shell::RetainedEntry* retained =
            runtime ? runtime->Retained(node->component.handle) : nullptr;
        if (!retained || retained->kind != shell::RetainedKind::Otp)
            return Div(cx->a);
        OtpState* state = retained->otp.Get(cx);
        if (!state) return Div(cx->a);
        state->disabled = behavior.disabled;
        state->onChange = Listen(cx, &ScriptView::OnOtpEvent,
                                 (intptr_t)(uint32_t)retained->id);
        Str nativeId = StrDup(cx->a, fmt("gpui-shell-otp-%u", retained->id));
        element = OtpInput::New(cx, nativeId, retained->otp);
        const shell::SpecNode* cellStyle =
            StateNode(specs, node, StrL("cell_style"));
        const shell::SpecNode* activeStyle =
            StateNode(specs, node, StrL("cell_active_style"));
        const shell::SpecNode* caretStyle =
            StateNode(specs, node, StrL("caret_style"));
        bool focused =
            !behavior.disabled && FocusHandleIsFocused(cx->win, state->focus);
        int active = focused
                         ? (state->len < state->length
                                ? state->len
                                : (state->length > 0 ? state->length - 1 : 0))
                         : -1;
        bool caret = focused && OtpCursorVisible(state, cx->app);
        for (int i = 0; i < state->length; i++) {
            El* cell = Div(cx->a);
            uint32_t ignored = 0;
            ApplyStyleNode(cx->a, cellStyle, cell, &ignored, error);
            if (i == active)
                ApplyStyleNode(cx->a, activeStyle, cell, &ignored, error);
            if (i < state->len) {
                cell->Child(TextEl(cx->a, state->masked
                                              ? StrL("•")
                                              : Str(state->value.els + i, 1)));
            } else if (i == active && caret && caretStyle) {
                El* mark = Div(cx->a);
                ApplyStyleNode(cx->a, caretStyle, mark, &ignored, error);
                cell->Child(mark);
            }
            element->Child(cell);
        }
        for (shell::SpecId child : node->children)
            element->Child(MaterializeNode(cx, runtime, specs, child, error));
        childrenConsumed = true;
    } else if (node->component.kind == shell::ComponentKind::NumberInput) {
        shell::RetainedEntry* retained =
            runtime ? runtime->Retained(node->component.handle) : nullptr;
        InputState* state =
            retained && retained->kind == shell::RetainedKind::Input
                ? retained->input
                : nullptr;
        if (!state) return Div(cx->a);
        state->disabled = behavior.disabled;
        state->onChange = Listen(cx, &ScriptView::OnInputEvent,
                                 (intptr_t)(uint32_t)retained->id);
        Str nativeId =
            StrDup(cx->a, fmt("gpui-shell-number-input-%u", retained->id));
        Listener onStep = behavior.onStep
                              ? Listen(cx, &ScriptView::OnNumberStep,
                                       (intptr_t)behavior.onStep)
                              : Listener{};
        NumberStep amount = NumberStep::Fixed(retained->number.step);
        const NumberStep* step =
            !behavior.onStep && retained->number.hasStep ? &amount : nullptr;
        Func0 decrement = NumberInputStepCallback(
            cx, state, StepAction::Decrement, step, retained->number.hasMin,
            retained->number.min, retained->number.hasMax, retained->number.max,
            behavior.disabled, onStep);
        Func0 increment = NumberInputStepCallback(
            cx, state, StepAction::Increment, step, retained->number.hasMin,
            retained->number.min, retained->number.hasMax, retained->number.max,
            behavior.disabled, onStep);
        El* decrementButton = NumberStepButton(
            cx, runtime, specs, SlotId(node, "decrement_button"),
            StrL("decrement"), behavior.disabled, decrement, error);
        El* incrementButton = NumberStepButton(
            cx, runtime, specs, SlotId(node, "increment_button"),
            StrL("increment"), behavior.disabled, increment, error);
        El* editor = Slot(cx, runtime, specs, node, "input", error);
        if (!editor) editor = Input::New(cx, state);
        El* ordinary = nullptr;
        if (node->children.len > 0) {
            ordinary = Div(cx->a)->FlexRow();
            for (shell::SpecId child : node->children)
                ordinary
                    ->Child(MaterializeNode(cx, runtime, specs, child, error));
        }
        element = NumberInput::Compose(cx, nativeId, state, behavior.disabled,
                                       decrementButton, editor, incrementButton,
                                       behavior.controlsRight, ordinary);
        ShellNumberBinding* key = ArenaNew<ShellNumberBinding>(cx->a);
        key->state = state;
        key->step = amount;
        key->hasStep = retained->number.hasStep;
        key->hasMin = retained->number.hasMin;
        key->min = retained->number.min;
        key->hasMax = retained->number.hasMax;
        key->max = retained->number.max;
        key->disabled = behavior.disabled;
        key->onStep = behavior.onStep;
        element->TrackFocus(state->focus)
            ->OnAccessibilityDecrement(decrement)
            ->OnAccessibilityIncrement(increment)
            ->OnKeyDown(Listen(cx, &ScriptView::OnNumberKey, (intptr_t)key));
        double numeric = 0;
        if (NumberParseValue(InputValue(state), &numeric))
            element->AriaNumericValue((float)numeric);
        childrenConsumed = true;
    } else if (node->component.kind == shell::ComponentKind::PathFill ||
               node->component.kind == shell::ComponentKind::PathStroke) {
        element = Div(cx->a);
        MaterialPath* path = ArenaNew<MaterialPath>(cx->a);
        path->node = node;
        element->customPaint = &PaintMaterialPath;
        element->customUser = path;
        childrenConsumed = true;
    } else if (node->component.kind == shell::ComponentKind::HResizable ||
               node->component.kind == shell::ComponentKind::VResizable) {
        Axis axis = node->component.kind == shell::ComponentKind::HResizable
                        ? Axis::Horizontal
                        : Axis::Vertical;
        ResizablePanelGroup* group =
            ResizablePanelGroup::New(cx, node->component.text, {}, axis);
        if (behavior.onResize) {
            group->OnResize(
                Listen(cx, &ScriptView::OnResize, (intptr_t)behavior.onResize));
        }
        for (shell::SpecId childId : node->children) {
            const shell::SpecNode* childNode = specs->Node(childId);
            El* content = MaterializeNode(cx, runtime, specs, childId, error);
            if (!childNode || childNode->component.kind !=
                                  shell::ComponentKind::ResizablePanel) {
                group->Grow(content);
                continue;
            }
            MaterialBehavior panelBehavior = {};
            ResolveBehavior(childNode, &panelBehavior);
            ResizablePanel* panel = ResizablePanel::New(cx)->Child(content);
            if (panelBehavior.hasPanelSize)
                panel->Size(panelBehavior.panelSize);
            if (panelBehavior.hasSizeRange)
                panel
                    ->SizeRange(panelBehavior.panelMin, panelBehavior.panelMax);
            if (panelBehavior.hasPanelVisible)
                panel->Visible(panelBehavior.panelVisible);
            if (content && content->style.flexGrow <= 0) panel->FlexNone();
            group->Child(panel);
        }
        element = group->IntoEl();
        childrenConsumed = true;
    } else if (node->component.kind == shell::ComponentKind::Collapsible) {
        Collapsible* collapsible = Collapsible::New(cx)->Open(behavior.open);
        for (shell::SpecId child : node->children)
            collapsible
                ->Child(MaterializeNode(cx, runtime, specs, child, error));
        if (El* content = Slot(cx, runtime, specs, node, "content", error))
            collapsible->Content(content);
        element = collapsible->IntoEl();
    } else if (node->component.kind == shell::ComponentKind::Popup) {
        El* trigger = Slot(cx, runtime, specs, node, "trigger", error);
        El* content = Slot(cx, runtime, specs, node, "content", error);
        if (trigger) {
            Popup* popup = Popup::New(
                cx, node->component.text, trigger,
                behavior.hasAnchor ? behavior.anchor : PopupAnchor::TopLeft);
            if (content) popup->Content(content);
            element = popup->IntoEl();
        }
    } else if (node->component.kind == shell::ComponentKind::Popover) {
        El* trigger = Slot(cx, runtime, specs, node, "trigger", error);
        El* content = Slot(cx, runtime, specs, node, "content", error);
        if (trigger) {
            Entity<PopoverState> state = KeyedEntity<PopoverState>(
                cx, KeyedName(cx, node->component.text));
            PopoverState* held = state.Get(cx);
            if (held && !held->seeded) {
                held->seeded = true;
                held->open = behavior.defaultOpen;
            }
            if (behavior.hasOpen) PopoverSetOpen(cx, state, behavior.open);
            bool open = PopoverIsOpen(cx, state);
            Popover* popover = Popover::New(cx, node->component.text, state,
                                            behavior.mouseButton)
                                   ->OverlayClosable(behavior.overlayClosable)
                                   ->Trigger(trigger);
            if (behavior.hasAnchor) popover->Anchor(behavior.anchor);
            if (behavior.contentFocus && runtime) {
                shell::RetainedEntry* focus =
                    runtime->Retained(behavior.contentFocus);
                if (focus && focus->kind == shell::RetainedKind::Focus)
                    popover->TrackedFocus(focus->focus);
            }
            if (open && content) popover->Content(content);
            if (behavior.onOpenChange)
                popover->OnOpenChange(Listen(cx, &ScriptView::OnOpenChange,
                                             (intptr_t)behavior.onOpenChange));
            if (behavior.onDismiss)
                popover->OnDismiss(Listen(cx, &ScriptView::OnClick,
                                          (intptr_t)behavior.onDismiss));
            element = popover->IntoEl();
            if (open)
                CancelBindKeys(cx, element, "Popover", node->component.text,
                               ListenTo(state, &PopoverDismiss));
        }
    } else if (node->component.kind == shell::ComponentKind::Avatar) {
        // Base renders the element in its `image` slot, or the one in its
        // `fallback` slot when there is no image, and draws nothing itself:
        // no circle, no size, no background. All of that is the script's.
        El* image = Slot(cx, runtime, specs, node, "image", error);
        El* fallback = Slot(cx, runtime, specs, node, "fallback", error);
        if (!image && !fallback) {
            logf(
                "shell: an Avatar with neither an `image` nor a `fallback` "
                "slot draws nothing\n");
        }
        Avatar* avatar = Avatar::New(cx);
        if (image) avatar->Image(image);
        if (fallback) avatar->Fallback(fallback);
        element = avatar->IntoEl();
    } else if (node->component.kind == shell::ComponentKind::AccordionHeader) {
        if (!gAccordionItem.active) {
            logf(
                "shell: an AccordionHeader belongs in an AccordionItem's "
                "`header` slot; used as an ordinary child it draws "
                "nothing\n");
            element = Div(cx->a);
        } else {
            // AccordionHeader::New takes the trigger, exactly as Popup::New
            // takes its own: a heading whose button arrived later would be a
            // heading that announced nothing for a frame.
            El* trigger = Slot(cx, runtime, specs, node, "trigger", error);
            if (!trigger) {
                // The empty button a malformed header falls back to, keyed by
                // the slot's own address rather than a fixed string, so two
                // broken items do not share one element id.
                trigger = AccordionTrigger::New(
                    cx, StrDup(cx->a, fmt("accordion-trigger-%u", id)));
            }
            element = AccordionHeader::New(
                cx, trigger, behavior.key,
                behavior.ariaLevel > 0 ? behavior.ariaLevel : 3);
        }
    } else if (node->component.kind == shell::ComponentKind::AccordionTrigger) {
        // `open` and `disabled` come from the item, which pushes its own down
        // over whatever was set here — so neither is read off the trigger.
        bool open = gAccordionItem.active ? gAccordionItem.open : behavior.open;
        bool disabled =
            gAccordionItem.active ? gAccordionItem.disabled : behavior.disabled;
        if (!behavior.onChange) {
            logf(
                "shell: an AccordionTrigger with no `on_change` cannot open "
                "anything: the item's `open` is the script's, and this is "
                "what asks it to flip\n");
        }
        element = AccordionTrigger::New(
            cx, node->component.text, open, disabled,
            behavior.onChange ? ChangeListener(cx) : Listener{});
        if (behavior.onChange && behavior.onChange <= INT32_MAX)
            element->Click((int)behavior.onChange);
    } else if (node->component.kind == shell::ComponentKind::AccordionPanel) {
        element = AccordionPanel::New(cx, behavior.key);
    } else if (node->component.kind == shell::ComponentKind::AccordionItem) {
        // The item passes its own `open` down to both halves, so a script sets
        // it once rather than three times in agreement with itself: the
        // trigger announces it, the panel mounts on it, and neither can drift.
        // An item with no `open` is a closed one — base has no uncontrolled
        // mode, and the script owns which item is showing.
        AccordionItem* item = AccordionItem::New(cx)
                                  ->Open(behavior.open)
                                  ->KeepMounted(behavior.keepMounted);
        AccordionItemScope scope(behavior.open, behavior.disabled);
        El* header = Slot(cx, runtime, specs, node, "header", error);
        El* panel = Slot(cx, runtime, specs, node, "panel", error);
        if (!header) {
            logf(
                "shell: an AccordionItem with no `header` slot has nothing "
                "to open it; the trigger lives in the header\n");
        }
        if (header) item->Header(header);
        if (panel) item->Panel(panel);
        element = item->IntoEl();
    } else if (node->component.kind == shell::ComponentKind::DockArea) {
        element = DockAreaElement(cx, runtime, node, behavior);
        childrenConsumed = true;
    } else if (node->component.kind == shell::ComponentKind::DockContent) {
        // Base hands a dock's content to the chrome as a finished element and
        // keeps whatever comes back, so a chrome that wants both has to place
        // the content itself. This is where the real one lands.
        El* content = shell::ShellDockTakeContent();
        if (!content) {
            logf(
                "shell: dock_content() was used outside a dock's chrome "
                "handler, or twice inside one; the dock's content is a "
                "single element and can only be placed once\n");
            element = Div(cx->a);
        } else {
            element = Div(cx->a)->Child(content);
        }
        childrenConsumed = true;
    } else if (node->component.kind == shell::ComponentKind::HoverCard) {
        El* trigger = Slot(cx, runtime, specs, node, "trigger", error);
        El* content = Slot(cx, runtime, specs, node, "content", error);
        Entity<HoverCardState> state =
            HoverCardStateFor(cx, node->component.text);
        HoverCardSetDelays(cx, state, behavior.openDelayMs,
                           behavior.closeDelayMs);
        HoverCard* card = HoverCard::New(cx, node->component.text, state);
        if (behavior.onOpenChange)
            card->OnOpenChange(Listen(cx, &ScriptView::OnOpenChange,
                                      (intptr_t)behavior.onOpenChange));
        if (trigger) card->Trigger(trigger);
        if (content && card->IsOpen()) {
            PopupPlaceContent(content, behavior.hasAnchor
                                           ? behavior.anchor
                                           : PopupAnchor::TopCenter);
            card->Content(content);
        }
        element = card->IntoEl();
    }
    if (!element) element = Construct(cx, runtime, node->component, behavior);

    if (node->component.kind == shell::ComponentKind::SliderIndicator) {
        shell::RetainedEntry* retained =
            runtime ? runtime->Retained(node->component.handle) : nullptr;
        SliderState* state =
            retained && retained->kind == shell::RetainedKind::Slider
                ? retained->slider
                : nullptr;
        const shell::SpecNode* range =
            StateNode(specs, node, StrL("range_style"));
        if (state && range) {
            El* fill = Div(cx->a)->Absolute();
            uint32_t ignored = 0;
            ApplyStyleNode(cx->a, range, fill, &ignored, error);
            if (behavior.axis == Axis::Vertical) {
                fill->BottomRel(state->pctLo)
                    ->TopRel(1.f - state->pctHi)
                    ->W(kFill);
            } else {
                fill->LeftRel(state->pctLo)
                    ->RightRel(1.f - state->pctHi)
                    ->H(kFill);
            }
            element->Child(fill);
        }
    }

    bool borderColored = false;
    for (const shell::SpecOp& op : node->ops) {
        if (op.kind == shell::SpecOpKind::NullaryStyle) {
            if (!ApplyNullary(element, op.name) && error && !error->IsSet())
                ShellErrorSet(error, shell::UnknownElementMethodTemp(op.name));
        } else if (op.kind == shell::SpecOpKind::ParamStyle) {
            if (StrEq(op.name, "border_color")) borderColored = true;
            if (!ApplyParam(element, op, error) && error && !error->IsSet())
                ShellErrorSet(error, fmt("invalid style call `%s`", op.name));
        }
    }
    // GPUI draws an unset border colour as transparent: `div().border(1)`
    // takes up the width and paints nothing. An El's colour defaults to
    // black, which is what the port's own widgets rely on, so a plain box the
    // script bordered without a colour is told so here.
    bool plainBox = node->component.kind == shell::ComponentKind::Div ||
                    node->component.kind == shell::ComponentKind::HFlex ||
                    node->component.kind == shell::ComponentKind::VFlex;
    if (plainBox && !borderColored)
        element->style.borderColor = Rgba{0, 0, 0, 0};
    ApplyMotions(cx, node, id, behavior, element);
    if (node->component.kind == shell::ComponentKind::SliderThumb) {
        shell::RetainedEntry* retained =
            runtime ? runtime->Retained(node->component.handle) : nullptr;
        SliderState* state =
            retained && retained->kind == shell::RetainedKind::Slider
                ? retained->slider
                : nullptr;
        if (state) {
            float at = behavior.start ? state->pctLo : state->pctHi;
            element->Absolute();
            if (behavior.axis == Axis::Vertical) {
                element->style.absTop = kAuto;
                element->style.absTopRel = 0;
                element->BottomRel(at);
            } else {
                element->style.absRight = kAuto;
                element->style.absRightRel = 0;
                element->LeftRel(at);
            }
        }
    }
    ApplyStateStyles(cx, specs, node, element, error);
    if (behavior.key) element->PathId(behavior.key);
    if (behavior.accessibilityLabel)
        element->AriaLabel(behavior.accessibilityLabel);
    if (behavior.role) {
        AccessibilityRole role =
            shell::AccessibilityRoleFromName(behavior.role);
        if (role != AccessibilityRole::None) element->Role(role);
    }
    if (behavior.hasAriaSelected) element->AriaSelected(behavior.ariaSelected);
    if (behavior.ariaActiveDescendant) element->AriaActiveDescendant();
    if (behavior.hasPosition) {
        element->AriaPositionInSet(behavior.positionInSet)
            ->AriaSizeOfSet(behavior.sizeOfSet);
    }
    if (behavior.focus && runtime) {
        shell::RetainedEntry* focus = runtime->Retained(behavior.focus);
        if (focus && focus->kind == shell::RetainedKind::Focus)
            element->TrackFocus(focus->focus);
    }
    if (behavior.tooltip) {
        // ShellRoot::tooltip_overlay: a script's tooltip shows in the shell
        // root's own layer, with its look, not in the window's.
        element->Tip(behavior.tooltip);
        element->rootTooltip = true;
    }
    element->TabIndex(behavior.tabIndex)->TabStop(behavior.tabStop);
    if (behavior.scrollX || behavior.scrollY ||
        node->component.kind == shell::ComponentKind::Scrollbar) {
        Str scrollName = behavior.key ? behavior.key : node->component.text;
        // track_scroll_position / Scrollable: the offset lives in window
        // state under the element's identity (its key, or the spec node's
        // own "gpui-shell" id), and the element's scroll events write it
        // back, which is what lets a wheel over it move it.
        ShellScrollPosition* at = nullptr;
        if (cx->win && node->component
                               .kind != shell::ComponentKind::Scrollbar) {
            Str identity = scrollName
                               ? scrollName
                               : Str(fmt("gpui-shell-spec-%u", (unsigned)id));
            at = KeyedState<ShellScrollPosition>(
                cx, KeyedKey((uint32_t)HashClickId(identity),
                             (uint32_t)HashClickId(StrL("ShellScroll"))));
        }
        if (behavior.scrollX ||
            node->component.kind == shell::ComponentKind::Scrollbar)
            element->ScrollX(at ? at->x : 0);
        if (behavior.scrollY ||
            node->component.kind == shell::ComponentKind::Scrollbar)
            element->ScrollY(at ? at->y : 0);
        if (at)
            element->OnScroll(
                Listen(cx, &ScriptView::OnScrollPosition, (intptr_t)at));
        // overflow_*_scrollbar: materialize.rs hands the element to a
        // Scrollable, whose root is size_full refined by the element's own
        // size, so a side the script left unsized fills its parent rather
        // than taking its content's extent (and a row's items_center never
        // shrinks it).
        if (behavior.scrollbar) {
            if (element->style.width == kAuto && element->style.widthFrac == 0)
                element->W(kFill);
            if (element->style.height == kAuto && element->style
                                                          .heightFrac == 0)
                element->H(kFill);
        }
        if (scrollName)
            element->ScrollId(HashClickId(scrollName));
        else
            element->ScrollFromPath();
        if (!behavior.scrollbar &&
            node->component.kind != shell::ComponentKind::Scrollbar) {
            if (behavior.scrollX) element->HideScrollbarX();
            if (behavior.scrollY) element->HideScrollbarY();
        }
        if (behavior.hasScrollbarMode)
            element->ScrollMode(behavior.scrollbarMode);
    }
    if (!behavior.disabled && behavior.onClick &&
        node->component.kind != shell::ComponentKind::Button &&
        node->component.kind != shell::ComponentKind::Link &&
        node->component.kind != shell::ComponentKind::Tab) {
        element->OnClick(ClickListener(cx, behavior.onClick));
    }
    if (behavior.onHover)
        element->OnHover(
            Listen(cx, &ScriptView::OnHover, (intptr_t)behavior.onHover));
    if (behavior.onMouseMove)
        element->OnMouseMove(Listen(cx, &ScriptView::OnMouseMove,
                                    (intptr_t)behavior.onMouseMove));
    // GPUI's own input listeners. Every node here is an El, so unlike Rust —
    // where each component builds its own base type and only a plain div,
    // h_flex or v_flex carries the family — there is no component that has to
    // report the gap: what a script writes reaches GPUI wherever it writes it.
    // Reachability is still the component's: a key event travels the focus
    // path, so an element that holds no focus handle hears presses and never
    // hears keys.
    if (behavior.keyContext) element->KeyContext(behavior.keyContext);
    if (behavior.onKeyDown)
        element->OnKeyDown(
            Listen(cx, &ScriptView::OnScriptKey, (intptr_t)behavior.onKeyDown));
    if (behavior.onKeyUp)
        element->OnKeyUp(
            Listen(cx, &ScriptView::OnScriptKey, (intptr_t)behavior.onKeyUp));
    if (behavior.hasMouseDown) {
        auto* buttons = ArenaNew<ShellMouseButtonBinding>(cx->a);
        *buttons = behavior.mouseDown;
        element->OnMouseDown(
            Listen(cx, &ScriptView::OnScriptMouseDown, (intptr_t)buttons));
    }
    if (behavior.hasMouseUp) {
        auto* buttons = ArenaNew<ShellMouseButtonBinding>(cx->a);
        *buttons = behavior.mouseUp;
        element->OnMouseUp(
            Listen(cx, &ScriptView::OnScriptMouseUp, (intptr_t)buttons));
    }
    if (behavior.onMouseDownOut)
        element->OnMouseDownOut(Listen(cx, &ScriptView::OnScriptMouseDownOut,
                                       (intptr_t)behavior.onMouseDownOut));
    if (behavior.onScrollWheel)
        element->OnScrollWheel(Listen(cx, &ScriptView::OnScriptScrollWheel,
                                      (intptr_t)behavior.onScrollWheel));
    for (int i = 0; i < behavior.actionCount; i++) {
        // One listener per action rather than one per element. GPUI matches by
        // the action's type and stops at the first listener that claims it,
        // which is why upstream — where every script action shares one Rust
        // type — has to install a single listener and route by id. An action
        // here is its own id, so the dispatch table already does the routing.
        auto* bound = ArenaNew<ShellActionBinding>(cx->a);
        *bound = behavior.actions[i];
        element->OnAction(bound->action, Listen(cx, &ScriptView::OnScriptAction,
                                                (intptr_t)bound));
    }
    element = WireDockCommands(cx, element, behavior);
    bool lazyList =
        node->component.kind == shell::ComponentKind::VVirtualList ||
        node->component.kind == shell::ComponentKind::HVirtualList ||
        node->component.kind == shell::ComponentKind::List ||
        node->component.kind == shell::ComponentKind::UniformList;
    if (lazyList && node->children.len > 0) {
        logf(
            "shell: children are dropped on a %s: its contents are whatever "
            "the item renderer returns\n",
            Str(shell::ComponentName(node->component)));
    }
    if (!childrenConsumed && !lazyList) {
        for (shell::SpecId child : node->children) {
            element->Child(MaterializeNode(cx, runtime, specs, child, error));
        }
    }
    return element;
}

El* ShellMaterialize(Ctx* cx, ShellRuntime* runtime,
                     const RenderSnapshot* snapshot, ShellError* error) {
    if (!cx || !snapshot || !snapshot->Specs()) {
        ShellErrorSet(error,
                      StrL("cannot materialize an empty shell snapshot"));
        return cx ? Div(cx->a) : nullptr;
    }
    if (runtime) runtime->BeginTokenFrame();
    double started = TimeNow();
    El* result = ShellMaterializeSpec(cx, runtime, snapshot->Specs(),
                                      snapshot->Root(), error);
    double elapsed = TimeNow() - started;
    if (runtime) {
        runtime
            ->RecordMaterialize(elapsed <= 0 ? 0 : (uint64_t)(elapsed * 1e9));
    }
    return result;
}

El* ShellMaterializeSpec(Ctx* cx, ShellRuntime* runtime,
                         const shell::SpecArena* specs, shell::SpecId root,
                         ShellError* error) {
    if (!cx || !specs) return cx ? Div(cx->a) : nullptr;
    return MaterializeNode(cx, runtime, specs, root, error);
}

El* ShellTryMaterialize(Ctx* cx, ShellRuntime* runtime,
                        const RenderSnapshot* snapshot, ShellError* error) {
    ShellError held = {};
    ShellError* outer = gMaterializeErrorFrame;
    gMaterializeErrorFrame = &held;
    El* element = ShellMaterialize(cx, runtime, snapshot, error);
    gMaterializeErrorFrame = outer;
    if (!held.IsSet()) return error && error->IsSet() ? nullptr : element;
    if (error && !error->IsSet()) ShellErrorSet(error, held.message);
    ShellErrorClear(&held);
    return nullptr;
}

} // namespace gpui
