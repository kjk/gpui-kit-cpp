#include "ui/carousel.h"
#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {
namespace component {

CarouselState CarouselState::New(int count) {
    CarouselState state;
    state.itemCount = std::max(0, count);
    state.selectedIndex = state.itemCount > 0 ? 0 : -1;
    return state;
}

CarouselState& CarouselState::WithSelectedIndex(int index) {
    if (itemCount <= 0) {
        selectedIndex = -1;
    } else {
        selectedIndex = std::max(0, std::min(index, itemCount - 1));
    }
    return *this;
}
CarouselState& CarouselState::WithAxis(Axis value) {
    axis = value;
    return *this;
}
CarouselState& CarouselState::WithLooping(bool value) {
    looping = value;
    return *this;
}
bool CarouselState::HasPrevious() const {
    return selectedIndex >= 0 && (looping ? itemCount > 1 : selectedIndex > 0);
}
bool CarouselState::HasNext() const {
    return selectedIndex >= 0 &&
           (looping ? itemCount > 1 : selectedIndex + 1 < itemCount);
}
void CarouselState::SetSelectedIndex(int index, Ctx* cx) {
    int before = selectedIndex;
    WithSelectedIndex(index);
    if (cx && before != selectedIndex) Notify(cx);
}
void CarouselState::SetItemCount(int count, Ctx* cx) {
    count = std::max(0, count);
    if (itemCount == count) return;
    itemCount = count;
    if (count == 0) {
        selectedIndex = -1;
    } else if (selectedIndex < 0) {
        selectedIndex = 0;
    } else if (selectedIndex >= count) {
        selectedIndex = count - 1;
    }
    if (cx) Notify(cx);
}
void CarouselState::SetAxis(Axis value, Ctx* cx) {
    if (axis == value) return;
    axis = value;
    if (cx) Notify(cx);
}
void CarouselState::SetLooping(bool value, Ctx* cx) {
    if (looping == value) return;
    looping = value;
    if (cx) Notify(cx);
}
bool CarouselState::SelectIndex(int index, Ctx* cx) {
    if (index < 0 || index >= itemCount || index == selectedIndex) return false;
    selectedIndex = index;
    if (cx) {
        CarouselEvent event = {index};
        if (self.IsValid()) EntityEmit(cx->app, cx->win, self, &event);
        Notify(cx);
    }
    return true;
}
bool CarouselState::SelectPrevious(Ctx* cx) {
    if (!HasPrevious()) return false;
    int next = selectedIndex - 1;
    if (next < 0) next = itemCount - 1;
    return SelectIndex(next, cx);
}
bool CarouselState::SelectNext(Ctx* cx) {
    if (!HasNext()) return false;
    int next = selectedIndex + 1;
    if (next >= itemCount) next = 0;
    return SelectIndex(next, cx);
}
bool CarouselState::SelectFirst(Ctx* cx) {
    return SelectIndex(0, cx);
}
bool CarouselState::SelectLast(Ctx* cx) {
    return SelectIndex(itemCount - 1, cx);
}
void CarouselState::OnAction(CarouselState* state, Ctx* cx,
                             const ActionEvent* event) {
    bool handled = false;
    if (event->action == action::SelectFirst())
        handled = state->SelectFirst(cx);
    if (event->action == action::SelectLast()) handled = state->SelectLast(cx);
    if (event->action == action::SelectLeft() &&
        state->axis == Axis::Horizontal)
        handled = state->SelectPrevious(cx);
    if (event->action == action::SelectRight() &&
        state->axis == Axis::Horizontal)
        handled = state->SelectNext(cx);
    if (event->action == action::SelectUp() && state->axis == Axis::Vertical)
        handled = state->SelectPrevious(cx);
    if (event->action == action::SelectDown() && state->axis == Axis::Vertical)
        handled = state->SelectNext(cx);
    if (!handled) const_cast<ActionEvent*>(event)->propagate = true;
}
void CarouselState::OnPrevious(CarouselState* state, Ctx* cx,
                               const ClickEvent*) {
    state->SelectPrevious(cx);
}
void CarouselState::OnNext(CarouselState* state, Ctx* cx, const ClickEvent*) {
    state->SelectNext(cx);
}
void CarouselState::OnSelect(CarouselState* state, Ctx* cx, const ClickEvent*,
                             intptr_t index) {
    state->SelectIndex((int)index, cx);
}

Entity<CarouselState> CarouselStateNew(App* app, int itemCount) {
    Entity<CarouselState> entity = EntityNewState<CarouselState>(app);
    if (CarouselState* state = entity.Get(app)) {
        *state = CarouselState::New(itemCount);
        state->self = entity;
    }
    return entity;
}

void CarouselInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) return;
    bound = KeymapGeneration();
    const char* context = "Carousel";
    KeyBinding bindings[] = {
        {"left", action::SelectLeft(), context},
        {"right", action::SelectRight(), context},
        {"up", action::SelectUp(), context},
        {"down", action::SelectDown(), context},
        {"home", action::SelectFirst(), context},
        {"end", action::SelectLast(), context},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

template <typename T>
static T* CarouselPart(Ctx* cx) {
    T* value = ArenaNew<T>(cx->a);
    value->a = cx->a;
    value->cx = cx;
    return value;
}
static void CarouselChildren(El* root, const ArenaVec<El*>& children) {
    for (El* child : children) root->Child(child);
}

Carousel* Carousel::New(Ctx* cx, Str valueId,
                        Entity<CarouselState> valueState) {
    Carousel* value = CarouselPart<Carousel>(cx);
    value->id = valueId;
    value->state = valueState;
    return value;
}
Carousel* Carousel::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
Carousel* Carousel::FocusRing(bool enabled) {
    focusRingEnabled = enabled;
    return this;
}
Carousel* Carousel::Child(El* child) {
    children.Append(a, child);
    return this;
}
Carousel* Carousel::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* Carousel::IntoEl() {
    El* root = Div(a)
                   ->Id(id)
                   ->FlexCol()
                   ->Gap(16)
                   ->Role(AccessibilityRole::Region)
                   ->AriaLabel(accessibilityLabel)
                   ->KeyContext(StrL("Carousel"))
                   ->Refine(style, styleSet);
    Listener actionListener = ListenTo(state, &CarouselState::OnAction);
    root->OnAction(action::SelectLeft(), actionListener)
        ->OnAction(action::SelectRight(), actionListener)
        ->OnAction(action::SelectUp(), actionListener)
        ->OnAction(action::SelectDown(), actionListener)
        ->OnAction(action::SelectFirst(), actionListener)
        ->OnAction(action::SelectLast(), actionListener);
    CarouselChildren(root, children);
    return root;
}

CarouselContent* CarouselContent::New(Ctx* cx,
                                      Entity<CarouselState> valueState) {
    CarouselContent* value = CarouselPart<CarouselContent>(cx);
    value->state = valueState;
    return value;
}
CarouselContent* CarouselContent::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselContent* CarouselContent::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
CarouselContent* CarouselContent::TrackStyle(const Style& value,
                                             uint32_t fields) {
    StyleApplyFields(&trackStyle, value, fields);
    trackStyleSet |= fields;
    return this;
}
El* CarouselContent::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    El* track = Div(a)->Flex1()->MinW(0)->MinH(0);
    if (snapshot && snapshot->axis == Axis::Vertical)
        track->FlexCol();
    else
        track->FlexRow();
    track->Refine(trackStyle, trackStyleSet);
    CarouselChildren(track, children);
    return Div(a)
        ->W(kFill)
        ->MinW(0)
        ->ClipX()
        ->ClipY()
        ->Refine(style, styleSet)
        ->Child(track);
}

