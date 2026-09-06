#include "ui/button.h"
#include "ui/icon.h"
#include "ui/menu.h"
#include "ui/progress.h"
#include "ui/spinner.h"

namespace gpui {

namespace component {

ButtonCustomVariant ButtonCustomVariant::New(const App* app) {
    ButtonCustomVariant out;
    out.color = Rgba8(0, 0, 0, 0);
    out.foreground = ThemeNow(app).foreground;
    out.hover = Rgba8(0, 0, 0, 0);
    out.active = Rgba8(0, 0, 0, 0);
    return out;
}

ButtonCustomVariant ButtonCustomVariant::Color(Rgba value) const {
    ButtonCustomVariant out = *this;
    out.color = value;
    return out;
}

ButtonCustomVariant ButtonCustomVariant::Foreground(Rgba value) const {
    ButtonCustomVariant out = *this;
    out.foreground = value;
    return out;
}

ButtonCustomVariant ButtonCustomVariant::Hover(Rgba value) const {
    ButtonCustomVariant out = *this;
    out.hover = value;
    return out;
}

ButtonCustomVariant ButtonCustomVariant::Active(Rgba value) const {
    ButtonCustomVariant out = *this;
    out.active = value;
    return out;
}

ButtonCustomVariant ButtonCustomVariant::Shadow(bool value) const {
    ButtonCustomVariant out = *this;
    out.shadow = value;
    return out;
}

static ButtonIcon* ButtonIconAlloc(Ctx* cx, ButtonIconVariant variant) {
    ButtonIcon* out = ArenaNew<ButtonIcon>(cx->a);
    out->cx = cx;
    out->variant = variant;
    return out;
}

ButtonIcon* ButtonIcon::New(Ctx* cx, IconName value) {
    ButtonIcon* out = ButtonIconAlloc(cx, ButtonIconVariant::Icon);
    out->iconName = value;
    return out;
}

ButtonIcon* ButtonIcon::New(Ctx* cx, component::Icon* value) {
    ButtonIcon* out = ButtonIconAlloc(cx, ButtonIconVariant::Icon);
    out->icon = value;
    return out;
}

ButtonIcon* ButtonIcon::New(Ctx* cx, component::Spinner* value) {
    ButtonIcon* out = ButtonIconAlloc(cx, ButtonIconVariant::Spinner);
    out->spinner = value;
    return out;
}

ButtonIcon* ButtonIcon::New(Ctx* cx, component::ProgressCircle* value) {
    ButtonIcon* out = ButtonIconAlloc(cx, ButtonIconVariant::Progress);
    out->progress = value;
    return out;
}

ButtonIcon* ButtonIcon::LoadingIcon(IconName value) {
    loadingIconName = value;
    loadingIcon = nullptr;
    return this;
}

ButtonIcon* ButtonIcon::LoadingIcon(component::Icon* value) {
    loadingIcon = value;
    loadingIconName = IconName::None;
    return this;
}

ButtonIcon* ButtonIcon::Loading(bool value) {
    loading = value;
    return this;
}

ButtonIcon* ButtonIcon::WithSize(UiSize value) {
    size = value;
    sizePx = 0;
    return this;
}

ButtonIcon* ButtonIcon::Size(float value) {
    sizePx = value;
    return this;
}

static float ButtonIconSizePx(UiSize size, float exact) {
    if (exact > 0) return exact;
    if (size == UiSize::XSmall) return 12.f;
    if (size == UiSize::Small) return 14.f;
    if (size == UiSize::Large) return 24.f;
    return 16.f;
}

El* ButtonIcon::IntoEl() {
    float px = ButtonIconSizePx(size, sizePx);
    if (loading && variant == ButtonIconVariant::Icon) {
        Spinner* wait = Spinner::New(cx)->Size(px);
        if (loadingIcon) {
            if (loadingIcon->name != IconName::None)
                wait->Icon(loadingIcon->name);
            if (loadingIcon->hasColor) wait->Color(loadingIcon->color);
        } else if (loadingIconName != IconName::None) {
            wait->Icon(loadingIconName);
        }
        return wait->IntoEl();
    }
    if (variant == ButtonIconVariant::Spinner) {
        return spinner ? spinner->Size(px)->IntoEl()
                       : Spinner::New(cx)->Size(px)->IntoEl();
    }
    if (variant == ButtonIconVariant::Progress) {
        return progress ? progress->Size(px)->IntoEl() : nullptr;
    }
    if (icon) return icon->Size(px)->IntoEl();
    return IconEl(cx->a, iconName, px);
}

Button* ButtonVariants::WithVariant(Button* button, ButtonVariant variant) {
    return button ? button->WithVariant(variant) : nullptr;
}
Button* ButtonVariants::Primary(Button* button) {
    return button ? button->Primary() : nullptr;
}
Button* ButtonVariants::Secondary(Button* button) {
    return button ? button->Secondary() : nullptr;
}
Button* ButtonVariants::Danger(Button* button) {
    return button ? button->Danger() : nullptr;
}
Button* ButtonVariants::Warning(Button* button) {
    return button ? button->Warning() : nullptr;
}
Button* ButtonVariants::Success(Button* button) {
    return button ? button->Success() : nullptr;
}
Button* ButtonVariants::Info(Button* button) {
    return button ? button->Info() : nullptr;
}
Button* ButtonVariants::Ghost(Button* button) {
    return button ? button->Ghost() : nullptr;
}
Button* ButtonVariants::Link(Button* button) {
    return button ? button->Link() : nullptr;
}
Button* ButtonVariants::Text(Button* button) {
    return button ? button->Text() : nullptr;
}
Button* ButtonVariants::Custom(Button* button,
                               const ButtonCustomVariant& variant) {
    return button ? button->Custom(variant) : nullptr;
}

Button* Button::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Button* b = ArenaNew<Button>(a);
    b->a = a;
    b->cx = cx;
    b->id = id;
    return b;
}

Button* Button::Label(Str s) {
    label = s;
    return this;
}
Button* Button::Icon(IconName n) {
    icon = n;
    buttonIcon = nullptr;
    return this;
}
Button* Button::Icon(ButtonIcon* value) {
    buttonIcon = value;
    icon = IconName::None;
    return this;
}
Button* Button::WithVariant(ButtonVariant value) {
    variant = value;
    if (value != ButtonVariant::Custom) hasCustom = false;
    return this;
}

Button* Button::IconColor(Rgba c) {
    hasIconColor = true;
    iconColor = c;
    return this;
}
Button* Button::IconRight(IconName n) {
    iconRight = n;
    return this;
}
Button* Button::Primary() {
    return WithVariant(ButtonVariant::Primary);
}
Button* Button::Secondary() {
    return WithVariant(ButtonVariant::Secondary);
}
Button* Button::Danger() {
    return WithVariant(ButtonVariant::Danger);
}
Button* Button::Warning() {
    return WithVariant(ButtonVariant::Warning);
}
Button* Button::Success() {
    return WithVariant(ButtonVariant::Success);
}
Button* Button::Info() {
    return WithVariant(ButtonVariant::Info);
}
Button* Button::Ghost() {
    return WithVariant(ButtonVariant::Ghost);
}
Button* Button::Link() {
    return WithVariant(ButtonVariant::Link);
}
Button* Button::Text() {
    return WithVariant(ButtonVariant::Text);
}
Button* Button::Outline() {
    outline = true;
    return this;
}
Button* Button::Rounded(ButtonRounded value) {
    rounded = value;
    if (value != ButtonRounded::Size) roundedPx = 0;
    return this;
}
Button* Button::Rounded(float px) {
    rounded = ButtonRounded::Size;
    roundedPx = px;
    return this;
}
Button* Button::JustifyStart(bool v) {
    justifyStart = v;
    return this;
}
Button* Button::Compact() {
    compact = true;
    return this;
}
Button* Button::Selected(bool v) {
    selected = v;
    return this;
}

Button* Button::SelectedStyle(const StateStyle& s) {
    selectedStyle = s;
    return this;
}

Button* Button::DisabledStyle(const StateStyle& s) {
    disabledStyle = s;
    return this;
}
Button* Button::DropdownCaret(bool v) {
    dropdown = v;
    return this;
}
Button* Button::Custom(Rgba c) {
    return Custom(ButtonCustomVariant::New(cx->app)
                      .Color(c)
                      .Foreground(c)
                      .Hover(RgbaOpacity(c, 0.1f))
                      .Active(RgbaOpacity(c, 0.2f)));
}
Button* Button::Custom(const ButtonCustomVariant& value) {
    customVariant = value;
    custom = value.color;
    hasCustom = true;
    variant = ButtonVariant::Custom;
    return this;
}
Button* Button::Extra(El* e) {
    extra = e;
    if (e) children.Append(a, e);
    return this;
}
Button* Button::Child(El* e) {
    if (e) {
        if (!extra) extra = e;
        children.Append(a, e);
    }
    return this;
}
Button* Button::Children(El** values, int count) {
    for (int i = 0; values && i < count; i++) Child(values[i]);
    return this;
}
Button* Button::Loading(bool v) {
    loading = v;
    return this;
}
Button* Button::Disabled(bool v) {
    disabled = v;
    return this;
}
Button* Button::WithSize(UiSize s) {
    size = s;
    return this;
}
Button* Button::Size(float px) {
    sizePx = px;
    return this;
}
Button* Button::LoadingIcon(IconName n) {
    loadingIcon = n;
    return this;
}
Button* Button::TabIndex(int v) {
    tabIndex = v;
    return this;
}
Button* Button::TabStop(bool v) {
    tabStop = v;
    return this;
}
Button* Button::FocusRing(bool v) {
    focusRing = v;
    return this;
}
Button* Button::Tooltip(Str s) {
    tooltip = s;
    return this;
}
Button* Button::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}
Button* Button::AccessibilityId(Str s) {
    accessibilityId = s;
    return this;
}
Button* Button::Role(AccessibilityRole value) {
    accessibilityRole = value;
    hasAccessibilityRole = true;
    return this;
}
Button* Button::Toggled(bool v) {
    accessibilityToggled = v;
    hasAccessibilityToggled = true;
    return this;
}
Button* Button::HoverGroup(bool v) {
    hoverGroup = v;
    return this;
}
Button* Button::HoverGroupHeld(bool v) {
    hoverGroupHeld = v;
    return this;
}
Button* Button::OnClick(Listener l) {
    onClick = l;
    return this;
}
Button* Button::OnHover(Listener l) {
    onHover = l;
    return this;
}
Button* Button::OnClickAction(uint32_t action, intptr_t arg) {
    clickAction = action;
    clickActionArg = arg;
    return this;
}

