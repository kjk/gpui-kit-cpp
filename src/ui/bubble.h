#ifndef GPUI_SRC_UI_BUBBLE_H_
#define GPUI_SRC_UI_BUBBLE_H_
/* Themed chat bubble — crates/ui/src/bubble.rs */

#include "ui/sizing.h"
#include "ui/message.h"

namespace gpui {

namespace component {

struct Button;

// Visual treatment for a chat bubble.
enum class BubbleVariant : uint8_t {
    // A filled primary surface.
    Filled,
    // A neutral secondary surface.
    Secondary,
    // A lower-emphasis surface.
    Muted,
    // A subtle primary-tinted surface.
    Tinted,
    // A background surface with a visible border.
    Outline,
    // No surface, padding or border.
    Ghost,
    // A destructive surface for failed or invalid content.
    Destructive
};

// The edge on which reaction feedback is attached.
enum class BubbleReactionSide : uint8_t {
    // Attach reactions above the bubble.
    Top,
    // Attach reactions below the bubble.
    Bottom
};

// The visible surface inside a Bubble.
//
// This part owns padding, radius, border, typography and semantic colours, so
// a caller can refine the surface without changing the bubble's row layout.
struct BubbleContent : SlotBuilder<BubbleContent> {
    // Filled in by the Bubble that owns this surface.
    BubbleVariant variant = BubbleVariant::Filled;
    MessageAlignment alignment = MessageAlignment::Start;
    bool hasAlignment = false;

    El* IntoEl();
};

// A vertical stack of consecutive bubbles from one sender.
struct BubbleGroup : SlotBuilder<BubbleGroup> {
    El* IntoEl();
};

// One entry of a BubbleReactions: a typed action, which takes the region's
// pill geometry, or an arbitrary element, which keeps its own styling.
struct BubbleReactionChild {
    Button* action = nullptr;
    El* element = nullptr;
};

// A styleable reaction region positioned on a bubble edge. Compose existing
// Button values inside it to keep button semantics and keyboard behaviour.
struct BubbleReactions {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<BubbleReactionChild> children;
    BubbleReactionSide side = BubbleReactionSide::Bottom;
    MessageAlignment alignment = MessageAlignment::End;
    Style style = {};
    uint32_t styleSet = 0;

    static BubbleReactions* New(Ctx* cx);
    BubbleReactions* Side(BubbleReactionSide value);
    BubbleReactions* Alignment(MessageAlignment value);
    // A typed action gives up the region's decorative content padding and
    // takes the theme's full radius, so it reads as part of one surface.
    BubbleReactions* Action(Button* action);
    BubbleReactions* Child(El* e);
    BubbleReactions* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// A chat bubble layout that owns alignment, width and reaction positioning.
// The visible surface is rendered by BubbleContent; direct children are added
// to that content slot as a convenience.
struct Bubble {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Style style = {};
    uint32_t styleSet = 0;
    MessageAlignment alignment = MessageAlignment::Start;
    bool hasAlignment = false;
    BubbleVariant variant = BubbleVariant::Filled;
    BubbleContent* content = nullptr;
    BubbleReactions* reactions = nullptr;

    static Bubble* New(Ctx* cx);
    Bubble* Alignment(MessageAlignment value);
    Bubble* WithVariant(BubbleVariant value);
    bool IsGhost() const { return variant == BubbleVariant::Ghost; }
    // Replace the visible content surface. Children already added directly to
    // the bubble move into the new surface, in front of its own children, so
    // Child composes the same way on either side of this call.
    Bubble* Content(BubbleContent* value);
    Bubble* Reactions(BubbleReactions* value);
    Bubble* Child(El* e);
    Bubble* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_BUBBLE_H_
