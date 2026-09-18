#include "ui/message.h"
#include "ui/bubble.h"

namespace gpui {

namespace component {

MessageGroup* MessageGroup::New(Ctx* cx) {
    Arena* a = cx->a;
    MessageGroup* s = ArenaNew<MessageGroup>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

MessageGroup* MessageGroup::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

MessageGroup* MessageGroup::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* MessageGroup::IntoEl() {
    El* column = Div(a)->FlexCol()->MinW(0)->Gap(8);
    if (styleSet) {
        column->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        column->Child(children[i]);
    }
    return column;
}

MessageAvatar* MessageAvatar::New(Ctx* cx) {
    Arena* a = cx->a;
    MessageAvatar* s = ArenaNew<MessageAvatar>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

MessageAvatar* MessageAvatar::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

MessageAvatar* MessageAvatar::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* MessageAvatar::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    El* slot = Div(a)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->MinW(32)
                   ->FlexNone()
                   ->JustifyCenter()
                   ->SelfEnd()
                   ->ClipX()
                   ->ClipY()
                   ->Radius(th.radiusFull)
                   ->Bg(th.tokens.muted);
    if (styleSet) {
        slot->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        slot->Child(children[i]);
    }
    return slot;
}

MessageHeader* MessageHeader::New(Ctx* cx) {
    Arena* a = cx->a;
    MessageHeader* s = ArenaNew<MessageHeader>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

MessageHeader* MessageHeader::ContentInset(bool value) {
    contentInset = value;
    hasContentInset = true;
    return this;
}

MessageHeader* MessageHeader::WithInheritedContentInset(bool value) {
    if (!hasContentInset) {
        contentInset = value;
        hasContentInset = true;
    }
    return this;
}

MessageHeader* MessageHeader::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

MessageHeader* MessageHeader::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

// MessageHeader and MessageFooter render the same row; only the slot they
// occupy differs, which is how the source keeps them two types.
static El* MessageMetaRow(Arena* a, Ctx* cx, bool contentInset,
                          bool hasContentInset, const Style& style,
                          uint32_t styleSet, const ArenaVec<El*>& children) {
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->MaxW(kFill)
                  ->MinW(0)
                  ->Gap(4)
                  ->Font(12)
                  ->LineHeight(1.25f)
                  ->Medium()
                  ->Fg(th.mutedFg);
    if (!hasContentInset || contentInset) {
        row->PadX(12);
    }
    if (styleSet) {
        row->Refine(style, styleSet);
    }
    for (int i = 0; i < len(children); i++) {
        row->Child(children[i]);
    }
    return row;
}

El* MessageHeader::IntoEl() {
    return MessageMetaRow(a, cx, contentInset, hasContentInset, style, styleSet,
                          children);
}

MessageContent* MessageContent::New(Ctx* cx) {
    Arena* a = cx->a;
    MessageContent* s = ArenaNew<MessageContent>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

MessageContent* MessageContent::WithBubble(Bubble* bubble) {
    if (bubble) {
        hasGhostBubble = hasGhostBubble || bubble->IsGhost();
        children.Append(a, bubble->IntoEl());
    }
    return this;
}

MessageContent* MessageContent::Aligned(MessageAlignment value) {
    alignment = value;
    return this;
}

MessageContent* MessageContent::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

MessageContent* MessageContent::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* MessageContent::IntoEl() {
    El* column = Div(a)
                     ->FlexCol()
                     ->W(kFill)
                     ->MaxW(kFill)
                     ->MinW(0)
                     // gap(rems(0.625)) at the 16px root.
                     ->Gap(10);
    column->ItemsStart();
    if (alignment == MessageAlignment::End) {
        column->ItemsEnd();
    }
    if (styleSet) {
        column->Refine(style, styleSet);
    }
    for (int i = 0; i < len(children); i++) {
        column->Child(children[i]);
    }
    return column;
}

MessageFooter* MessageFooter::New(Ctx* cx) {
    Arena* a = cx->a;
    MessageFooter* s = ArenaNew<MessageFooter>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

MessageFooter* MessageFooter::ContentInset(bool value) {
    contentInset = value;
    hasContentInset = true;
    return this;
}

MessageFooter* MessageFooter::WithInheritedContentInset(bool value) {
    if (!hasContentInset) {
        contentInset = value;
        hasContentInset = true;
    }
    return this;
}

MessageFooter* MessageFooter::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

MessageFooter* MessageFooter::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* MessageFooter::IntoEl() {
    return MessageMetaRow(a, cx, contentInset, hasContentInset, style, styleSet,
                          children);
}

Message* Message::New(Ctx* cx) {
    Arena* a = cx->a;
    Message* s = ArenaNew<Message>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

Message* Message::Alignment(MessageAlignment value) {
    alignment = value;
    return this;
}

Message* Message::WithStackStyle(const Style& s, uint32_t fields) {
    StyleApplyFields(&stackStyle, s, fields);
    stackStyleSet |= fields;
    return this;
}

Message* Message::Avatar(El* avatarEl) {
    avatar = MessageAvatar::New(cx)->Child(avatarEl);
    return this;
}

Message* Message::AvatarSlot(MessageAvatar* value) {
    avatar = value;
    return this;
}

Message* Message::Header(MessageHeader* value) {
    header = value;
    return this;
}

Message* Message::Content(MessageContent* value) {
    content = value;
    return this;
}

Message* Message::Footer(MessageFooter* value) {
    footer = value;
    return this;
}

Message* Message::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* Message::IntoEl() {
    bool hasAvatar = avatar != nullptr;
    bool hasGhostBubble = content && content->hasGhostBubble;

    El* root = Div(a)
                   ->FlexCol()
                   ->W(kFill)
                   ->MinW(0)
                   // gap(rems(0.625)) at the 16px root.
                   ->Gap(10)
                   ->Font(14)
                   ->LineHeight(1.25f);
    root->ItemsStart();
    if (alignment == MessageAlignment::End) {
        root->ItemsEnd();
    }
    if (styleSet) {
        root->Refine(style, styleSet);
    }

    // The footer lives outside this row so the bottom-anchored avatar always
    // sits flush with the content's bottom edge, whatever the footer holds.
    El* row = Div(a)->FlexRow()->W(kFill)->MinW(0)->ItemsEnd()->Gap(8);
    if (alignment == MessageAlignment::End) {
        row->FlexRowReverse();
    }
    if (avatar) {
        row->Child(avatar->IntoEl());
    }
    El* stack = Div(a)->FlexCol()->W(kFill)->MinW(0)->Gap(10);
    stack->ItemsStart();
    if (alignment == MessageAlignment::End) {
        stack->ItemsEnd();
    }
    if (stackStyleSet) {
        stack->Refine(stackStyle, stackStyleSet);
    }
    if (header) {
        stack->Child(header->WithInheritedContentInset(!hasGhostBubble)
                         ->IntoEl());
    }
    if (content) {
        stack->Child(content->Aligned(alignment)->IntoEl());
    }
    row->Child(stack);
    root->Child(row);

    if (footer) {
        El* el = footer->WithInheritedContentInset(!hasGhostBubble)->IntoEl();
        // Align the footer with the content column: the avatar's shared
        // `size-8` baseline plus the row gap, rems(2.5) at the 16px root.
        if (hasAvatar && alignment == MessageAlignment::Start) {
            el->MarginL(40);
        }
        if (hasAvatar && alignment == MessageAlignment::End) {
            el->MarginR(40);
        }
        root->Child(el);
    }
    return root;
}

} // namespace component
} // namespace gpui