El* Button::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    const Rgba clear = Rgba8(0, 0, 0, 0);
    const Rgba transparentWhite = Rgba8(255, 255, 255, 0);
    const bool dark = ThemeGet(cx->app) == ThemeMode::Dark;
    // ButtonVariant::bg_color / hovered / active / border_color / text_color,
    // in button.rs. Every fill a variant paints is a *token* there —
    // `tokens.button_primary_hover` and not a mix computed at the call — so
    // that a theme file naming `button.primary.hover.background` is honoured.
    // The mixes this used to do are still the numbers, but they live in
    // `ThemeFillDerived` as the fallbacks schema.rs spells, which is the one
    // place they belong.
    //
    // bg, hover and press are Backgrounds, not colours: a theme may spell any
    // of them as a gradient, and a button is where most of them land.
    Background bg = th.tokens.button, hover = th.tokens.buttonHover,
               press = th.tokens.buttonActive;
    Rgba fg = th.buttonFg, bd = th.inputBorder;
    // hovered().fg / active().fg only ever differ from normal().fg for Link
    // and Text, the two that paint no fill: what the pointer moves on them is
    // the ink. Everything else answers the pointer with its background.
    Rgba fgHover = {};
    bool hasFgHover = false;
    // The status variants keep their accent around because `outline` re-derives
    // its three fills from it — `outline_background` is written against
    // `tokens.danger` and friends, not against the `button_danger` family.
    Background accent = {};
    bool hasAccent = false;
    switch (variant) {
        case ButtonVariant::Secondary:
            bg = th.tokens.buttonSecondary;
            fg = th.buttonSecondaryFg;
            hover = th.tokens.buttonSecondaryHover;
            press = th.tokens.buttonSecondaryActive;
            bd = th.border;
            break;
        case ButtonVariant::Primary:
            bg = th.tokens.buttonPrimary;
            fg = th.buttonPrimaryFg;
            hover = th.tokens.buttonPrimaryHover;
            press = th.tokens.buttonPrimaryActive;
            bd = th.primary;
            break;
        case ButtonVariant::Danger:
            accent = th.tokens.danger;
            hasAccent = true;
            bg = th.tokens.buttonDanger;
            fg = th.buttonDangerFg;
            hover = th.tokens.buttonDangerHover;
            press = th.tokens.buttonDangerActive;
            bd = th.buttonDanger;
            break;
        case ButtonVariant::Success:
            accent = th.tokens.success;
            hasAccent = true;
            bg = th.tokens.buttonSuccess;
            fg = th.buttonSuccessFg;
            hover = th.tokens.buttonSuccessHover;
            press = th.tokens.buttonSuccessActive;
            bd = th.buttonSuccess;
            break;
        case ButtonVariant::Warning:
            accent = th.tokens.warning;
            hasAccent = true;
            bg = th.tokens.buttonWarning;
            fg = th.buttonWarningFg;
            hover = th.tokens.buttonWarningHover;
            press = th.tokens.buttonWarningActive;
            bd = th.buttonWarning;
            break;
        case ButtonVariant::Info:
            accent = th.tokens.info;
            hasAccent = true;
            bg = th.tokens.buttonInfo;
            fg = th.buttonInfoFg;
            hover = th.tokens.buttonInfoHover;
            press = th.tokens.buttonInfoActive;
            bd = th.buttonInfo;
            break;
        case ButtonVariant::Ghost:
            // The one family with no token of its own: button.rs computes it
            // from `secondary`, lightened in dark and darkened in light, and
            // at 0.8 alpha in both. Twice as far for the pressed state.
            bg = clear;
            fg = th.secondaryFg;
            hover = RgbaOpacity(dark ? RgbaLighten(th.secondary, 0.1f)
                                     : RgbaDarken(th.secondary, 0.1f),
                                0.8f);
            press = RgbaOpacity(dark ? RgbaLighten(th.secondary, 0.2f)
                                     : RgbaDarken(th.secondary, 0.2f),
                                0.8f);
            bd = clear;
            break;
        case ButtonVariant::Text:
            // Link and Text paint no fill in any state — what changes under
            // the pointer is the text colour, which `HoverFg` carries.
            bg = clear;
            fg = RgbaOpacity(th.foreground, 0.9f);
            fgHover = th.foreground;
            hasFgHover = true;
            hover = clear;
            press = clear;
            bd = clear;
            break;
        case ButtonVariant::Link:
            bg = clear;
            fg = th.link;
            fgHover = th.linkHover;
            hasFgHover = true;
            hover = clear;
            press = clear;
            bd = clear;
            break;
        default:
            break;
    }
    if (hasCustom) {
        fg = customVariant.foreground;
        bd = outline ? RgbaMixOklab(customVariant.color, transparentWhite, 0.4f)
                     : customVariant.color;
        bg = RgbaMixOklab(customVariant.color, clear, 0.2f);
        hover = customVariant.hover;
        press = customVariant.active;
    }
    if (outline && !hasCustom) {
        // outline_background(state): the semantic token at 0.1 / 0.2 / 0.4,
        // and Default on the input colour mixed toward transparent instead.
        if (hasAccent) {
            bg = BackgroundOpacity(accent, 0.1f);
            bd = RgbaOpacity(accent.color, 0.6f);
            hover = BackgroundOpacity(accent, 0.2f);
            press = BackgroundOpacity(accent, 0.4f);
        } else if (variant == ButtonVariant::Primary) {
            bg = BackgroundOpacity(th.tokens.primary, 0.1f);
            fg = th.primary;
            hover = BackgroundOpacity(th.tokens.primaryHover, 0.2f);
            press = BackgroundOpacity(th.tokens.primaryActive, 0.4f);
        } else if (variant == ButtonVariant::Secondary) {
            bg = BackgroundOpacity(th.tokens.secondary, 0.1f);
            fg = th.secondaryFg;
            hover = BackgroundOpacity(th.tokens.secondaryHover, 0.2f);
            press = BackgroundOpacity(th.tokens.secondaryActive, 0.4f);
        } else if (variant == ButtonVariant::Ghost ||
                   variant == ButtonVariant::Link ||
                   variant == ButtonVariant::Text) {
            // Transparent in every state, outlined or not.
        } else {
            bg = th.inputBg;
            hover = RgbaMixOklab(th.inputBorder, clear, 0.5f);
            press = RgbaMixOklab(th.inputBorder, clear, 0.7f);
        }
    }
    if (selected) {
        // ButtonVariant::selected: Ghost uses secondary_active, while Link
        // and Text remain transparent.
        switch (variant) {
            case ButtonVariant::Ghost:
                bg = th.tokens.secondaryActive;
                break;
            case ButtonVariant::Link:
            case ButtonVariant::Text:
                bg = clear;
                break;
            default:
                bg = press;
                break;
        }
    }
    if (disabled) {
        // ButtonVariant::disabled. The foreground is muted at half — which is
        // the whole of what the port had — and the background and border go
        // with it: the variant's own fill at 0.15 (Secondary's at 1.5, which
        // is Rust's number and saturates), Default on the input surface at
        // half, and Ghost, Link and Text disabled to nothing at all.
        // Outlined, both are the normal outline pair at half.
        fg = RgbaOpacity(th.mutedFg, 0.5f);
        if (outline) {
            bg = BackgroundOpacity(bg, 0.5f);
            bd = RgbaOpacity(bd, 0.5f);
        } else if (hasCustom) {
            bg = RgbaOpacity(custom, 0.15f);
            bd = RgbaOpacity(custom, 0.15f);
        } else if (hasAccent) {
            bg = BackgroundOpacity(accent, 0.15f);
            bd = RgbaOpacity(accent.color, 0.15f);
        } else {
            switch (variant) {
                case ButtonVariant::Ghost:
                case ButtonVariant::Link:
                case ButtonVariant::Text:
                    bg = Rgba8(0, 0, 0, 0);
                    bd = Rgba8(0, 0, 0, 0);
                    break;
                case ButtonVariant::Primary:
                    bg = BackgroundOpacity(th.tokens.primary, 0.15f);
                    bd = RgbaOpacity(th.primary, 0.15f);
                    break;
                case ButtonVariant::Secondary:
                    bg = BackgroundOpacity(th.tokens.secondary, 1.f);
                    bd = th.secondary;
                    break;
                default:
                    bg = BackgroundOpacity(th.inputBg, 0.5f);
                    bd = RgbaOpacity(th.inputBorder, 0.5f);
                    break;
            }
        }
    }
    // state_style.rs resolve_style: whatever the caller asked for goes on last
    // and only for the fields it named — the value state, then disabled.
    const StateStyle* active[2] = {selected ? &selectedStyle : nullptr,
                                   disabled ? &disabledStyle : nullptr};
    StateStyle resolved = StateStyleResolve(StateStyle{}, active, 2);
    float borderW = 1;
    if (resolved.Has(StateFieldBg)) {
        bg = resolved.style.bg;
    }
    if (resolved.Has(StateFieldFg)) {
        fg = resolved.style.color;
    }
    if (resolved.Has(StateFieldBorder)) {
        borderW = resolved.style.border;
        bd = resolved.style.borderColor;
    }
    if (resolved.Has(StateFieldHoverBg)) {
        hover = resolved.style.hoverBg;
    }
    if (resolved.Has(StateFieldActiveBg)) {
        press = resolved.style.activeBg;
    }
    // crates/ui/src/button: h_5/px_1, h_6/px_2, h_8/px_2p5, h_8/px_3, with a
    // tighter px when compact. Buttons do not use the generic control height.
    float h = 32.f;
    float padX = compact ? 8.f : 10.f;
    if (size == UiSize::XSmall) {
        h = 20.f;
        padX = 4.f;
    } else if (size == UiSize::Small) {
        h = 24.f;
        padX = compact ? 6.f : 8.f;
    } else if (size == UiSize::Large) {
        padX = compact ? 8.f : 12.f;
    }
    // button.rs: `label.is_none() && children.is_empty()` is an Icon Button —
    // a square of the size's own side and no padding at all, rather than the
    // h/px pair a labelled button takes. `.icon()` is not a child in Rust, so
    // an icon alone still lands here; `extra` is what a `.child()` is here.
    bool iconOnly = !label.s && children.len == 0;
    if (iconOnly) {
        h = size == UiSize::XSmall ? 20.f : size == UiSize::Small ? 24.f : 32.f;
        padX = 0;
    }
    if (variant == ButtonVariant::Text || variant == ButtonVariant::Link) {
        padX = 0;
        h = 0;
        iconOnly = false;
    }
    // Size::Size(px): a square of that size, with no room for a label.
    if (sizePx > 0) {
        h = sizePx;
        padX = 0;
    }
    // button.rs: gap_1 at the two small sizes, gap_2 above them.
    float gap = (size == UiSize::XSmall || size == UiSize::Small) ? 4.f : 8.f;
    // `icon_size`: the button's own size, and three quarters of it when the
    // caller gave a pixel size. Icon::with_size then resolves it —
    // size_3 / size_3p5 / size_4 / size_6.
    float iconPx = size == UiSize::XSmall  ? 12.f
                   : size == UiSize::Small ? 14.f
                   : size == UiSize::Large ? 24.f
                                           : 16.f;
    if (sizePx > 0) {
        iconPx = sizePx * 0.75f;
    }
    // The unstyled Button takes the interaction gate here. Loading is inert
    // without taking disabled styling, so its visual state stays separate
    // while Base still removes its focus and activation behavior.
    bool interactive = !(disabled || loading);
    float rounding = th.radius;
    if (rounded == ButtonRounded::None)
        rounding = 0;
    else if (rounded == ButtonRounded::Small)
        rounding = th.radius * 0.5f;
    else if (rounded == ButtonRounded::Large)
        rounding = th.radius * 2.f;
    else if (rounded == ButtonRounded::Size)
        rounding = roundedPx;
    if (resolved.Has(StateFieldRadius)) rounding = resolved.style.radius;
    AccessibilityRole role = hasAccessibilityRole ? accessibilityRole
                             : variant == ButtonVariant::Link
                                 ? AccessibilityRole::Link
                                 : AccessibilityRole::Button;
    El* e = gpui::Button::New(cx, id, !interactive)
                ->Role(role)
                ->AriaDisabled(!interactive)
                ->TabIndex(tabIndex)
                ->TabStop(tabStop)
                ->FocusRing(focusRing)
                ->H(h > 0 ? h : kAuto)
                ->PadX(padX)
                ->ItemsCenter()
                ->JustifyCenter()
                ->Gap(gap)
                ->Corners(cornerTL ? rounding : 0.f, cornerTR ? rounding : 0.f,
                          cornerBR ? rounding : 0.f, cornerBL ? rounding : 0.f);
    if (accessibilityId.s) {
        e->AccessibilityId(accessibilityId);
    }
    if (hasAccessibilityToggled) {
        e->AriaToggled(accessibilityToggled ? AccessibilityToggled::True
                                            : AccessibilityToggled::False);
    }
    if (accessibilityLabel.s) {
        e->AriaLabel(accessibilityLabel);
    } else if (label.s) {
        e->AriaLabel(label);
    } else if (tooltip.s) {
        // Icon-only buttons use their tooltip as the accessible name, the
        // same fallback `Button::accessibility_label` takes upstream.
        e->AriaLabel(tooltip);
    }
    if (justifyStart) {
        e->JustifyStart();
    }
    if (sizePx > 0) {
        e->W(sizePx);
    } else if (iconOnly) {
        e->W(h);
    } else if (compact && h > 0) {
        // button.rs: compact is `min_w_5` / `min_w_6` / `min_w_8` beside the
        // tighter px, so a labelled compact button is never narrower than it
        // is tall — which is what keeps a pagination page number square.
        e->MinW(h);
    }
    if (bd.a) {
        if (joined) {
            // A joined child draws only the edges the group left it, and
            // keeps only the corner radii the group assigned it.
            if (edgeT) {
                e->BorderT(borderW, bd);
            }
            if (edgeB) {
                e->BorderB(borderW, bd);
            }
            if (edgeL) {
                e->BorderL(borderW, bd);
            }
            if (edgeR) {
                e->BorderR(borderW, bd);
            }
        } else {
            e->Border(borderW, bd);
        }
    }
    if (bg.gradient || bg.color.a) {
        e->Bg(bg);
    }
    if (!disabled && th.shadow && variant == ButtonVariant::Custom &&
        customVariant.shadow) {
        // Styled::shadow_xs at the pinned GPUI checkin: Tailwind's
        // 0 1px 2px 0 black/5%. This was retained but could not be painted
        // before El acquired the source BoxShadow value above.
        BoxShadow shadow = {0, 1, 2, 0, Rgba8(0, 0, 0, 13), false};
        e->Shadows(&shadow, 1);
    }
    if (interactive && onClick.IsValid()) {
        e->OnClick(onClick);
    }
    if (interactive && clickAction) {
        e->OnClickAction(clickAction, clickActionArg);
    }
    // The ink the label and the icons inherit, so the pointer can move it:
    // `text_color(normal_style.fg)` on the root, with `hover` and `active`
    // refining it. Only the caret names a colour of its own, since Rust
    // builds it from `normal_style.fg` and it does not follow the state.
    e->Fg(fg);
    // button.rs hangs the hover and the active style off
    // `when(!disabled && !selected)` and then `when(interactive)`: a selected
    // button shows its selected fill and nothing else, and a loading one
    // keeps its normal colours because it is not waiting for another click.
    if (interactive && !selected) {
        e->HoverBg(hover);
        e->ActiveBg(press);
        if (hasFgHover) {
            e->HoverFg(fgHover);
        }
        // Registered beside hover and active, so it yields to selected,
        // disabled and loading the way Rust's `when(!disabled && !selected)`
        // and `when(interactive)` do. The paint asks the group only where the
        // element's own hover and active have nothing to say, which is the
        // order the two refinements are applied in.
        if (hoverGroup) {
            Background idle = BackgroundOpacity(hover, 0.5f);
            if (hoverGroupHeld) {
                e->Bg(idle);
            }
            e->GroupHoverBg(idle);
        }
    }
    if (interactive && onHover.IsValid()) e->OnHover(onHover);
    // button.rs: `cursor_default`, and the hand only for the two variants that
    // look like a link rather than a button. A ghost button is still a button,
    // so it keeps the arrow.
    if (interactive &&
        (variant == ButtonVariant::Link || variant == ButtonVariant::Text)) {
        e->Cursor(CursorKind::Pointer);
    }
    if (tooltip.s) {
        e->Tip(tooltip);
    }
    // button.rs fades the whole button while it loads rather than dimming its
    // colours one by one, and says why: Ghost, Link and Text have no
    // background for an alpha to show up on.
    if (loading && !disabled) {
        e->Opacity(0.8f);
    }
    // button_icon.rs only substitutes a spinner when an icon exists. Loading
    // a text-only button dims and gates it without inventing new content.
    if (buttonIcon) {
        buttonIcon->Loading(loading)->Size(iconPx);
        if (loadingIcon != IconName::None) buttonIcon->LoadingIcon(loadingIcon);
        if (El* ic = buttonIcon->IntoEl()) e->Child(ic);
    } else if (icon != IconName::None) {
        El* ic = nullptr;
        if (loading) {
            Spinner* wait = Spinner::New(cx)->Size(iconPx);
            if (loadingIcon != IconName::None) wait->Icon(loadingIcon);
            ic = wait->IntoEl();
        } else {
            ic = IconEl(a, icon, iconPx);
        }
        if (hasIconColor && !loading) {
            ic->Fg(iconColor);
        }
        e->Child(ic);
    }
    if (label.s) {
        // button_text_size: text_xs, text_sm, then text_base — a step larger
        // than the generic control font.
        float fontPx = size == UiSize::XSmall  ? 12.f
                       : size == UiSize::Small ? 14.f
                                               : 16.f;
        // `line_height(relative(1.))` on the base button: with the inherited
        // line height the text box is taller than the glyphs, so the padding
        // no longer decides the control's height and a button cannot be sized
        // precisely. A label is one line and is cut rather than wrapped —
        // `min_w_0`, `whitespace_nowrap`, `truncate` — since a button that
        // grew a second line would push everything around it.
        El* text = TextEl(a, label)->Font(fontPx)->LineHeight(1.f)->Truncate();
        // ButtonVariant::underline: only the link looks like a link.
        if (variant == ButtonVariant::Link) {
            text->Underline();
        }
        e->Child(text);
    }
    for (El* child : children) e->Child(child);
    if (iconRight != IconName::None) {
        e->Child(IconEl(a, iconRight, iconPx));
    }
    if (dropdown) {
        // dropdown_caret adds the caret and nothing else: a DropdownButton's
        // seam is the border between its two buttons, not a rule inside one.
        // Caret::new(size): xs and sm keep their own icon size, everything
        // else — Large included — takes the medium one, at three quarters of
        // the button's own ink.
        float caretPx = size == UiSize::XSmall  ? 12.f
                        : size == UiSize::Small ? 14.f
                                                : 16.f;
        e->Child(IconEl(a, IconName::ChevronDown, caretPx)
                     ->Fg(RgbaOpacity(fg, 0.75f)));
    }
    return e;
}

