/* Ported from the tests in crates/ui/src/message.rs:
 * test_message_builder and test_ghost_bubble_inherits_message_slot_insets. */

#include "Test.h"

using namespace gpui::component;

static void TheBuilderCarriesAlignmentAndEverySlot() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Style stackStyle = {};
    stackStyle.gapX = 4;
    stackStyle.gapY = 4;
    Message* message =
        Message::New(&cx)
            ->Alignment(MessageAlignment::End)
            ->WithStackStyle(stackStyle, StyleFieldGap)
            ->AvatarSlot(MessageAvatar::New(&cx)->Child(Div(a)))
            ->Header(MessageHeader::New(&cx)->ContentInset(false)->Child(
                TextEl(a, StrL("Alice"))))
            ->Content(MessageContent::New(&cx)->Child(TextEl(a, StrL("Hello"))))
            ->Footer(MessageFooter::New(&cx)->ContentInset(false)->Child(
                TextEl(a, StrL("Delivered"))));

    utassert(message->alignment == MessageAlignment::End);
    utassert((message->stackStyleSet & StyleFieldGap) != 0);
    utassertnear(message->stackStyle.gapY, 4.f);
    utassert(message->avatar != nullptr);
    utassert(message->header != nullptr);
    utassert(message->content != nullptr);
    utassert(message->footer != nullptr);
    utassert(message->header->hasContentInset && !message->header
                                                      ->contentInset);
    utassert(message->footer->hasContentInset && !message->footer
                                                      ->contentInset);

    MessageGroup* group = MessageGroup::New(&cx)
                              ->Child(TextEl(a, StrL("First")))
                              ->Child(TextEl(a, StrL("Second")));
    utassert(group->children.len == 2);

    MessageContent* content = MessageContent::New(&cx)
                                  ->Aligned(MessageAlignment::End);
    utassert(content->alignment == MessageAlignment::End);

    MessageAvatar* avatar = MessageAvatar::New(&cx)
                                ->Child(TextEl(a, StrL("ME")));
    utassert(avatar->children.len == 1);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void AGhostBubbleRemovesTheMetadataInsets() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    MessageContent* content =
        MessageContent::New(&cx)
            ->WithBubble(Bubble::New(&cx))
            ->WithBubble(Bubble::New(&cx)->WithVariant(BubbleVariant::Ghost));
    utassert(content->hasGhostBubble);
    utassert(content->children.len == 2);

    utassert(!MessageHeader::New(&cx)
                  ->WithInheritedContentInset(false)
                  ->contentInset);
    utassert(!MessageFooter::New(&cx)
                  ->WithInheritedContentInset(false)
                  ->contentInset);
    // An explicit answer wins over the inherited one.
    utassert(MessageHeader::New(&cx)
                 ->ContentInset(true)
                 ->WithInheritedContentInset(false)
                 ->contentInset);
    utassert(MessageFooter::New(&cx)
                 ->ContentInset(true)
                 ->WithInheritedContentInset(false)
                 ->contentInset);

    // And the inset it comes to on the rendered rows: px_3 by default, none
    // beside a ghost surface.
    El* framed =
        Message::New(&cx)
            ->Header(MessageHeader::New(&cx)->Child(TextEl(a, StrL("System"))))
            ->Content(MessageContent::New(&cx)->WithBubble(Bubble::New(&cx)))
            ->IntoEl();
    El* framedStack = framed->first->first;
    utassertnear(framedStack->first->style.pad.left, 12.f);

    El* ghost =
        Message::New(&cx)
            ->Header(MessageHeader::New(&cx)->Child(TextEl(a, StrL("System"))))
            ->Content(MessageContent::New(&cx)->WithBubble(
                Bubble::New(&cx)->WithVariant(BubbleVariant::Ghost)))
            ->IntoEl();
    El* ghostStack = ghost->first->first;
    utassertnear(ghostStack->first->style.pad.left, 0.f);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// The row the message lays out: the avatar sits beside the content column and
// the footer lives outside that row, indented past the avatar's baseline.
static void TheFooterSitsOutsideTheAvatarRow() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    El* root =
        Message::New(&cx)
            ->Avatar(Div(a))
            ->Content(MessageContent::New(&cx)->WithBubble(Bubble::New(&cx)))
            ->Footer(MessageFooter::New(&cx)->Child(TextEl(a, StrL("Read"))))
            ->IntoEl();
    El* row = root->first;
    El* footer = row->next;
    utassert(footer != nullptr);
    // rems(2.5) at the 16px root: the avatar's size-8 plus the row gap.
    utassertnear(footer->style.margin.left, 40.f);
    utassert(row->style.align == FlexAlign::End);
    utassert(row->style.dir == FlexDir::Row);

    El* trailing =
        Message::New(&cx)
            ->Alignment(MessageAlignment::End)
            ->Avatar(Div(a))
            ->Content(MessageContent::New(&cx)->WithBubble(Bubble::New(&cx)))
            ->Footer(MessageFooter::New(&cx)->Child(TextEl(a, StrL("Sent"))))
            ->IntoEl();
    utassert(trailing->first->style.dir == FlexDir::RowReverse);
    utassertnear(trailing->first->next->style.margin.right, 40.f);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

void TestMessage() {
    TestSuite("message");
    TheBuilderCarriesAlignmentAndEverySlot();
    AGhostBubbleRemovesTheMetadataInsets();
    TheFooterSitsOutsideTheAvatarRow();
}
