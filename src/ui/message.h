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

// A vertical stack of consecutive messages from the same sender.
struct MessageGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static MessageGroup* New(Ctx* cx);
    MessageGroup* Child(El* e);
    MessageGroup* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// The sender identity slot rendered beside a Message. It reserves the shared
// `size-8` baseline; the message row keeps it flush with the bottom edge of
// the visible message surface.
struct MessageAvatar {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static MessageAvatar* New(Ctx* cx);
    MessageAvatar* Child(El* e);
    MessageAvatar* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// Header content such as a sender name and timestamp.
struct MessageHeader {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    // Option<bool>: unset inherits from the message's content.
    bool contentInset = false;
    bool hasContentInset = false;
    Style style = {};
    uint32_t styleSet = 0;

    static MessageHeader* New(Ctx* cx);
    MessageHeader* ContentInset(bool value);
    // The message's own answer, taken only when the caller gave none.
    MessageHeader* WithInheritedContentInset(bool value);
    MessageHeader* Child(El* e);
    MessageHeader* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// The message body slot. It can contain bubbles, images, code, or files.
struct MessageContent {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    MessageAlignment alignment = MessageAlignment::Start;
    bool hasGhostBubble = false;
    Style style = {};
    uint32_t styleSet = 0;

    static MessageContent* New(Ctx* cx);
    // Add a typed bubble and inherit its ghost-surface metadata layout.
    // Ordinary Child stays available for arbitrary elements.
    MessageContent* WithBubble(Bubble* bubble);
    MessageContent* Aligned(MessageAlignment value);
    MessageContent* Child(El* e);
    MessageContent* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// Footer content such as delivery state, reactions, or action buttons.
struct MessageFooter {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    bool contentInset = false;
    bool hasContentInset = false;
    Style style = {};
    uint32_t styleSet = 0;

    static MessageFooter* New(Ctx* cx);
    MessageFooter* ContentInset(bool value);
    MessageFooter* WithInheritedContentInset(bool value);
    MessageFooter* Child(El* e);
    MessageFooter* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// A composable message row with named avatar, header, content and footer
// slots. The named slots let the message apply its alignment consistently
// while every part stays independently styleable.
struct Message {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    // Option<ElementId>.
    Str id = {};
    bool hasId = false;
    RoleOverride role = {};
    Style style = {};
    uint32_t styleSet = 0;
    Style stackStyle = {};
    uint32_t stackStyleSet = 0;
    MessageAlignment alignment = MessageAlignment::Start;
    MessageAvatar* avatar = nullptr;
    MessageHeader* header = nullptr;
    MessageContent* content = nullptr;
    MessageFooter* footer = nullptr;

    static Message* New(Ctx* cx);
    // A stable identity, so the message can appear in the accessibility tree
    // and keep element state across frames.
    Message* Id(Str value);
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
    Message* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_MESSAGE_H_
