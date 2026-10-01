#include "Story.h"

#include <stdio.h>

static const char* kTextareaText =
    "Hello 世界，this is GPUI component.\n"
    "\n"
    "The GPUI Component is a collection of UI components for GPUI framework, "
    "including.\n"
    "\n"
    "Button, Input, Checkbox, Radio, Dropdown, Tab, and more...\n"
    "\n"
    "Here is an application that is built by using GPUI Component.\n"
    "\n"
    "> This application is still under development, not published yet.\n"
    "\n"
    "![image](https://github.com/user-attachments/assets/"
    "559a648d-19df-4b5a-b563-b78cc79c8894)\n"
    "\n"
    "![image](https://github.com/user-attachments/assets/"
    "5e06ad5d-7ea0-43db-8d13-86a240da4c8d)\n"
    "\n"
    "## Demo\n"
    "\n"
    "If you want to see the demo, here is a some demo applications.\n";

static const char* kNoWrapText =
    "This is a very long line of text to test if the horizontal scrolling "
    "function is working properly, and it should not wrap automatically but "
    "display a horizontal scrollbar.\n"
    "The second line is also very long text, used to test the horizontal "
    "scrolling effect under multiple lines, and you can input more content "
    "to test.\n"
    "The third line: Here you can input other long text content that "
    "requires horizontal scrolling.\n";

static const char* kAutoGrowText =
    "Hello 世界 this is a very long line of text to test if the horizontal "
    "scrolling function is working properly, and it should not wrap "
    "automatically but display a horizontal scrollbar.\n"
    "The second line is also very long text, used to test the horizontal "
    "scrolling effect under multiple lines, and you can input more content "
    "to test.\n"
    "The third line: Here you can input other long text content that "
    "requires horizontal scrolling.\n";

// ComposerAttachment: one pasted image, as the pill above the composer
// shows it. Both strings are owned.
struct ComposerAttachment {
    uint64_t id;
    Str title;
    Str detail;
};

// One TextareaState per section, the way the Rust story makes one per
// example. `submitOnEnter` is what makes the chat box send on a plain Enter.
struct TextareaStory {
    InputState notes;
    InputState noWrap;
    InputState autoGrow;
    InputState both;
    InputState chat;
    // What the chat box sent, oldest first. Owned.
    Vec<Str> chatMessages;
    InputState composer;
    Vec<ComposerAttachment> attachments;
    // Image::id is a content hash in Rust, so pasting the same image twice
    // would collide; the story counts instead.
    uint64_t nextAttachmentId = 0;
    // story_toolbar(self.size): every textarea on the page takes the size.
    StoryToolbarState toolbar;
    bool seeded = false;
    EntityId tokens;

    static El* Render(TextareaStory* self, Ctx* cx);
};

// on_insert_text_to_textarea / on_replace_text_to_textarea.
static void OnInsertText(TextareaStory* self, Ctx* cx, const ClickEvent*) {
    InputInsert(&self->notes, cx->app, cx->win, StrL("Hello 你好"));
    Notify(cx);
}

static void OnReplaceText(TextareaStory* self, Ctx* cx, const ClickEvent*) {
    InputReplace(&self->notes, cx->app, cx->win, StrL("Hello 你好"));
    Notify(cx);
}

// The chat box's PressEnter without Shift: keep the trimmed text as a
// message and empty the box.
static void OnChatEvent(TextareaStory* self, Ctx* cx, const InputEvent* ev) {
    if (!ev || ev->kind != InputEventKind::PressEnter || ev->shift) {
        return;
    }
    Str text = StrTrimAscii(InputValue(&self->chat));
    if (len(text) == 0) {
        return;
    }
    VecAppend(self->chatMessages, StrDup(text));
    InputSetValue(&self->chat, Str{});
    Notify(cx);
}

// ImageFormat's Debug name, read off the encoded stream's signature: what
// Rust's ClipboardEntry::Image carries as `format`.
static const char* PastedImageFormat(const uint8_t* b, int n) {
    if (n >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        return "Png";
    }
    if (n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) {
        return "Jpeg";
    }
    if (n >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBP", 4) == 0) {
        return "Webp";
    }
    if (n >= 6 && memcmp(b, "GIF8", 4) == 0) {
        return "Gif";
    }
    if (n >= 2 && b[0] == 'B' && b[1] == 'M') {
        return "Bmp";
    }
    if (n >= 4 &&
        ((b[0] == 'I' && b[1] == 'I') || (b[0] == 'M' && b[1] == 'M'))) {
        return "Tiff";
    }
    if (n >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0) {
        return "Ico";
    }
    return "Svg";
}

