#include "Story.h"

// ToggleOption: this page's Options dropdown.
enum {
    DlActVertical = 3200,
    DlActBordered
};

struct DescriptionListStory {
    StoryToolbarState toolbar;
    bool vertical = false;
    bool bordered = true;

    static El* Render(DescriptionListStory* self, Ctx* cx);
};

static void OnDlOption(DescriptionListStory* self, Ctx* cx, const ClickEvent*,
                       int64_t act) {
    if (act == DlActVertical) {
        self->vertical = !self->vertical;
    } else if (act == DlActBordered) {
        self->bordered = !self->bordered;
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    Notify(cx);
}

// Every value is `TextView::markdown(ix, value)` in Rust, so the description's
// bold names and the two links come out of the one rich-text view.
static El* Md(Ctx* cx, Str src) {
    return component::TextView::New(cx, src)->Font(14)->IntoEl();
}

El* DescriptionListStory::Render(DescriptionListStory* self, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Pad(16)->Gap(24)->W(kFill)->ItemsCenter();

    StoryToolbarOpt opts[2] = {
        {"Vertical", self->vertical, DlActVertical},
        {"Bordered", self->bordered, DlActBordered},
    };
    page->Child(
        StoryToolbarOptions(cx, self, opts, 2, Listen(cx, &OnDlOption)));

    El* list =
        component::DescriptionList::New(cx)
            ->Columns(3)
            ->Vertical(self->vertical)
            ->Bordered(self->bordered)
            ->WithSize(self->toolbar.size)
            ->ItemEl(StrL("Name"), Md(cx, StrL("GPUI Component")))
            ->ItemEl(
                StrL("Description"),
                Md(cx, StrL("UI components for building fantastic desktop "
                            "application by using GPUI.\n\n"
                            "Contains a lot of useful UI components, such as "
                            "**Button**, **Input**, **Table**, **List**, "
                            "**Select**, **DatePicker** ...\n\n"
                            "You can easily create your native desktop "
                            "application by using GPUI Component.")),
                3)
            ->ItemEl(StrL("Version"), Md(cx, StrL("0.1.0")))
            ->ItemEl(StrL("License"), Md(cx, StrL("Apache-2.0")))
            ->ItemEl(StrL("Author"), Md(cx, StrL("Longbridge")))
            ->Separator()
            ->ItemEl(StrL("Repository"),
                     Md(cx, StrL("https://github.com/longbridge/"
                                 "gpui-kit")),
                     2)
            ->ItemEl(StrL("Category"), Md(cx, StrL("UI, Desktop, Framework")))
            ->ItemEl(StrL("This is a long label for Platform"),
                     Md(cx, StrL("macOS, Windows, Linux")))
            ->IntoEl();
    page->Child(Div(a)->W(720)->Child(list));
    return page;
}

STORY_PAGE(StoryDescriptionList, DescriptionListStory);
