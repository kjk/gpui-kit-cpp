#ifndef GPUI_SHELL_STYLE_H_
#define GPUI_SHELL_STYLE_H_

// crates/shell/src/style.rs: which method names on a script element are style
// methods, and what each one does to the element's style.
//
// Rust reflects the no-argument half off GPUI's `Styled` and gpui-base's
// `StyledExt` — the macro-generated spacing scale (`w_4`, `gap_1p5`,
// `mt_neg_2`, `w_1_2`, `inset_full`), the corner and border ramps
// (`rounded_md`, `border_t_2`), shadows, cursors, text sizes and decorations,
// display, position, overflow and flex keywords. This tree has no reflection,
// so the same vocabulary is written out as tables: a family prefix with a
// suffix grammar for the ramps, and a keyword table for the rest.

#include "shell/spec.h"

namespace gpui::shell {

// `param_style_name`: whether `name` is one of the style methods that take an
// argument (Rust's PARAM_STYLES).
bool IsParamStyleName(Str name);

// `nullary_index` answering Some: whether `name` is a no-argument style
// method.
bool IsNullaryStyleName(Str name);

// `apply_nullary`. False when `name` is not a no-argument style method.
bool ApplyNullaryStyle(El* element, Str name);

// `apply_param`. False when `op.name` is not a parametric style method (the
// error is left alone) or when its argument is refused (`error` carries
// Rust's message).
bool ApplyParamStyle(El* element, const SpecOp& op, ShellError* error);

// The StyleField bits a style method names, for a hover/active/focus
// refinement that must copy only what it set. Zero for a method whose field
// no StyleField covers.
uint32_t StyleFieldsOf(Str name);

} // namespace gpui::shell
#endif // GPUI_SHELL_STYLE_H_
