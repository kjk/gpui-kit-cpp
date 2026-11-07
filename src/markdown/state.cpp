/* src/state.rs — the dispatcher.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).

   The indexed table keeps dispatch constant-time and keeps an amalgamating
   compiler from folding every state handler into one very large function. */

#include "markdown/construct.h"

namespace markdown {

using StateFn = State (*)(Tokenizer*);

// StateName's values index this table; Count catches length drift.
static StateFn const kStateFns[] = {
#define GPUI_MARKDOWN_STATE_FN(name) name,
    GPUI_MARKDOWN_STATES(GPUI_MARKDOWN_STATE_FN)
#undef GPUI_MARKDOWN_STATE_FN
};
static_assert(sizeof(kStateFns) / sizeof(kStateFns[0]) ==
              (uint16_t)StateName::Count);

State Call(Tokenizer* t, StateName name) {
    return kStateFns[(uint16_t)name](t);
}

} // namespace markdown
