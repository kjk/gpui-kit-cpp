#include "Story.h"

struct KbdStory {
    static El* Render(KbdStory* self, Ctx* cx);
};

// The keystrokes the Rust story builds. Kbd::format is what turns one into
// the spelling this platform uses — ⌃⌥⇧⌘ run together on macOS, and
// Ctrl+Alt+Shift+Win joined with a plus everywhere else.
static const component::Keystroke kStrokes[] = {
    {false, false, true, true, StrL("p")},
    {true, false, false, true, StrL("t")},
    {false, false, false, true, StrL("-")},
    {false, false, false, true, StrL("+")},
    {false, false, false, false, StrL("escape")},
    {false, false, false, false, StrL("backspace")},
    {false, false, false, false, StrL("/")},
    {false, false, false, false, StrL("enter")},
};

El* KbdStory::Render(KbdStory*, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->W(kFill)->ItemsCenter()->Gap(24);
    El* def = StorySection(cx, "Default",
                           "Displays single keys and multi-key shortcuts.");
    StorySectionBody(def)->W(560);
    El* row = Div(a)->FlexRow()->W(kFill)->JustifyCenter()->Gap(8)->FlexWrap();
    for (int i = 0; i < 8; i++) {
        row->Child(component::Kbd::New(cx, kStrokes[i])->IntoEl());
    }
    StorySectionAdd(def, row);
    page->Child(def);

    El* out =
        StorySection(cx, "Outlined",
                     "An outlined treatment adds emphasis on dense surfaces.");
    StorySectionBody(out)->W(560);
    El* row2 = Div(a)->FlexRow()->W(kFill)->JustifyCenter()->Gap(8)->FlexWrap();
    for (int i = 0; i < 3; i++) {
        row2->Child(component::Kbd::New(cx, kStrokes[i == 2 ? 7 : i])
                        ->Outline()
                        ->IntoEl());
    }
    StorySectionAdd(out, row2);
    page->Child(out);

    El* labels = StorySection(cx, "Label",
                              "Shows text that is not a single keystroke, such "
                              "as a range or a choice of keys.");
    StorySectionBody(labels)->W(560);
    El* hints =
        Div(a)->FlexRow()->W(kFill)->JustifyCenter()->Gap(8)->FlexWrap();
    hints->Child(component::Kbd::Label(cx, StrL("Esc"))->IntoEl())
        ->Child(component::Kbd::Label(cx, StrL("⌘1–9"))->IntoEl())
        ->Child(component::Kbd::Label(cx, StrL("↑↓"))->IntoEl())
        ->Child(component::Kbd::Label(cx, StrL("Enter / Tab"))
                    ->Outline()
                    ->IntoEl());
    StorySectionAdd(labels, hints);
    page->Child(labels);

    return page;
}

STORY_PAGE(StoryKbd, KbdStory);
