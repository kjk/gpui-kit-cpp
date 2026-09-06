#ifndef GPUI_UI_BUTTON_H_
#define GPUI_UI_BUTTON_H_
/* Themed button — crates/ui/src/button/button.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

enum class ButtonRounded : uint8_t {
    None,
    Small,
    Medium,
    Large,
    Size
};

struct ButtonCustomVariant {
    Rgba color = {};
    Rgba foreground = {};
    bool shadow = false;
    Rgba hover = {};
    Rgba active = {};

    static ButtonCustomVariant New(const App* app);
    ButtonCustomVariant Color(Rgba value) const;
    ButtonCustomVariant Foreground(Rgba value) const;
    ButtonCustomVariant Hover(Rgba value) const;
    ButtonCustomVariant Active(Rgba value) const;
    ButtonCustomVariant Shadow(bool value = true) const;
};

enum class ButtonVariant : uint8_t {
    Default,
    Primary,
    Secondary,
    Danger,
    Info,
    Success,
    Warning,
    Ghost,
    Link,
    Text,
    // Rust stores ButtonCustomVariant in this enum case. The POD port keeps
    // that payload beside the discriminator in Button::customVariant.
    Custom
};

inline bool ButtonVariantIsLink(ButtonVariant value) {
    return value == ButtonVariant::Link;
}
inline bool ButtonVariantIsText(ButtonVariant value) {
    return value == ButtonVariant::Text;
}
inline bool ButtonVariantIsGhost(ButtonVariant value) {
    return value == ButtonVariant::Ghost;
}

enum class ButtonIconVariant : uint8_t {
    Icon,
    Spinner,
    Progress
};

struct Icon;
struct Spinner;
struct ProgressCircle;

// button_icon.rs. The three source enum payloads stay as typed pointers; all
// are frame-owned builders and ButtonIcon applies the button's size last.
struct ButtonIcon {
    Ctx* cx = nullptr;
    ButtonIconVariant variant = ButtonIconVariant::Icon;
    IconName iconName = IconName::None;
    component::Icon* icon = nullptr;
    component::Spinner* spinner = nullptr;
    component::ProgressCircle* progress = nullptr;
    IconName loadingIconName = IconName::None;
    component::Icon* loadingIcon = nullptr;
    bool loading = false;
    UiSize size = UiSize::Medium;
    float sizePx = 0;

    static ButtonIcon* New(Ctx* cx, IconName icon);
    static ButtonIcon* New(Ctx* cx, component::Icon* icon);
    static ButtonIcon* New(Ctx* cx, component::Spinner* spinner);
    static ButtonIcon* New(Ctx* cx, component::ProgressCircle* progress);
    ButtonIcon* LoadingIcon(IconName value);
    ButtonIcon* LoadingIcon(component::Icon* value);
    ButtonIcon* Loading(bool value);
    ButtonIcon* WithSize(UiSize value);
    ButtonIcon* Size(float value);
    bool IsSpinner() const { return variant == ButtonIconVariant::Spinner; }
    bool IsProgress() const { return variant == ButtonIconVariant::Progress; }
    El* IntoEl();
};

struct Button;
// The source trait becomes stateless operations over the concrete builders,
// as other Rust extension traits do in this port.
struct ButtonVariants {
    static Button* WithVariant(Button* button, ButtonVariant variant);
    static Button* Primary(Button* button);
    static Button* Secondary(Button* button);
    static Button* Danger(Button* button);
    static Button* Warning(Button* button);
    static Button* Success(Button* button);
    static Button* Info(Button* button);
    static Button* Ghost(Button* button);
    static Button* Link(Button* button);
    static Button* Text(Button* button);
    static Button* Custom(Button* button, const ButtonCustomVariant& variant);
};

struct Button {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str label = {};
    IconName icon = IconName::None;
    // ButtonIcon carries an `Icon`, and an Icon may name its own colour:
    // `.icon(Icon::new(x).text_color(c))`. Its size is not the caller's —
    // `with_size(icon_size)` overwrites that with the button's own.
    bool hasIconColor = false;
    Rgba iconColor = {};
    // An icon after the label rather than before it, which is what a row
    // laid out `flex_row_reverse` comes to — Pagination's Next button.
    IconName iconRight = IconName::None;
    ButtonIcon* buttonIcon = nullptr;
    ButtonVariant variant = ButtonVariant::Default;
    ButtonCustomVariant customVariant = {};
    ButtonRounded rounded = ButtonRounded::Medium;
    float roundedPx = 0;
    UiSize size = UiSize::Medium;
    bool outline = false;
    bool disabled = false;
    bool loading = false;
    bool compact = false;
    bool justifyStart = false;
    bool selected = false;
    bool dropdown = false;
    // hover_group: while any member of the group is hovered, an idle member
    // shows its hover surface at half strength, so a composite such as a
    // split button reads as one control with the hovered part emphasized.
    // Rust names its groups; the group here is the nearest ancestor that
    // asked to be one, which is the DropdownButton's own row.
    bool hoverGroup = false;
    // hover_group_held: keep that idle surface up without a pointer, for as
    // long as the group is held engaged — while a sibling's menu is open.
    bool hoverGroupHeld = false;
    bool focusRing = true;
    int tabIndex = 0;
    bool tabStop = true;
    bool hasCustom = false;
    Rgba custom = {};
    Str tooltip = {};
    // tooltip_placement: Rust's `Option<Placement>`, -1 for None.
    int8_t tooltipPlacement = -1;
    Str accessibilityLabel = {};
    Str accessibilityId = {};
    AccessibilityRole accessibilityRole = AccessibilityRole::None;
    bool hasAccessibilityRole = false;
    bool accessibilityToggled = false;
    bool hasAccessibilityToggled = false;
    El* extra = nullptr;
    ArenaVec<El*> children;
    // Size::Size(px), when the caller gave one instead of a Size.
    float sizePx = 0;
    IconName loadingIcon = IconName::None;
    // ButtonGroup joins its children: the edges each one draws and whether it
    // keeps the group's rounding. Nothing else sets these.
    bool joined = false;
    bool edgeT = true, edgeB = true, edgeL = true, edgeR = true;
    bool cornerTL = true, cornerTR = true, cornerBL = true, cornerBR = true;
    Listener onClick;
    Listener onHover;
    uint32_t clickAction = 0;
    intptr_t clickActionArg = 0;
    // ButtonStyles: what the caller wants a selected or a disabled button to
    // look like, over what the variant computed. resolve_style's order is
    // fixed — the value state first, disabled last.
    StateStyle selectedStyle = {};
    StateStyle disabledStyle = {};

    static Button* New(Ctx* cx, Str id);
    Button* Label(Str s);
    Button* Icon(IconName n);
    Button* Icon(ButtonIcon* value);
    Button* WithVariant(ButtonVariant value);
    Button* IconColor(Rgba c);
    Button* IconRight(IconName n);
    Button* Primary();
    Button* Secondary();
    Button* Danger();
    Button* Warning();
    Button* Success();
    Button* Info();
    Button* Ghost();
    Button* Link();
    Button* Text();
    Button* Outline();
    Button* Rounded(ButtonRounded value);
    Button* Rounded(float px);
    Button* Compact();
    // `.justify_start()`: a full-width button whose content sits at its
    // leading edge rather than in the middle.
    Button* JustifyStart(bool v = true);
    Button* Selected(bool v);
    Button* SelectedStyle(const StateStyle& s);
    Button* DisabledStyle(const StateStyle& s);
    Button* DropdownCaret(bool v = true);
    // Crate-private in Rust: only DropdownButton joins its halves this way.
    Button* HoverGroup(bool v = true);
    Button* HoverGroupHeld(bool v);
    Button* Custom(Rgba c);
    Button* Custom(const ButtonCustomVariant& value);
    Button* Extra(El* e);
    Button* Child(El* e);
    Button* Children(El** values, int count);
    Button* Loading(bool v);
    Button* Disabled(bool v);
    Button* WithSize(UiSize s);
    // Sizable's Size::Size(px): a square button of exactly this many pixels,
    // which is what the Custom size section asks for.
    Button* Size(float px);
    // ButtonIcon::loading_icon: what spins in place of `icon` while loading.
    Button* LoadingIcon(IconName n);
    // FocusableExt::focus_ring: no focus appearance on this control.
    // FocusHandle::tab_index / tab_stop: where this control sits in the
    // Tab order, and whether Tab stops on it at all.
    Button* TabIndex(int v);
    Button* TabStop(bool v);
    Button* FocusRing(bool v);
    Button* Tooltip(Str s);
    // tooltip_placement: prefer a side for the tooltip, falling back when it
    // does not fit. Applies to `Tooltip`; omitting it keeps automatic
    // positioning, and setting it without tooltip content does nothing.
    Button* TooltipPlacement(gpui::Placement placement);
    Button* AccessibilityLabel(Str s);
    Button* AccessibilityId(Str s);
    Button* Role(AccessibilityRole role);
    // Accessibility state only; Selected controls the visual state.
    Button* Toggled(bool v = true);
    Button* OnClick(Listener l);
    Button* OnHover(Listener l);
    Button* OnClickAction(uint32_t action, intptr_t arg = 0);
    El* IntoEl();
};

enum class ToggleVariant : uint8_t {
    Ghost,
    Outline
};

struct Toggle;
struct ToggleGroup;

struct ToggleVariants {
    static Toggle* WithVariant(Toggle* toggle, ToggleVariant variant);
    static Toggle* Ghost(Toggle* toggle);
    static Toggle* Outline(Toggle* toggle);
    static ToggleGroup* WithVariant(ToggleGroup* group, ToggleVariant variant);
    static ToggleGroup* Ghost(ToggleGroup* group);
    static ToggleGroup* Outline(ToggleGroup* group);
};

struct Toggle {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str label = {};
    Str tooltip = {};
    IconName icon = IconName::None;
    ArenaVec<El*> children;
    bool checked = false;
    UiSize size = UiSize::Medium;
    ToggleVariant variant = ToggleVariant::Ghost;
    bool disabled = false;
    bool cornerTL = true, cornerTR = true, cornerBL = true, cornerBR = true;
    bool edgeT = true, edgeB = true, edgeL = true, edgeR = true;
    Listener onClick = {};

    static Toggle* New(Ctx* cx, Str id);
    Toggle* Tooltip(Str value);
    Toggle* Label(Str value);
    Toggle* Icon(IconName value);
    Toggle* Child(El* value);
    Toggle* Checked(bool value);
    Toggle* OnClick(Listener value);
    Toggle* BorderCorners(bool tl, bool tr, bool br, bool bl);
    Toggle* BorderEdges(bool top, bool right, bool bottom, bool left);
    Toggle* WithVariant(ToggleVariant value);
    Toggle* Ghost();
    Toggle* Outline();
    Toggle* Disabled(bool value);
    Toggle* WithSize(UiSize value);
    El* IntoEl();
};

struct ToggleGroupEvent {
    const bool* checked = nullptr;
    int count = 0;
};

struct ToggleGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    ArenaVec<Toggle*> items;
    UiSize size = UiSize::Medium;
    ToggleVariant variant = ToggleVariant::Ghost;
    bool disabled = false;
    bool segmented = false;
    Listener onClick = {};

    static ToggleGroup* New(Ctx* cx, Str id);
    ToggleGroup* Child(Toggle* value);
    ToggleGroup* Children(Toggle** values, int count);
    ToggleGroup* OnClick(Listener value);
    ToggleGroup* Segmented(bool value = true);
    ToggleGroup* WithSize(UiSize value);
    ToggleGroup* WithVariant(ToggleVariant value);
    ToggleGroup* Ghost();
    ToggleGroup* Outline();
    ToggleGroup* Disabled(bool value);
    El* IntoEl();
};

// crates/ui/src/button/dropdown_button.rs: a split button — an action button
// joined to a caret-only button that opens a menu. Two buttons, not one with a
// caret. The halves stay joined except for a ghost split that is not selected,
// which reads better in a toolbar as two separate buttons.
struct DropdownMenu;
struct PopupMenu;

struct DropdownButton {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Button* button = nullptr;
    PopupMenu* menu = nullptr;
    bool selected = false;
    bool disabled = false;
    bool outline = false;
    // The props applied to both halves. Unset means the inner Button keeps
    // whatever it was given, and its value is what the caret takes too.
    // Action-specific props -- compact, loading, tooltip, the click handler --
    // belong to the inner Button and are not mirrored onto the caret.
    bool hasVariant = false;
    ButtonVariant variant = ButtonVariant::Default;
    ButtonCustomVariant customVariant = {};
    bool hasSize = false;
    UiSize size = UiSize::Medium;
    // Anchor::TopRight by default; the story's first one asks for
    // BottomRight, which lines the same edge up.
    bool anchorRight = true;

    static DropdownButton* New(Ctx* cx, Str id);
    DropdownButton* Button_(component::Button* b);
    DropdownButton* Menu(PopupMenu* m);
    DropdownButton* Selected(bool v);
    DropdownButton* Disabled(bool v);
    DropdownButton* Outline();
    DropdownButton* WithVariant(ButtonVariant v);
    DropdownButton* Primary();
    DropdownButton* Secondary();
    DropdownButton* Danger();
    DropdownButton* Warning();
    DropdownButton* Success();
    DropdownButton* Info();
    DropdownButton* Ghost();
    DropdownButton* Link();
    DropdownButton* Text();
    DropdownButton* Custom(const ButtonCustomVariant& value);
    DropdownButton* WithSize(UiSize s);
    El* IntoEl();
};

// crates/ui/src/button/button_group.rs: buttons joined into one control,
// which is also a toggle group. The selection slice belongs to the dispatch
// and is valid for the duration of the listener call, like Rust's &Vec<usize>.
struct ButtonGroupEvent {
    const int* selected = nullptr;
    int count = 0;

    bool Contains(int index) const {
        for (int i = 0; i < count; i++) {
            if (selected[i] == index) {
                return true;
            }
        }
        return false;
    }
};

struct ButtonGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    ArenaVec<Button*> children;
    bool multiple = false;
    bool disabled = false;
    bool vertical = false;
    bool compact = false;
    bool outline = false;
    bool hasVariant = false;
    ButtonVariant variant = ButtonVariant::Default;
    ButtonCustomVariant customVariant = {};
    bool hasSize = false;
    UiSize size = UiSize::Medium;
    // Receives ButtonGroupEvent, the ordered indices selected after the click.
    Listener onClick;

    static ButtonGroup* New(Ctx* cx, Str id);
    ButtonGroup* Child(Button* b);
    ButtonGroup* Children(Button** values, int count);
    ButtonGroup* Multiple(bool v);
    ButtonGroup* Disabled(bool v);
    ButtonGroup* Vertical(bool v = true);
    ButtonGroup* Layout(Axis value);
    ButtonGroup* Compact();
    ButtonGroup* Outline();
    ButtonGroup* WithVariant(ButtonVariant v);
    ButtonGroup* Primary();
    ButtonGroup* Secondary();
    ButtonGroup* Danger();
    ButtonGroup* Warning();
    ButtonGroup* Success();
    ButtonGroup* Info();
    ButtonGroup* Ghost();
    ButtonGroup* Link();
    ButtonGroup* Text();
    ButtonGroup* Custom(const ButtonCustomVariant& value);
    ButtonGroup* WithSize(UiSize s);
    ButtonGroup* OnClick(Listener l);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_BUTTON_H_