Toggle* Toggle::New(Ctx* cx, Str id) {
    Toggle* out = ArenaNew<Toggle>(cx->a);
    out->a = cx->a;
    out->cx = cx;
    out->id = id;
    return out;
}

Toggle* Toggle::Tooltip(Str value) {
    tooltip = value;
    return this;
}
Toggle* Toggle::Label(Str value) {
    label = value;
    return this;
}
Toggle* Toggle::Icon(IconName value) {
    icon = value;
    return this;
}
Toggle* Toggle::Child(El* value) {
    if (value) children.Append(a, value);
    return this;
}
Toggle* Toggle::Checked(bool value) {
    checked = value;
    return this;
}
Toggle* Toggle::OnClick(Listener value) {
    onClick = value;
    return this;
}
Toggle* Toggle::BorderCorners(bool tl, bool tr, bool br, bool bl) {
    cornerTL = tl;
    cornerTR = tr;
    cornerBR = br;
    cornerBL = bl;
    return this;
}
Toggle* Toggle::BorderEdges(bool top, bool right, bool bottom, bool left) {
    edgeT = top;
    edgeR = right;
    edgeB = bottom;
    edgeL = left;
    return this;
}
Toggle* Toggle::WithVariant(ToggleVariant value) {
    variant = value;
    return this;
}
Toggle* Toggle::Ghost() {
    return WithVariant(ToggleVariant::Ghost);
}
Toggle* Toggle::Outline() {
    return WithVariant(ToggleVariant::Outline);
}
Toggle* Toggle::Disabled(bool value) {
    disabled = value;
    return this;
}
Toggle* Toggle::WithSize(UiSize value) {
    size = value;
    return this;
}

