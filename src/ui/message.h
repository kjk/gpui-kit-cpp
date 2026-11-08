#ifndef GPUI_SRC_UI_MESSAGE_H_
#define GPUI_SRC_UI_MESSAGE_H_
/* Themed chat message — crates/ui/src/message.rs */

#include "ui/sizing.h"
#include "base/styled.h"

namespace gpui {

namespace component {

struct Bubble;

// Horizontal alignment for a message and message-owned chat surfaces.
enum class MessageAlignment : uint8_t {
    // Place the message at the leading edge.
    Start,
    // Place the message at the trailing edge.
    End
};

// What MessageGroup and BubbleGroup render: the children stacked in a
// column half a rem apart, with the caller's style over it.
El* MessageGroupColumn(Ctx* cx, const ArenaVec<El*>& children,
                       const Style& style, uint32_t styleSet);

// A vertical stack of consecutive messages from the same sender.
struct MessageGroup : SlotBuilder<MessageGroup> {
    El* IntoEl();
};

// The sender identity slot rendered beside a Message. It reserves the shared
// `size-8` baseline; the message row keeps it flush with the bottom edge of
// the visible message surface.
struct MessageAvatar : SlotBuilder<MessageAvatar> {
    El* IntoEl();
};

// The row MessageHeader and MessageFooter both render; the inset padding
// applies unless the caller turned it off.
El* MessageMetaRow(Ctx* cx, bool contentInset, bool hasContentInset,
                   const Style& style, uint32_t styleSet,
                   const ArenaVec<El*>& children);

// MessageHeader and MessageFooter are the same slot; only where it sits in
// the message differs, which is how the source keeps them two types.
template <typename Self>
struct MessageMetaSlot : SlotBuilder<Self> {
    // Option<bool>: unset inherits from the message's content.
    bool contentInset = false;
    bool hasContentInset = false;

    Self* ContentInset(bool value) {
        contentInset = value;
        hasContentInset = true;
        return static_cast<Self*>(this);
    }
    // The message's own answer, taken only when the caller gave none.
    Self* WithInheritedContentInset(bool value) {
        if (!hasContentInset) {
            contentInset = value;
            hasContentInset = true;
        }
        return static_cast<Self*>(this);
    }
    El* IntoEl() {
        return MessageMetaRow(this->cx, contentInset, hasContentInset,
                              this->style, this->styleSet, this->children);
    }
};

// Header content such as a sender name and timestamp.
struct MessageHeader : MessageMetaSlot<MessageHeader> {};

// The message body slot. It can contain bubbles, images, code, or files.
struct MessageContent : SlotBuilder<MessageContent> {
    MessageAlignment alignment = MessageAlignment::Start;
    bool hasGhostBubble = false;

    // Add a typed bubble and inherit its ghost-surface metadata layout.
    // Ordinary Child stays available for arbitrary elements.
    MessageContent* WithBubble(Bubble* bubble);
    MessageContent* Aligned(MessageAlignment value);
    El* IntoEl();
};

// Footer content such as delivery state, reactions, or action buttons.
struct MessageFooter : MessageMetaSlot<MessageFooter> {};

// A composable message row with named avatar, header, content and footer
// slots. The named slots let the message apply its alignment consistently
// while every part stays independently styleable.
struct Message : StyledBuilder<Message>, IdBuilder<Message> {
    // Option<ElementId>.
    RoleOverride role = {};
    Style stackStyle = {};
    uint32_t stackStyleSet = 0;
    MessageAlignment alignment = MessageAlignment::Start;
    MessageAvatar* avatar = nullptr;
    MessageHeader* header = nullptr;
    MessageContent* content = nullptr;
    MessageFooter* footer = nullptr;

    // A stable identity, so the message can appear in the accessibility tree
    // and keep element state across frames.
    // The role announced for this message, e.g. ListItem for the rows of a
    // transcript. A message is presentational by default; an accessibility
    // node needs a stable identity, so the role takes effect only with Id.
    Message* Role(RoleOverride value);
    Message* Alignment(MessageAlignment value);
    Message* WithStackStyle(const Style& s, uint32_t fields);
    // An avatar or other sender identity element, wrapped in a default slot.
    Message* Avatar(El* avatarEl);
    Message* AvatarSlot(MessageAvatar* value);
    Message* Header(MessageHeader* value);
    Message* Content(MessageContent* value);
    Message* Footer(MessageFooter* value);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_MESSAGE_H_
