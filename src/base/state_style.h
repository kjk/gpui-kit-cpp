#ifndef GPUI_BASE_STATE_STYLE_H_
#define GPUI_BASE_STATE_STYLE_H_
/* Semantic-state styles — crates/base/src/state_style.rs

   A control looks one way, and then differently when it is selected, pressed,
   checked, focused or disabled. Rust layers those with GPUI's StyleRefinement:
   a partial style where an unset field means "leave whatever is underneath".
   `resolve_style` puts them in one fixed order so no two controls drift apart —

     1. the instance style the caller built,
     2. the value states: checked, pressed, selected, focused,
     3. disabled, always resolved last.

   The Style in this tree is a whole style, not a refinement, so a refinement
   here is a Style plus the set of fields that were actually named. Only the
   fields a control state overrides are refinable; the rest of a control's look
   is layout, and no semantic state moves a control. */

#include "gpui/gpui.h"

namespace gpui {

// The fields a state may name. A field not named is left as it was. They are
// `StyleField`'s bits: a semantic state and the inspector's live edit are both
// refinements of a whole style, so one set of names covers both.
enum StateField : uint32_t {
    StateFieldBg = StyleFieldBg,
    StateFieldFg = StyleFieldColor,
    // Width and colour together, as `border_*` is.
    StateFieldBorder = StyleFieldBorder | StyleFieldBorderColor,
    // One edge at a time, which is what `.border_l_2()` names.
    StateFieldBorderL = StyleFieldBorderL | StyleFieldBorderColor,
    StateFieldBorderR = StyleFieldBorderR | StyleFieldBorderColor,
    StateFieldBorderT = StyleFieldBorderT | StyleFieldBorderColor,
    StateFieldBorderB = StyleFieldBorderB | StyleFieldBorderColor,
    StateFieldRadius = StyleFieldRadius,
    StateFieldHoverBg = StyleFieldHoverBg,
    StateFieldHoverFg = StyleFieldHoverFg,
    StateFieldActiveBg = StyleFieldActiveBg,
    // What Rust's own `disabled(|style| style.opacity(0.5))` names.
    StateFieldOpacity = StyleFieldOpacity,
    StateFieldPad = StyleFieldPad,
};

struct StateStyle {
    Style style = {};
    uint32_t set = 0;

    StateStyle& Bg(Background c);
    StateStyle& Fg(Rgba c);
    StateStyle& Border(float w, Rgba c);
    StateStyle& BorderL(float w, Rgba c);
    StateStyle& BorderR(float w, Rgba c);
    StateStyle& BorderT(float w, Rgba c);
    StateStyle& BorderB(float w, Rgba c);
    StateStyle& Radius(float v);
    StateStyle& HoverBg(Background c);
    StateStyle& HoverFg(Rgba c);
    StateStyle& ActiveBg(Background c);
    StateStyle& Opacity(float v);
    // px(x).py(y): the four edges, horizontal and vertical.
    StateStyle& Pad(float x, float y);

    // Some fields name a value and its shared colour bit. All of the bits
    // must be present: testing for any bit makes BorderL look like BorderB
    // merely because both carry BorderColor.
    bool Has(StateField f) const {
        uint32_t bits = (uint32_t)f;
        return (set & bits) == bits;
    }
};

// StyleRefinement::refine: `over` wins for the fields it names, and only those.
void StateStyleRefine(StateStyle* into, const StateStyle& over);

// resolve_style. The states come in the order above; one that is not active is
// simply left out, which is what Rust's `then_some(..).flatten()` does.
StateStyle StateStyleResolve(const StateStyle& instance,
                             const StateStyle* const* states, int n);

// Put a resolved style on an element. It lands at layout time rather than
// where the call sits, which is what lets a primitive hand an element back and
// have the state still win over the look the caller chains onto it — the
// order `resolve_style` promises.
El* ElRefine(El* e, const StateStyle& s);

} // namespace gpui
#endif // GPUI_BASE_STATE_STYLE_H_