El* Toggle::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    StateStyle pressed;
    pressed.Bg(th.tokens.accent).Fg(th.accentFg);
    gpui::ToggleStyles styles;
    styles.Pressed(pressed);
    StateStyle instance;
    instance.Fg(th.foreground);
    if (variant == ToggleVariant::Outline) {
        if (edgeL) instance.BorderL(1, th.border);
        if (edgeR) instance.BorderR(1, th.border);
        if (edgeT) instance.BorderT(1, th.border);
        if (edgeB) instance.BorderB(1, th.border);
        instance.Bg(th.tokens.background);
    }
    El* root = gpui::Toggle::New(cx, id, checked, disabled, onClick, &styles,
                                 &instance)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->JustifyCenter();

    float h = 32.f;
    float pad = 8.f;
    float font = 16.f;
    if (size == UiSize::XSmall) {
        h = 20.f;
        pad = 2.f;
        font = 12.f;
    } else if (size == UiSize::Small) {
        h = 24.f;
        pad = 4.f;
        font = 14.f;
    } else if (size == UiSize::Large) {
        h = 36.f;
        pad = 12.f;
        font = 18.f;
    }
    root->MinW(h)->H(h)->PadX(pad)->Corners(
        cornerTL ? th.radius : 0.f, cornerTR ? th.radius : 0.f,
        cornerBR ? th.radius : 0.f, cornerBL ? th.radius : 0.f);
    if (!disabled && !checked) {
        root->HoverBg(th.tokens.accent)->HoverFg(th.accentFg);
    }
    if (tooltip.s) root->Tip(tooltip)->AriaLabel(tooltip);
    if (icon != IconName::None) root->Child(IconEl(a, icon, 16.f));
    if (label.s) root->Child(TextEl(a, label)->Font(font));
    for (El* child : children) root->Child(child);
    return root;
}

