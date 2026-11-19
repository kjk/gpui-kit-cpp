#include "base/tabs.h"
#include "base/button.h"
#include "base/layout_direction.h"

namespace gpui {

TabStyles& TabStyles::Selected(const StateStyle& style) {
    StateStyleRefine(&selected, style);
    return *this;
}

TabStyles& TabStyles::Disabled(const StateStyle& style) {
    StateStyleRefine(&disabled, style);
    return *this;
}

El* Tabs::New(Ctx* cx, Str id, Axis axis) {
    if (cx->win) cx->win->hasRovingTabs = true;
    AccessibilityOrientation orientation =
        axis == Axis::Vertical ? AccessibilityOrientation::Vertical
                               : AccessibilityOrientation::Horizontal;
    El* list = Div(cx->a)
                   ->Id(id)
                   ->Role(AccessibilityRole::TabList)
                   ->AriaOrientation(orientation);
    list->managesTabFocus = true;
    return list;
}

struct TabFocusEntry {
    int id = 0;
    bool disabled = false;
};

struct TabListNavigation {
    Vec<TabFocusEntry> tabs;
    int tabStop = 0;
    bool vertical = false;

    static void OnKeyDown(TabListNavigation* self, Ctx* cx, const KeyEvent* ev);
};

void TabListNavigation::OnKeyDown(TabListNavigation* self, Ctx* cx,
                                  const KeyEvent* ev) {
    if (!ev || ev->shift || ev->ctrl || ev->alt || ev->platform ||
        ev->function || !cx->win) {
        return;
    }
    int previous = self->vertical ? KeyUp : KeyLeft;
    int next = self->vertical ? KeyDown : KeyRight;
    if (!self->vertical && LayoutIsRtl(cx->app)) std::swap(previous, next);
    if (ev->vk != KeyHome && ev->vk != KeyEnd && ev->vk != previous &&
        ev->vk != next) {
        return;
    }

    Window* win = cx->win;
    int count = len(self->tabs);
    int current = 0;
    while (current < count && self->tabs[current].id != win->focusId) {
        current++;
    }
    if (current == count) return;

    int direction = ev->vk == previous || ev->vk == KeyEnd ? -1 : 1;
    int index = ev->vk == KeyHome ? count - 1 : ev->vk == KeyEnd ? 0 : current;
    for (int i = 0; i < count; ++i) {
        index = (index + direction + count) % count;
        if (self->tabs[index].disabled) continue;
        const int target = self->tabs[index].id;
        // Update the current frame's order as well, so Tab can leave the
        // list immediately even when another frame has yet to draw.
        for (FocusRect& focus : win->focusEls) {
            if (focus.id == self->tabStop) focus.tabStop = false;
            if (focus.id == target) focus.tabStop = true;
        }
        self->tabStop = target;
        FocusHandleFocus(win, FocusHandle{target});
        const_cast<KeyEvent*>(ev)->propagate = false;
        Notify(cx);
        return;
    }
}

struct TabStopCandidates {
    El* focused = nullptr;
    El* selected = nullptr;
    El* first = nullptr;
};

static void CollectTabCandidates(Window* win, El* list,
                                 TabListNavigation* state,
                                 TabStopCandidates* stops, bool reverse) {
    for (El* child = list->first; child; child = child->next) {
        if (child->accessibility.role == AccessibilityRole::TabList) continue;
        if (child->accessibility.role == AccessibilityRole::Tab &&
            child->clickFromPath && child->clickId) {
            int id = child->clickId;
            bool disabled = child->accessibility.disabled;
            child->TrackFocus(FocusHandle{id})
                ->TabStop(false)
                ->FocusOnPress(false);
            child->focusOnMouseDownBubble = !disabled;
            VecAppend(state->tabs, TabFocusEntry{id, disabled});
            if (!disabled) {
                if (win->focusId == id) stops->focused = child;
                if (child->accessibility.selected &&
                    (!stops->selected || reverse)) {
                    stops->selected = child;
                }
                if (!stops->first || reverse) stops->first = child;
            }
        }
        CollectTabCandidates(win, child, state, stops, reverse);
    }
}

void TabsPrepareFocus(Window* win, El* root) {
    if (!root) return;
    if (root->managesTabFocus) {
        Ctx cx = {win->app, win, win->frameArena, win->root};
        auto navigation = KeyedEntity<TabListNavigation>(
            &cx, KeyedKey(root->pathId,
                          HashClickId(StrL("gpui::TabListNavigation"))));
        auto* state = navigation.Get(&cx);
        if (state) {
            VecClear(state->tabs);
            state->vertical = root->accessibility.orientation ==
                              AccessibilityOrientation::Vertical;
            TabStopCandidates stops;
            CollectTabCandidates(win, root, state, &stops,
                                 root->reverseTabOrder);
            if (root->reverseTabOrder) {
                // The themed strip reverses its physical children in RTL
                // so its scroll coordinates stay left-to-right.
                for (int i = 0, j = len(state->tabs) - 1; i < j; ++i, --j) {
                    std::swap(state->tabs[i], state->tabs[j]);
                }
            }
            El* stop = stops.focused    ? stops.focused
                       : stops.selected ? stops.selected
                                        : stops.first;
            state->tabStop = stop ? stop->clickId : 0;
            if (stop) stop->TabStop(true);
            root->OnKeyDown(
                ListenTo(navigation, &TabListNavigation::OnKeyDown));
        }
    }
    for (El* child = root->first; child; child = child->next) {
        TabsPrepareFocus(win, child);
    }
}

El* Tab::New(Ctx* cx, Str id, bool disabled, Listener onClick, bool selected,
             Str accessibilityLabel, int positionInSet, int sizeOfSet,
             const TabStyles* styles, const StateStyle* instance) {
    Arena* a = cx->a;
    // `div().id(ix)`: a tab is named by its place in the strip, which the bar
    // above it scopes.
    // Button's neutral control geometry: a fixed-size tab centers ordinary
    // content while its caller owns the size, spacing and visual treatment.
    El* e = ControlBox(a, id)
                ->Role(AccessibilityRole::Tab)
                ->AriaSelected(selected)
                ->AriaDisabled(disabled)
                // Tabs handle their presses at the control.
                ->StopMouseDown();
    if (styles || instance) {
        StateStyle base = instance ? *instance : StateStyle{};
        const StateStyle* states[2] = {
            selected && styles ? &styles->selected : nullptr,
            disabled && styles ? &styles->disabled : nullptr,
        };
        ElRefine(e, StateStyleResolve(base, states, 2));
    }
    if (accessibilityLabel.s) {
        e->AriaLabel(accessibilityLabel);
    }
    if (positionInSet > 0) {
        e->AriaPositionInSet(positionInSet);
    }
    if (sizeOfSet > 0) {
        e->AriaSizeOfSet(sizeOfSet);
    }
    if (!disabled && onClick.IsValid()) {
        e->OnClick(onClick);
    }
    return e;
}
} // namespace gpui