// Textarea::on_paste: an image becomes an attachment pill and the paste is
// consumed; anything else falls through to ordinary text insertion.
static bool OnComposerPaste(void* data, const ClipboardItem& item, App* app,
                            Window* win) {
    TextareaStory* self = (TextareaStory*)data;
    if (!item.HasImage()) {
        return false;
    }
    uint64_t id = self->nextAttachmentId++;
    ComposerAttachment att;
    att.id = id;
    char buf[96];
    snprintf(buf, sizeof(buf), "pasted-image-%llu.png", (unsigned long long)id);
    att.title = StrDup(Str(buf));
    snprintf(buf, sizeof(buf), "%s - %d bytes",
             PastedImageFormat(item.imageBytes, item.imageBytesLen),
             item.imageBytesLen);
    att.detail = StrDup(Str(buf));
    VecAppend(self->attachments, att);
    (void)win;
    NotifyApp(app);
    return true;
}

static void OnRemovePasted(TextareaStory* self, Ctx* cx, const ClickEvent*,
                           intptr_t id) {
    for (int i = 0; i < self->attachments.len; i++) {
        if (self->attachments[i].id == (uint64_t)id) {
            StrFree(self->attachments[i].title);
            StrFree(self->attachments[i].detail);
            VecRemoveAt(self->attachments, i);
            break;
        }
    }
    Notify(cx);
}