Toggle* ToggleVariants::WithVariant(Toggle* toggle, ToggleVariant variant) {
    return toggle ? toggle->WithVariant(variant) : nullptr;
}
Toggle* ToggleVariants::Ghost(Toggle* toggle) {
    return toggle ? toggle->Ghost() : nullptr;
}
Toggle* ToggleVariants::Outline(Toggle* toggle) {
    return toggle ? toggle->Outline() : nullptr;
}

ToggleGroup* ToggleGroup::New(Ctx* cx, Str id) {
    ToggleGroup* out = ArenaNew<ToggleGroup>(cx->a);
    out->a = cx->a;
    out->cx = cx;
    out->id = id;
    return out;
}
ToggleGroup* ToggleGroup::Child(Toggle* value) {
    if (value) items.Append(a, value);
    return this;
}
ToggleGroup* ToggleGroup::Children(Toggle** values, int count) {
    for (int i = 0; values && i < count; i++) Child(values[i]);
    return this;
}
ToggleGroup* ToggleGroup::OnClick(Listener value) {
    onClick = value;
    return this;
}
ToggleGroup* ToggleGroup::Segmented(bool value) {
    segmented = value;
    return this;
}
ToggleGroup* ToggleGroup::WithSize(UiSize value) {
    size = value;
    return this;
}
ToggleGroup* ToggleGroup::WithVariant(ToggleVariant value) {
    variant = value;
    return this;
}
ToggleGroup* ToggleGroup::Ghost() {
    return WithVariant(ToggleVariant::Ghost);
}
ToggleGroup* ToggleGroup::Outline() {
    return WithVariant(ToggleVariant::Outline);
}
ToggleGroup* ToggleGroup::Disabled(bool value) {
    disabled = value;
    return this;
}