CarouselItem* CarouselItem::New(Ctx* cx, Str valueId, int valueIndex,
                                Entity<CarouselState> valueState) {
    CarouselItem* value = CarouselPart<CarouselItem>(cx);
    value->id = valueId;
    value->index = valueIndex;
    value->state = valueState;
    return value;
}
CarouselItem* CarouselItem::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
CarouselItem* CarouselItem::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselItem* CarouselItem::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* CarouselItem::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    if (!snapshot || snapshot->selectedIndex != index) return Div(a);
    Str label = accessibilityLabel.s
                    ? accessibilityLabel
                    : fmt("Slide %d of %d", index + 1, snapshot->itemCount);
    El* root = Div(a)
                   ->Id(id)
                   ->Role(AccessibilityRole::Group)
                   ->AriaLabel(label)
                   ->AriaPositionInSet(index + 1)
                   ->AriaSizeOfSet(snapshot->itemCount)
                   ->MinW(0)
                   ->MinH(0)
                   ->Flex1()
                   ->Refine(style, styleSet);
    CarouselChildren(root, children);
    return root;
}

CarouselControl* CarouselControl::WithSize(UiSize value) {
    size = value;
    return this;
}
CarouselControl* CarouselControl::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
CarouselControl* CarouselControl::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselControl* CarouselControl::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* CarouselControl::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    bool vertical = snapshot && snapshot->axis == Axis::Vertical;
    bool disabled =
        !snapshot || (next ? !snapshot->HasNext() : !snapshot->HasPrevious());
    IconName icon =
        vertical ? (next ? IconName::ChevronDown : IconName::ChevronUp)
                 : (next ? IconName::ChevronRight : IconName::ChevronLeft);
    Str label = accessibilityLabel.s
                    ? accessibilityLabel
                    : (next ? StrL("Next slide") : StrL("Previous slide"));
    Button* button = Button::New(cx, next ? StrL("carousel-next")
                                          : StrL("carousel-previous"))
                         ->Outline()
                         ->WithSize(size)
                         ->Rounded(1000.f)
                         ->Disabled(disabled)
                         ->AccessibilityLabel(label)
                         ->Tooltip(label);
    if (len(children) == 0)
        button->Icon(icon);
    else
        for (El* child : children) button->Child(child);
    if (!disabled)
        button->OnClick(next ? ListenTo(state, &CarouselState::OnNext)
                             : ListenTo(state, &CarouselState::OnPrevious));
    return button->IntoEl()->Refine(style, styleSet);
}
CarouselPrevious* CarouselPrevious::New(Ctx* cx, Entity<CarouselState> state) {
    CarouselPrevious* value = CarouselPart<CarouselPrevious>(cx);
    value->state = state;
    value->next = false;
    return value;
}
CarouselNext* CarouselNext::New(Ctx* cx, Entity<CarouselState> state) {
    CarouselNext* value = CarouselPart<CarouselNext>(cx);
    value->state = state;
    value->next = true;
    return value;
}