El* TextareaStory::Render(TextareaStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        InputState* all[] = {&self->notes, &self->noWrap, &self->autoGrow,
                             &self->both, &self->chat};
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
            all[i]->kind = InputKind::Textarea;
        }
        InputSetValue(&self->notes, Str(kTextareaText));
        InputSetValue(&self->noWrap, Str(kNoWrapText));
        InputSetValue(&self->autoGrow, Str(kAutoGrowText));
        InputSetValue(&self->both, StrL("Hello 世界，this is GPUI component."));
        // auto_grow(1, 5).
        self->autoGrow.mode.kind = LayoutModeKind::AutoGrow;
        self->autoGrow.mode.minRows = 1;
        self->autoGrow.mode.maxRows = 5;
        self->chat.submitOnEnter = true;
        InputSetPlaceholder(&self->chat, StrL("Type a message, Enter to send, "
                                              "Shift+Enter for newline"));
        self->chat.onChange = Listen(cx, &OnChatEvent);
        self->composer.kind = InputKind::Textarea;
        self->composer.mode.kind = LayoutModeKind::AutoGrow;
        self->composer.mode.minRows = 1;
        self->composer.mode.maxRows = 5;
        InputSetPlaceholder(
            &self->composer,
            StrL("Paste a screenshot here, it becomes an attachment above"));
    }
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));
    UiSize size = self->toolbar.size;

    El* def = StorySection(cx, "Textarea", nullptr);
    StorySectionBody(def)->W(560);
    El* defCol = Div(a)->FlexCol()->W(560)->Gap(8);
    defCol->Child(component::Textarea::New(cx, StrL("notes"), &self->notes)
                      ->WithSize(size)
                      ->H(320)
                      ->IntoEl());
    // The action row: two xsmall outline buttons, and the cursor position at
    // the far end.
    El* actions = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
    El* btns = Div(a)->FlexRow()->Gap(8);
    btns->Child(component::Button::New(cx, StrL("btn-insert-text"))
                    ->Outline()
                    ->WithSize(UiSize::XSmall)
                    ->Label(StrL("Insert Text"))
                    ->OnClick(Listen(cx, &OnInsertText))
                    ->IntoEl());
    btns->Child(component::Button::New(cx, StrL("btn-replace-text"))
                    ->Outline()
                    ->WithSize(UiSize::XSmall)
                    ->Label(StrL("Replace Text"))
                    ->OnClick(Listen(cx, &OnReplaceText))
                    ->IntoEl());
    actions->Child(btns);
    RopePoint loc = InputCursorPosition(&self->notes);
    actions->Child(StoryTxt(cx, StoryFmt(cx, "%d:%d", loc.row, loc.column), 16,
                            th.foreground));
    defCol->Child(actions);
    StorySectionAdd(def, defCol);
    page->Child(def);

    El* nowrap = StorySection(cx, "No Wrap", nullptr);
    StorySectionBody(nowrap)->W(560);
    StorySectionAdd(
        nowrap, component::Textarea::New(cx, StrL("notes-nw"), &self->noWrap)
                    ->WithSize(size)
                    ->H(200)
                    ->SoftWrap(false)
                    ->IntoEl()
                    ->W(560));
    page->Child(nowrap);

    // auto_grow(1, 5): five rows once the text is long enough to fill them.
    El* grow = StorySection(cx, "Auto Grow", nullptr);
    StorySectionBody(grow)->W(560);
    StorySectionAdd(
        grow, component::Textarea::New(cx, StrL("notes-grow"), &self->autoGrow)
                  ->WithSize(size)
                  ->Rows(5)
                  ->IntoEl()
                  ->W(560));
    page->Child(grow);

    El* both = StorySection(cx, "Auto Grow with No Wrap", nullptr);
    StorySectionBody(both)->W(560);
    StorySectionAdd(
        both, component::Textarea::New(cx, StrL("notes-both"), &self->both)
                  ->WithSize(size)
                  ->Rows(1)
                  ->SoftWrap(false)
                  ->IntoEl()
                  ->W(560));
    page->Child(both);

    El* chat = StorySection(cx, "Submit on Enter (Chat)", nullptr);
    StorySectionBody(chat)->W(560);
    El* chatCol = Div(a)->FlexCol()->Gap(8)->W(kFill);
    El* msgs = Div(a)->FlexCol()->Gap(4);
    for (int i = 0; i < self->chatMessages.len; i++) {
        msgs->Child(
            Div(a)->PadX(8)->PadY(4)->Radius(th.radius)->Bg(th.muted)->Child(
                StoryTxt(cx, self->chatMessages[i], 16, th.foreground)));
    }
    chatCol->Child(msgs);
    chatCol->Child(component::Textarea::New(cx, StrL("chat"), &self->chat)
                       ->WithSize(size)
                       ->Rows(1)
                       ->IntoEl()
                       ->W(kFill));
    StorySectionAdd(chat, chatCol);
    page->Child(chat);

    El* paste = StorySection(
        cx, "Paste Images (Composer)",
        "Paste a screenshot, it lands as an attachment pill above.");
    StorySectionBody(paste)->W(560);
    El* pasteCol = Div(a)->FlexCol()->Gap(8)->W(kFill);
    if (self->attachments.len > 0) {
        component::AttachmentGroup* group =
            component::AttachmentGroup::New(cx, StrL("composer-attachments"));
        for (int i = 0; i < self->attachments.len; i++) {
            const ComposerAttachment& att = self->attachments[i];
            component::Attachment* pill =
                component::Attachment::New(cx)
                    ->Media(component::AttachmentMedia::New(cx)->Child(
                        component::Icon::New(cx, IconName::FileText)
                            ->Size(UiSize::Small)
                            ->IntoEl()))
                    ->Content(
                        component::AttachmentContent::New(cx)
                            ->Title(
                                component::AttachmentTitle::New(cx, att.title))
                            ->Description(component::AttachmentDescription::New(
                                cx, att.detail)))
                    ->Actions(component::AttachmentActions::New(cx)->Child(
                        component::Button::New(
                            cx, StoryFmt(cx, "remove-pasted-%llu",
                                         (unsigned long long)att.id))
                            ->Ghost()
                            ->WithSize(UiSize::XSmall)
                            ->Icon(IconName::Close)
                            ->OnClick(
                                Listen(cx, &OnRemovePasted, (intptr_t)att.id))
                            ->IntoEl()));
            group->Child(
                component::HoverCard::New(cx,
                                          StoryFmt(cx, "pasted-preview-%llu",
                                                   (unsigned long long)att.id))
                    ->Trigger(pill->IntoEl())
                    ->Content(StoryTxt(cx, att.detail, 14, th.foreground))
                    ->IntoEl());
        }
        pasteCol->Child(group->IntoEl());
    }
    pasteCol
        ->Child(component::Textarea::New(cx, StrL("composer"), &self->composer)
                    ->WithSize(size)
                    ->OnPaste(&OnComposerPaste, self)
                    ->Rows(5)
                    ->IntoEl()
                    ->W(kFill));
    StorySectionAdd(paste, pasteCol);
    page->Child(paste);

    if (!self->tokens.IsValid()) {
        self->tokens = TokenExampleNew(cx->app, true);
    }
    El* tokens = StorySection(
        cx, "Atomic inline tokens",
        "References keep their identity through selection, deletion and undo. "
        "Copy returns the underlying text.");
    StorySectionBody(tokens)->W(kFill);
    StorySectionAdd(tokens,
                    EntityRender(cx->app, cx->win, cx->a, self->tokens));
    page->Child(tokens);
    return page;
}

STORY_PAGE(StoryTextarea, TextareaStory);