struct ToggleGroupState {
    Vec<bool> checked;
    Listener onClick = {};

    ~ToggleGroupState() { VecReset(checked); }

    static void OnChildClick(ToggleGroupState* self, Ctx* cx, const ClickEvent*,
                             intptr_t ix) {
        if (ix < 0 || ix >= self->checked.len) return;
        self->checked[(int)ix] = !self->checked[(int)ix];
        ToggleGroupEvent event{self->checked.els, self->checked.len};
        ListenerCall(cx->app, cx->win, self->onClick, &event);
    }
};

El* ToggleGroup::IntoEl() {
    Entity<ToggleGroupState> state;
    ToggleGroupState* stored = nullptr;
    if (onClick.IsValid() && !disabled) {
        state = ElementStateEntity<ToggleGroupState>(
            cx, id, StrL("gpui::component::ToggleGroupState"));
        stored = state.Get(cx);
        if (stored) {
            VecClear(stored->checked);
            for (Toggle* item : items)
                VecAppend(stored->checked, item->checked);
            stored->onClick = onClick;
        }
    }

    El* root = gpui::ToggleGroup::New(cx, id, Axis::Horizontal);
    El* row = Div(a)->FlexRow()->ItemsCenter();
    if (!segmented) row->Gap(8);
    int n = items.len;
    for (int i = 0; i < n; i++) {
        Toggle* item = items[i];
        item->Disabled(disabled || item->disabled)
            ->WithSize(size)
            ->WithVariant(variant);
        if (segmented && n > 1) {
            if (i == 0) {
                item->BorderCorners(true, false, false, true)
                    ->BorderEdges(true, true, true, true);
            } else if (i == n - 1) {
                item->BorderCorners(false, true, true, false)
                    ->BorderEdges(true, true, true, false);
            } else {
                item->BorderCorners(false, false, false, false)
                    ->BorderEdges(true, true, true, false);
            }
        }
        if (stored) {
            item->OnClick(ListenTo(state, &ToggleGroupState::OnChildClick, i));
        }
        row->Child(item->IntoEl());
    }
    return root->Child(row);
}

ToggleGroup* ToggleVariants::WithVariant(ToggleGroup* group,
                                         ToggleVariant variant) {
    return group ? group->WithVariant(variant) : nullptr;
}
ToggleGroup* ToggleVariants::Ghost(ToggleGroup* group) {
    return group ? group->Ghost() : nullptr;
}
ToggleGroup* ToggleVariants::Outline(ToggleGroup* group) {
    return group ? group->Outline() : nullptr;
}

