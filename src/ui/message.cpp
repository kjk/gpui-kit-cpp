#include "ui/message.h"
#include "ui/bubble.h"

namespace gpui {

namespace component {

El* MessageGroupColumn(Ctx* cx, const ArenaVec<El*>& children,
                       const Style& style, uint32_t styleSet) {
    El* column = Div(cx->a)->FlexCol()->MinW(0)->Gap(Rems(cx, 0.5f));
    if (styleSet) {
        column->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        column->Child(children[i]);
    }
    return column;
}

El* MessageGroup::IntoEl() {
    return MessageGroupColumn(cx, children, style, styleSet);
}

El* MessageAvatar::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    El* slot = Div(a)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->MinW(Rems(cx, 2))
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

El* MessageMetaRow(Ctx* cx, bool contentInset, bool hasContentInset,
                   const Style& style, uint32_t styleSet,
                   const ArenaVec<El*>& children) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->MaxW(kFill)
                  ->MinW(0)
                  ->Gap(Rems(cx, 0.25f))
                  ->Font(12)
                  ->LineHeight(1.25f)
                  ->Medium()
                  ->Fg(th.mutedFg);
    if (!hasContentInset || contentInset) {
        row->PadX(Rems(cx, 0.75f));
    }
    if (styleSet) {
        row->Refine(style, styleSet);
    }
    for (int i = 0; i < len(children); i++) {
        row->Child(children[i]);
    }
    return row;
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

El* MessageContent::IntoEl() {
    El* column = Div(a)->FlexCol()->W(kFill)->MaxW(kFill)->MinW(0)->Gap(
        Rems(cx, 0.625f));
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

Message* Message::Role(RoleOverride value) {
    role = value;
    return this;
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

El* Message::IntoEl() {
    bool hasAvatar = avatar != nullptr;
    bool hasGhostBubble = content && content->hasGhostBubble;

    // No text size or line height here: the header and footer set their own,
    // and content typography belongs to the bubble or the caller.
    El* root = Div(a)->FlexCol()->W(kFill)->MinW(0)->Gap(Rems(cx, 0.625f));
    root->ItemsStart();
    if (alignment == MessageAlignment::End) {
        root->ItemsEnd();
    }
    if (styleSet) {
        root->Refine(style, styleSet);
    }

    // The footer lives outside this row so the bottom-anchored avatar always
    // sits flush with the content's bottom edge, whatever the footer holds.
    El* row =
        Div(a)->FlexRow()->W(kFill)->MinW(0)->ItemsEnd()->Gap(Rems(cx, 0.5f));
    if (alignment == MessageAlignment::End) {
        row->FlexRowReverse();
    }
    if (avatar) {
        row->Child(avatar->IntoEl());
    }
    El* stack = Div(a)->FlexCol()->W(kFill)->MinW(0)->Gap(Rems(cx, 0.625f));
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
        // `size-8` baseline plus the row gap, rems(2.5).
        if (hasAvatar && alignment == MessageAlignment::Start) {
            el->MarginL(Rems(cx, 2.5f));
        }
        if (hasAvatar && alignment == MessageAlignment::End) {
            el->MarginR(Rems(cx, 2.5f));
        }
        root->Child(el);
    }

    // `role` lives on the stateful element: an accessibility node needs the
    // stable identity only an element id provides.
    if (hasId) {
        root->PathId(id);
        if (role.kind == RoleOverrideKind::Role) {
            root->Role(role.role);
        }
    }
    return root;
}

} // namespace component
} // namespace gpui