CarouselPagination* CarouselPagination::New(Ctx* cx) {
    CarouselPagination* value = ArenaNew<CarouselPagination>(cx->a);
    value->a = cx->a;
    return value;
}
CarouselPagination* CarouselPagination::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
CarouselPagination* CarouselPagination::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselPagination* CarouselPagination::Refine(const Style& value,
                                               uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* CarouselPagination::IntoEl() {
    El* root = Div(a)
                   ->Role(AccessibilityRole::Group)
                   ->AriaLabel(accessibilityLabel)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->Gap(8)
                   ->Refine(style, styleSet);
    CarouselChildren(root, children);
    return root;
}

CarouselPaginationItem* CarouselPaginationItem::New(
    Ctx* cx, Str valueId, int valueIndex, Entity<CarouselState> valueState) {
    CarouselPaginationItem* value = CarouselPart<CarouselPaginationItem>(cx);
    value->id = valueId;
    value->index = valueIndex;
    value->state = valueState;
    return value;
}
CarouselPaginationItem* CarouselPaginationItem::WithSize(UiSize value) {
    size = value;
    return this;
}
CarouselPaginationItem* CarouselPaginationItem::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
CarouselPaginationItem* CarouselPaginationItem::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselPaginationItem* CarouselPaginationItem::Refine(const Style& value,
                                                       uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* CarouselPaginationItem::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    bool disabled = !snapshot || index < 0 || index >= snapshot->itemCount;
    bool selected = snapshot && snapshot->selectedIndex == index;
    Str label = accessibilityLabel.s ? accessibilityLabel
                                     : fmt("Go to slide %d", index + 1);
    Button* button = Button::New(cx, id)
                         ->Compact()
                         ->WithSize(size)
                         ->Selected(selected)
                         ->Disabled(disabled)
                         ->AccessibilityLabel(label);
    for (El* child : children) button->Child(child);
    if (!disabled)
        button->OnClick(
            ListenTo(state, &CarouselState::OnSelect, (intptr_t)index));
    return button->IntoEl()->Refine(style, styleSet);
}

} // namespace component
} // namespace gpui