DropdownButton* DropdownButton::New(Ctx* cx, Str id) {
    DropdownButton* d = ArenaNew<DropdownButton>(cx->a);
    d->a = cx->a;
    d->cx = cx;
    d->id = id;
    return d;
}
DropdownButton* DropdownButton::Button_(Button* b) {
    button = b;
    return this;
}
DropdownButton* DropdownButton::Menu(PopupMenu* m) {
    menu = m;
    return this;
}
DropdownButton* DropdownButton::Selected(bool v) {
    selected = v;
    return this;
}
DropdownButton* DropdownButton::Disabled(bool v) {
    disabled = v;
    return this;
}
DropdownButton* DropdownButton::Outline() {
    outline = true;
    return this;
}
DropdownButton* DropdownButton::WithVariant(ButtonVariant v) {
    hasVariant = true;
    variant = v;
    if (v != ButtonVariant::Custom) customVariant = {};
    return this;
}
DropdownButton* DropdownButton::Primary() {
    return WithVariant(ButtonVariant::Primary);
}
DropdownButton* DropdownButton::Secondary() {
    return WithVariant(ButtonVariant::Secondary);
}
DropdownButton* DropdownButton::Danger() {
    return WithVariant(ButtonVariant::Danger);
}
DropdownButton* DropdownButton::Warning() {
    return WithVariant(ButtonVariant::Warning);
}
DropdownButton* DropdownButton::Success() {
    return WithVariant(ButtonVariant::Success);
}
DropdownButton* DropdownButton::Info() {
    return WithVariant(ButtonVariant::Info);
}
DropdownButton* DropdownButton::Ghost() {
    return WithVariant(ButtonVariant::Ghost);
}
DropdownButton* DropdownButton::Link() {
    return WithVariant(ButtonVariant::Link);
}
DropdownButton* DropdownButton::Text() {
    return WithVariant(ButtonVariant::Text);
}
DropdownButton* DropdownButton::Custom(const ButtonCustomVariant& value) {
    customVariant = value;
    return WithVariant(ButtonVariant::Custom);
}
DropdownButton* DropdownButton::WithSize(UiSize s) {
    hasSize = true;
    size = s;
    return this;
}

// The props the two halves share. An outer variant or size applies to both;
// when either is unset the inner button's own becomes the shared value, so a
// caller can style the split from either level. Nothing here is invented for
// the caret: `compact`, `loading` and the tooltip stay on the action button.
static ButtonVariant DropdownVariant(const DropdownButton& d) {
    if (d.hasVariant) {
        return d.variant;
    }
    return d.button ? d.button->variant : ButtonVariant::Default;
}
static UiSize DropdownSize(const DropdownButton& d) {
    if (d.hasSize) {
        return d.size;
    }
    return d.button ? d.button->size : UiSize(UiSize::Medium);
}
static ButtonCustomVariant DropdownCustom(const DropdownButton& d) {
    if (d.hasVariant && d.variant == ButtonVariant::Custom) {
        return d.customVariant;
    }
    if (d.button && d.button->hasCustom) return d.button->customVariant;
    return {};
}

El* DropdownButton::IntoEl() {
    ButtonVariant v = DropdownVariant(*this);
    ButtonCustomVariant customValue = DropdownCustom(*this);
    UiSize sz = DropdownSize(*this);
    // An inner selected state is the split's, rather than being cleared by the
    // DropdownButton's own default.
    bool isSelected = selected || (button && button->selected);
    // The two halves stay visually joined for every variant. Only a ghost
    // split has no surface at rest, so only it needs hovering one half to
    // reveal the other, and the action half to stay revealed while the menu
    // holds the trigger pressed.
    bool isGhost = v == ButtonVariant::Ghost;
    // Rust records the menu's open state in keyed state and follows it
    // through `DropdownMenuPopover::on_open_change`; the menu's own state is
    // that value, and it is already in hand here.
    PopupMenuState* menuState = menu ? menu->state.Get(cx) : nullptr;
    bool menuOpen = menuState && menuState->open;

    IdScope scope(cx, id);
    El* row = Div(a)->Id(id)->FlexRow()->ItemsCenter();
    if (isGhost) {
        // div().group(HALVES_GROUP): the row is what a half's group_hover is
        // resolved against.
        row->Group();
    }
    if (button) {
        button->Selected(isSelected)
            ->Disabled(disabled || button->disabled)
            ->WithSize(sz);
        if (v == ButtonVariant::Custom)
            button->Custom(customValue);
        else
            button->WithVariant(v);
        if (outline) {
            button->Outline();
        }
        button->joined = true;
        button->cornerTL = true;
        button->cornerTR = false;
        button->cornerBL = true;
        button->cornerBR = false;
        if (isGhost) {
            button->HoverGroup()->HoverGroupHeld(menuOpen);
        }
        row->Child(button->IntoEl());
    }

    // The trigger renders on its own account rather than disappearing with the
    // action button, and a loading action button leaves it available: loading
    // is action-specific, `Disabled(true)` is what shuts both halves.
    if (menu) {
        Button* caret = Button::New(cx, StrL("popup"))
                            ->DropdownCaret()
                            ->Selected(isSelected)
                            ->Disabled(disabled)
                            ->WithSize(sz);
        if (v == ButtonVariant::Custom)
            caret->Custom(customValue);
        else
            caret->WithVariant(v);
        if (outline) {
            caret->Outline();
        }
        caret->joined = true;
        caret->edgeL = false;
        caret->cornerTL = false;
        caret->cornerTR = true;
        caret->cornerBL = false;
        caret->cornerBR = true;
        if (isGhost) {
            caret->HoverGroup();
        }
        El* trigger = caret->IntoEl();
        if (disabled) {
            row->Child(trigger);
        } else {
            row->Child(DropdownMenu::New(cx, StrL("menu"))
                           ->Trigger(trigger)
                           ->Menu(menu)
                           ->AnchorRight(anchorRight)
                           ->IntoEl());
        }
    }
    return row;
}

