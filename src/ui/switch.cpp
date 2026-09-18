#include "ui/switch.h"
#include "base/motion.h"

namespace gpui {

namespace component {

// switch.rs no longer names a spring of its own: the thumb travels under the
// theme's `spring_move`, the policy everything that goes from one place to
// another shares, and the tolerance it carries is already the tenth of a
// pixel a travel in pixels wants.

Switch* Switch::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Switch* s = ArenaNew<Switch>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    return s;
}

Switch* Switch::Label(Str s) {
    label = s;
    return this;
}
Switch* Switch::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}
Switch* Switch::Checked(bool v) {
    checked = v;
    return this;
}
Switch* Switch::Disabled(bool v) {
    disabled = v;
    return this;
}
Switch* Switch::WithSize(UiSize s) {
    size = s;
    return this;
}
Switch* Switch::Color(Rgba c) {
    color = c;
    hasColor = true;
    return this;
}
Switch* Switch::FocusRing(bool v) {
    focusRing = v;
    return this;
}
Switch* Switch::TabIndex(int v) {
    tabIndex = v;
    return this;
}
Switch* Switch::TabStop(bool v) {
    tabStop = v;
    return this;
}
Switch* Switch::OnClick(Listener fn) {
    return OnChange(fn);
}
Switch* Switch::OnChange(Listener fn) {
    onClick = fn;
    return this;
}

struct SwitchFocusState {
    FocusHandle handle = {};
};

El* Switch::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Background on = hasColor ? Background(color) : th.tokens.primary;
    float trackW = 36;
    float trackH = 20;
    float thumb = 16;
    if (size == UiSize::Small || size == UiSize::XSmall) {
        trackW = 28;
        trackH = 16;
        thumb = 12;
    } else if (size == UiSize::Large) {
        trackW = 44;
        trackH = 24;
        thumb = 20;
    }
    // The unstyled parts own semantic-state priority. The unchecked fill is
    // the instance baseline; checked replaces it and disabled replaces that.
    //
    // Element opacity multiplies each primitive's alpha instead of
    // compositing the subtree as one group, so fading the whole control would
    // let the track show through the thumb. Fading the track alone — either
    // fill, not just the checked one — lands on the pixels a grouped fade
    // would, because the thumb is the background colour.
    Background trackBg = checked ? on : Background(th.tokens.secondary);
    SwitchTrackStyles trackStyles;
    trackStyles.Checked(StateStyle().Bg(on));
    trackStyles.Disabled(StateStyle().Bg(BackgroundOpacity(trackBg, 0.5f)));
    SwitchThumbStyles thumbStyles;
    // use_keyed_state(id) + track_focus: the UI layer and the base switch
    // must share one handle, or the ring would not know when the track is
    // focused. A keyed handle created here lives at a different path from
    // one the base would create on its own.
    SwitchFocusState* focusState =
        ElementState<SwitchFocusState>(cx, id, StrL("Switch"));
    if (!focusState->handle.IsValid()) {
        focusState->handle = FocusHandleNew(cx);
    }
    FocusHandle focus = focusState->handle;
    // Rust builds the track's id from `(id, "track")`, so the part is named
    // apart from the switch it sits in.
    El* track = SwitchTrack::New(cx, StrDup(a, fmt("%s-track", id)), checked,
                                 disabled, &trackStyles)
                    ->W(trackW)
                    ->H(trackH)
                    // The thumb inset is a 1px border plus 1px padding, not a
                    // 2px border: the focus ring tints the border solid, and
                    // that 1px line is what keeps the ring visible on an
                    // unchecked track. Its 50% halo alone lands within a few
                    // values of `switch.background` in both default modes.
                    ->Border(1, th.transparent)
                    ->Pad(1)
                    ->Radius(trackH * 0.5f)
                    ->ItemsCenter();
    if (focusRing) {
        // The ring hugs the track, not the row, so the label stays outside
        // it. The track is not a tab stop: it shares the switch's handle so
        // El::FocusRing paints here while keyboard focus stays on the root.
        track->TrackFocus(focus)->TabStop(false)->FocusRing(true);
    }
    if (!checked) {
        track->Bg(th.tokens.secondary);
    }
    // The thumb slides rather than jumping: Rust animates `left` from one end
    // to the other over 150 ms whenever the checked flag turns over. A
    // disabled switch does not animate there, and does not here.
    float inset = 2.f;
    float maxX = trackW - thumb - inset * 2;
    float x = checked ? maxX : 0.f;
    if (!disabled) {
        // spring_move: a switch is toggled again long before the thumb has
        // finished travelling, and a spring turns it around from where it is
        // rather than restarting the curve.
        x = SpringValue(cx, MotionId(id, StrL("switch-thumb")), x,
                        th.motion.springMove);
    }
    // Absolutely placed, since what moves is an offset rather than which end
    // of the track the thumb is packed against.
    // The thumb keeps its own colour in every state; only the track fades.
    El* thumbEl = SwitchThumb::New(cx, checked, disabled, &thumbStyles)
                      ->Absolute()
                      ->Left(inset + x)
                      ->Top(inset)
                      ->W(thumb)
                      ->H(thumb)
                      ->Radius(thumb * 0.5f)
                      ->Bg(th.tokens.switchThumb);
    track->Child(thumbEl);
    // The disabled root mutes the label, which is what Rust's
    // `styles(|styles| styles.disabled(..))` on the switch itself puts there.
    // Its other half, `cursor_not_allowed()`, has no seam: StateStyle carries
    // fills, borders and radii, not a cursor, so a disabled switch keeps the
    // arrow rather than showing the barred pointer.
    SwitchStyles rootStyles;
    rootStyles.Disabled(StateStyle().Fg(th.mutedFg));
    // gpui_base::Switch owns identity, focus and activation, and hands the
    // handler the value the activation produces.
    Str name = accessibilityLabel.s ? accessibilityLabel : label;
    El* root =
        gpui::Switch::New(cx, id, checked, disabled, onClick, &rootStyles,
                          nullptr, name, tabIndex, tabStop, focus)
            ->FlexRow()
            ->ItemsCenter()
            ->Gap(8);
    root->Child(track);
    if (label.s) {
        // text_sm below Medium, text_base at and above it. A disabled switch
        // mutes the label along with the track it names.
        float labelFont =
            (size == UiSize::XSmall || size == UiSize::Small) ? 14.f : 16.f;
        root->Child(TextEl(a, label)->Font(labelFont)->Fg(
            disabled ? th.mutedFg : th.foreground));
    }
    return root;
}

} // namespace component
} // namespace gpui
