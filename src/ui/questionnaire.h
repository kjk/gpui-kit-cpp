#ifndef GPUI_UI_QUESTIONNAIRE_H_
#define GPUI_UI_QUESTIONNAIRE_H_
/* Themed questionnaire — crates/component/src/questionnaire/

   Composable questionnaire parts over `base/questionnaire.h`, following the
   ReUI `base-nova` questionnaire skin. The behavior — answers, validation,
   navigation, focus and shortcuts — lives in base; the public names are the
   same on both sides, the way `gpui_component::questionnaire::*` re-exports
   them.

   Every part takes the state and, where Rust's does, the item name. A part
   that Rust renders as `gpui::Empty` answers null from IntoEl, which Child()
   skips. A part's own instance style is `Refine(style, fields)` — Rust's
   `Styled` refinement — or whatever the caller chains on a non-null result. */

#include "base/questionnaire.h"
#include "ui/sizing.h"

namespace gpui {

namespace component {

// Fields every part carries: the state, the scale it was told (None falls
// back to the one its root published), its children and its refinement.
struct QuestionnairePart {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Entity<QuestionnaireState> state = {};
    Str item = {};
    UiSize size = UiSize::Medium;
    bool hasSize = false;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;
    // The shell's whole StyleRefinement, replayed after `style`.
    ElRefiner refiner = {};
};

#define GPUI_QUESTIONNAIRE_PART(T)                       \
    T* WithSize(UiSize s);                               \
    T* Child(El* e);                                     \
    T* Refine(const Style& refinement, uint32_t fields); \
    El* IntoEl();

// The composable questionnaire root. It owns layout and keyboard routing
// while QuestionnaireState remains the single source of behavioral state.
struct Questionnaire : QuestionnairePart {
    static Questionnaire* New(Ctx* cx, Entity<QuestionnaireState> state);
    GPUI_QUESTIONNAIRE_PART(Questionnaire)
};

// Textual progress.
struct QuestionnaireProgress : QuestionnairePart {
    static QuestionnaireProgress* New(Ctx* cx,
                                      Entity<QuestionnaireState> state);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireProgress)
};

struct QuestionnaireTitle : QuestionnairePart {
    static QuestionnaireTitle* New(Ctx* cx, Entity<QuestionnaireState> state,
                                   Str item);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireTitle)
};

struct QuestionnaireDescription : QuestionnairePart {
    static QuestionnaireDescription* New(Ctx* cx,
                                         Entity<QuestionnaireState> state,
                                         Str item);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireDescription)
};

// The active question group. Inactive or disabled items do not enter layout,
// focus traversal, or the accessibility tree.
struct QuestionnaireItem : QuestionnairePart {
    static QuestionnaireItem* New(Ctx* cx, Entity<QuestionnaireState> state,
                                  Str item);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireItem)
};

// Container for an item's answer controls.
struct QuestionnaireChoices : QuestionnairePart {
    static QuestionnaireChoices* New(Ctx* cx, Entity<QuestionnaireState> state,
                                     Str item);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireChoices)
};

// render_indicator / render_shortcut: `Fn(&QuestionnaireChoiceState, &mut
// Window, &mut App) -> AnyElement`.
using QuestionnaireChoiceRenderer =
    El* (*)(Ctx * cx, const QuestionnaireChoiceState* choice);

// A selectable choice card.
struct QuestionnaireChoice : QuestionnairePart {
    Str value = {};
    Style indicatorStyle = {};
    uint32_t indicatorStyleSet = 0;
    Style contentStyle = {};
    uint32_t contentStyleSet = 0;
    Style shortcutStyle = {};
    uint32_t shortcutStyleSet = 0;
    QuestionnaireChoiceRenderer indicatorRenderer = nullptr;
    QuestionnaireChoiceRenderer shortcutRenderer = nullptr;

    static QuestionnaireChoice* New(Ctx* cx, Entity<QuestionnaireState> state,
                                    Str item, Str value);
    QuestionnaireChoice* IndicatorStyle(const Style& value, uint32_t fields);
    QuestionnaireChoice* ContentStyle(const Style& value, uint32_t fields);
    QuestionnaireChoice* ShortcutStyle(const Style& value, uint32_t fields);
    QuestionnaireChoice* RenderIndicator(QuestionnaireChoiceRenderer fn);
    QuestionnaireChoice* RenderShortcut(QuestionnaireChoiceRenderer fn);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireChoice)
};

// Secondary text for custom choice compositions.
struct QuestionnaireChoiceDescription : QuestionnairePart {
    static QuestionnaireChoiceDescription* New(Ctx* cx);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireChoiceDescription)
};

// The optional freeform answer input for an item. Rust's has no children.
struct QuestionnaireInput : QuestionnairePart {
    static QuestionnaireInput* New(Ctx* cx, Entity<QuestionnaireState> state,
                                   Str item);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireInput)
};

// Validation error for an item. It only enters the tree while invalid.
struct QuestionnaireError : QuestionnairePart {
    static QuestionnaireError* New(Ctx* cx, Entity<QuestionnaireState> state,
                                   Str item);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireError)
};

// Layout part for questionnaire navigation actions.
struct QuestionnaireActions : QuestionnairePart {
    static QuestionnaireActions* New(Ctx* cx, Entity<QuestionnaireState> state);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireActions)
};

struct QuestionnairePrevious : QuestionnairePart {
    static QuestionnairePrevious* New(Ctx* cx,
                                      Entity<QuestionnaireState> state);
    GPUI_QUESTIONNAIRE_PART(QuestionnairePrevious)
};

struct QuestionnaireSkip : QuestionnairePart {
    static QuestionnaireSkip* New(Ctx* cx, Entity<QuestionnaireState> state);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireSkip)
};

struct QuestionnaireNext : QuestionnairePart {
    static QuestionnaireNext* New(Ctx* cx, Entity<QuestionnaireState> state);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireNext)
};

struct QuestionnaireSubmit : QuestionnairePart {
    static QuestionnaireSubmit* New(Ctx* cx, Entity<QuestionnaireState> state);
    GPUI_QUESTIONNAIRE_PART(QuestionnaireSubmit)
};

#undef GPUI_QUESTIONNAIRE_PART

// Base reports why an item failed; the skin owns the sentence a person reads.
Str QuestionnaireErrorText(const QuestionnaireValidationError& error);

// The scale a part resolves: its own when it named one, otherwise the one its
// root published under the state, otherwise Medium. GPUI has no style
// cascade, so the root records the scale for its parts as it renders.
UiSize QuestionnaireResolveSize(App* app, const QuestionnairePart* part);
void QuestionnairePublishSize(App* app, Entity<QuestionnaireState> state,
                              UiSize size);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_QUESTIONNAIRE_H_
