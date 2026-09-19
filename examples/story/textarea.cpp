#include "Story.h"

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

// One TextareaState per section, the way the Rust story makes one per
// example. `submitOnEnter` is what makes the chat box send on a plain Enter.
struct TextareaStory {
    InputState notes;
    InputState noWrap;
    InputState autoGrow;
    InputState both;
    InputState chat;
    bool seeded = false;
    EntityId tokens;

    static El* Render(TextareaStory* self, Ctx* cx);
};

static void NoOp(TextareaStory*, Ctx*, const ClickEvent*) {}

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
    }
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);

    El* def = StorySection(cx, "Textarea", nullptr);
    StorySectionBody(def)->W(560);
    El* defCol = Div(a)->FlexCol()->W(560)->Gap(8);
    defCol->Child(component::Textarea::New(cx, StrL("notes"), &self->notes)
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
                    ->OnClick(Listen(cx, &NoOp))
                    ->IntoEl());
    btns->Child(component::Button::New(cx, StrL("btn-replace-text"))
                    ->Outline()
                    ->WithSize(UiSize::XSmall)
                    ->Label(StrL("Replace Text"))
                    ->OnClick(Listen(cx, &NoOp))
                    ->IntoEl());
    actions->Child(btns);
    actions->Child(StoryTxt(cx, StrL("0:0"), 16, th.foreground));
    defCol->Child(actions);
    StorySectionAdd(def, defCol);
    page->Child(def);

    El* nowrap = StorySection(cx, "No Wrap", nullptr);
    StorySectionBody(nowrap)->W(560);
    StorySectionAdd(
        nowrap, component::Textarea::New(cx, StrL("notes-nw"), &self->noWrap)
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
                  ->Rows(5)
                  ->IntoEl()
                  ->W(560));
    page->Child(grow);

    El* both = StorySection(cx, "Auto Grow with No Wrap", nullptr);
    StorySectionBody(both)->W(560);
    StorySectionAdd(
        both, component::Textarea::New(cx, StrL("notes-both"), &self->both)
                  ->Rows(1)
                  ->SoftWrap(false)
                  ->IntoEl()
                  ->W(560));
    page->Child(both);

    El* chat = StorySection(cx, "Submit on Enter (Chat)", nullptr);
    StorySectionBody(chat)->W(560);
    StorySectionAdd(chat,
                    component::Textarea::New(cx, StrL("chat"), &self->chat)
                        ->Rows(1)
                        ->IntoEl()
                        ->W(560));
    page->Child(chat);

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