ButtonGroup* ButtonGroup::New(Ctx* cx, Str id) {
    ButtonGroup* g = ArenaNew<ButtonGroup>(cx->a);
    g->a = cx->a;
    g->cx = cx;
    g->id = id;
    return g;
}
ButtonGroup* ButtonGroup::Child(Button* b) {
    if (b) {
        // child(): the group's `disabled` is pushed down as the child is
        // added, which is why the order of the two calls matters in Rust.
        b->Disabled(b->disabled || disabled);
        children.Append(a, b);
    }
    return this;
}
ButtonGroup* ButtonGroup::Children(Button** values, int count) {
    // Rust's children() extends the vector directly; unlike child(), it does
    // not stamp the group's current disabled value onto each button.
    for (int i = 0; values && i < count; i++) {
        if (values[i]) children.Append(a, values[i]);
    }
    return this;
}
ButtonGroup* ButtonGroup::Multiple(bool v) {
    multiple = v;
    return this;
}
ButtonGroup* ButtonGroup::Disabled(bool v) {
    disabled = v;
    return this;
}
ButtonGroup* ButtonGroup::Vertical(bool v) {
    vertical = v;
    return this;
}
ButtonGroup* ButtonGroup::Layout(Axis value) {
    vertical = value == Axis::Vertical;
    return this;
}
ButtonGroup* ButtonGroup::Compact() {
    compact = true;
    return this;
}
ButtonGroup* ButtonGroup::Outline() {
    outline = true;
    return this;
}
ButtonGroup* ButtonGroup::WithVariant(ButtonVariant v) {
    hasVariant = true;
    variant = v;
    if (v != ButtonVariant::Custom) customVariant = {};
    return this;
}
ButtonGroup* ButtonGroup::Primary() {
    return WithVariant(ButtonVariant::Primary);
}
ButtonGroup* ButtonGroup::Secondary() {
    return WithVariant(ButtonVariant::Secondary);
}
ButtonGroup* ButtonGroup::Danger() {
    return WithVariant(ButtonVariant::Danger);
}
ButtonGroup* ButtonGroup::Warning() {
    return WithVariant(ButtonVariant::Warning);
}
ButtonGroup* ButtonGroup::Success() {
    return WithVariant(ButtonVariant::Success);
}
ButtonGroup* ButtonGroup::Info() {
    return WithVariant(ButtonVariant::Info);
}
ButtonGroup* ButtonGroup::Ghost() {
    return WithVariant(ButtonVariant::Ghost);
}
ButtonGroup* ButtonGroup::Link() {
    return WithVariant(ButtonVariant::Link);
}
ButtonGroup* ButtonGroup::Text() {
    return WithVariant(ButtonVariant::Text);
}
ButtonGroup* ButtonGroup::Custom(const ButtonCustomVariant& value) {
    customVariant = value;
    return WithVariant(ButtonVariant::Custom);
}
ButtonGroup* ButtonGroup::WithSize(UiSize s) {
    hasSize = true;
    size = s;
    return this;
}
ButtonGroup* ButtonGroup::OnClick(Listener l) {
    onClick = l;
    return this;
}

// The group's builder and its selected-index scratch are frame-owned, while a
// child listener has to survive until input dispatch. Rust uses an Rc<Cell>
// shared by the rendered children and group; a keyed entity is the equivalent
// lifetime seam in this runtime.
struct ButtonGroupState {
    Vec<int> selected;
    bool multiple = false;
    bool disabled = false;
    Listener onClick;

    static void OnChildClick(ButtonGroupState* self, Ctx* cx, const ClickEvent*,
                             intptr_t childIndex) {
        if (self->disabled) return;
        Vec<int> next = self->selected;
        int at = -1;
        for (int i = 0; i < next.len; i++) {
            if (next[i] == (int)childIndex) {
                at = i;
                break;
            }
        }
        if (self->multiple) {
            if (at >= 0) {
                for (int i = at + 1; i < next.len; i++) {
                    next[i - 1] = next[i];
                }
                next.len--;
            } else {
                VecAppend(next, (int)childIndex);
            }
        } else {
            VecClear(next);
            VecAppend(next, (int)childIndex);
        }

        ButtonGroupEvent ev{next.els, next.len};
        ListenerCall(cx->app, cx->win, self->onClick, &ev);
    }
};

El* ButtonGroup::IntoEl() {
    Entity<ButtonGroupState> state;
    ButtonGroupState* stateValue = nullptr;
    if (onClick.IsValid()) {
        state = ElementStateEntity<ButtonGroupState>(
            cx, id, StrL("gpui::component::ButtonGroupState"));
        stateValue = state.Get(cx->app);
        if (stateValue) {
            VecClear(stateValue->selected);
            for (int i = 0; i < children.len; i++) {
                if (children[i]->selected) {
                    VecAppend(stateValue->selected, i);
                }
            }
            stateValue->multiple = multiple;
            stateValue->disabled = disabled;
            stateValue->onClick = onClick;
        }
    }
    El* box = Div(a)->Id(id);
    if (vertical) {
        // Rust's column stretches its children to the widest of them, because
        // taffy's default align_items is Stretch. Layout here only stretches
        // to a cross size the parent already has, and a group shrink-wraps,
        // so a vertical group's buttons stay as wide as their own labels.
        box->FlexCol()->JustifyCenter();
    } else {
        box->FlexRow()->ItemsCenter();
    }
    for (int i = 0; i < children.len; i++) {
        Button* b = children[i];
        // A button's selected presentation alone is not a toggle state, but
        // membership in ButtonGroup is: upstream stamps every group child.
        b->Toggled(b->selected);
        if (hasSize) {
            b->WithSize(size);
        }
        if (hasVariant) {
            if (variant == ButtonVariant::Custom)
                b->Custom(customVariant);
            else
                b->WithVariant(variant);
        }
        if (compact) {
            b->Compact();
        }
        if (outline) {
            b->Outline();
        }
        b->joined = children.len > 1;
        if (children.len > 1) {
            // First / middle / last: the seam between two children is drawn
            // once, by the one after it.
            b->edgeT = vertical ? (i == 0) : true;
            b->edgeL = vertical ? true : (i == 0);
            b->edgeB = true;
            b->edgeR = true;
            if (i == 0) {
                b->cornerTL = true;
                b->cornerTR = vertical;
                b->cornerBL = !vertical;
                b->cornerBR = false;
            } else if (i == children.len - 1) {
                b->cornerTL = false;
                b->cornerTR = !vertical;
                b->cornerBL = vertical;
                b->cornerBR = true;
            } else {
                b->cornerTL = false;
                b->cornerTR = false;
                b->cornerBL = false;
                b->cornerBR = false;
            }
        }
        if (stateValue) {
            // Installing the group callback replaces a child's callback, as
            // Button::on_click does in the Rust map that builds the group.
            b->OnClick(ListenTo(state, &ButtonGroupState::OnChildClick, i));
        }
        box->Child(b->IntoEl());
    }
    return box;
}

} // namespace component
} // namespace gpui
