#include "Story.h"

// editor_preview.rs, the sample the Rust story loads into its editor.
static const char* kEditorCode =
    "use gpui::{Context, IntoElement, ParentElement, Render, Styled, Window, "
    "div};\n"
    "use gpui_component::{ActiveTheme, Icon, IconName, StyledExt, h_flex, "
    "progress::Progress, v_flex};\n"
    "\n"
    "pub struct ProjectOverview {\n"
    "    progress: f32,\n"
    "}\n"
    "\n"
    "impl ProjectOverview {\n"
    "    pub fn new() -> Self {\n"
    "        Self { progress: 72. }\n"
    "    }\n"
    "\n"
    "    fn metric(&self, label: &'static str, value: &'static str) -> impl "
    "IntoElement {\n"
    "        v_flex()\n"
    "            .gap_1()\n"
    "            .p_4()\n"
    "            .rounded(cx.theme().radius_lg)\n"
    "            .border_1()\n"
    "            .child(div().text_sm().child(label))\n"
    "            .child(div().text_2xl().font_semibold().child(value))\n"
    "    }\n"
    "}\n"
    "\n"
    "impl Render for ProjectOverview {\n"
    "    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl "
    "IntoElement {\n"
    "        v_flex()\n"
    "            .size_full()\n"
    "            .gap_5()\n"
    "            .p_6()\n"
    "            .child(\n"
    "                h_flex()\n"
    "                    .items_center()\n"
    "                    .justify_between()\n"
    "                    .child(\n"
    "                        v_flex()\n"
    "                            .gap_1()\n"
    "                            "
    ".child(div().text_2xl().font_semibold().child(\"Project overview\"))\n"
    "                            .child(div().text_sm().child(\"Everything is "
    "moving on schedule.\")),\n"
    "                    )\n"
    "                    .child(Icon::new(IconName::ChartNoAxesCombined)),\n"
    "            )\n"
    "            .child(\n"
    "                h_flex()\n"
    "                    .gap_3()\n"
    "                    .child(self.metric(\"Open tasks\", \"24\"))\n"
    "                    .child(self.metric(\"Completed\", \"86%\"))\n"
    "                    .child(self.metric(\"Contributors\", \"12\")),\n"
    "            )\n"
    "            .child(\n"
    "                v_flex()\n"
    "                    .gap_3()\n"
    "                    .p_4()\n"
    "                    .rounded(cx.theme().radius_lg)\n"
    "                    .bg(cx.theme().muted)\n"
    "                    .child(\n"
    "                        h_flex()\n"
    "                            .justify_between()\n"
    "                            .child(\"Release progress\")\n"
    "                            .child(format!(\"{}%\", self.progress)),\n"
    "                    )\n"
    "                    "
    ".child(Progress::new(\"release\").value(self.progress)),\n"
    "            )\n"
    "    }\n"
    "}\n"
    "\n";

// The decorations tab shows a short document instead, with four
// TextDecorations hung off it. Weight and slant are not among the things a
// span can change — every run of a line shares one shaped layout — so the
// bold and the italic Rust also asks for are not drawn; the colour, the wash
// and the wavy rule are. The last two paragraphs carry range decorations, a
// fill and a frame, which follow edits.
static const char* kDecorationText =
    "Decoration styles\n"
    "Color highlights important text.\n"
    "Italic adds emphasis.\n"
    "Underline marks review text.\n"
    "\n"
    "Fill: marks a tracked range.\n"
    "\n"
    "Frame: outlines a tracked range.";

struct EditorStory {
    int tab = 0;
    bool readOnly = false;
    // One EditorState per tab, the way the Rust story keeps one per document.
    InputState code;
    InputState decorations;
    // create_range_decorations_collection: geometry is a separate owner
    // from text styling. Both follow edits, including newlines inserted
    // before these ranges.
    RangeDecorationCollection rangeDecorations;
    bool seeded = false;

    static El* Render(EditorStory* self, Ctx* cx);
};

static void SetEditorTab(EditorStory* self, Ctx* cx, const ClickEvent*,
                         intptr_t ix) {
    self->tab = (int)ix;
    Notify(cx);
}
static void ToggleReadOnly(EditorStory* self, Ctx* cx, const ClickEvent*) {
    self->readOnly = !self->readOnly;
    Notify(cx);
}

El* EditorStory::Render(EditorStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->code.kind = InputKind::Editor;
        self->decorations.kind = InputKind::Editor;
        InputSetValue(&self->code, Str(kEditorCode));
        InputSetValue(&self->decorations, Str(kDecorationText));
        Str text = Str(kDecorationText);
        Str fill = StrL("marks a tracked range.");
        Str frame = StrL("outlines a tracked range.");
        int fillStart = std::max(0, StrFind(text, fill));
        int frameStart = std::max(0, StrFind(text, frame));
        RangeDecoration ranges[2] = {
            RangeDecoration::New({fillStart, fillStart + len(fill)})
                .WithStyle(RangeDecorationStyle::Fill),
            RangeDecoration::New({frameStart, frameStart + len(frame)}),
        };
        self->rangeDecorations = InputCreateRangeDecorationsCollection(
            &self->decorations, ranges, 2);
    }
    self->code.readonly = self->readOnly;
    self->decorations.readonly = self->readOnly;
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);

    // The tab bar on the left, the read-only switch on the right.
    El* head = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
    // TabBar::new(..).w_64().underline()
    // TabBar::new(..).w_64(): the bar is 256 wide, not the row it sits in.
    head->Child(Div(a)->Child(component::Tabs::New(cx)
                                  ->W(256)
                                  ->Underline()
                                  ->Tab(StrL("Code"))
                                  ->Tab(StrL("Decorations"))
                                  ->Selected(self->tab)
                                  ->OnChange(Listen(cx, &SetEditorTab))
                                  ->IntoEl()));
    head->Child(component::Switch::New(cx, StrL("editor-read-only"))
                    ->Label(StrL("Read only"))
                    ->Checked(self->readOnly)
                    ->OnClick(Listen(cx, &ToggleReadOnly))
                    ->IntoEl());
    page->Child(head);

    El* box =
        Div(a)->FlexCol()->W(kFill)->Radius(th.radius)->Border(1, th.border);
    // The editor owns the box its rows scroll inside, so the caret can bring
    // the view with it.
    component::Editor* ed = component::Editor::New(
        cx, StrL("editor"), self->tab == 0 ? &self->code : &self->decorations);
    ed->H(WindowSize(cx->win).dipH - 262)->ActiveLine()->IndentGuides();
    if (self->tab == 0) {
        // The Rust tab is the code editor, which is where folding is on
        // upstream: a chevron in the gutter beside every brace block.
        ed->Language(StrL("rust"))->Folding();
    } else {
        // create_decorations_collection: the four runs, found in the text by
        // the words they cover, as Rust finds them.
        Str text = Str(kDecorationText);
        static const Str kWords[4] = {StrL("Decoration styles"), StrL("Color"),
                                      StrL("Italic"), StrL("Underline")};
        auto* runs = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * 4);
        int n = 0;
        for (int i = 0; i < 4; i++) {
            int at = StrFind(text, kWords[i]);
            if (at < 0) {
                continue;
            }
            TextSpan& sp = runs[n];
            sp.lo = at;
            sp.hi = sp.lo + kWords[i].len;
            sp.bg = Rgba8(0, 0, 0, 0);
            sp.underline = false;
            switch (i) {
                case 0:
                    sp.color = th.danger;
                    sp.bg = RgbaOpacity(th.warning, 0.2f);
                    break;
                case 1:
                    sp.color = th.success;
                    break;
                case 2:
                    sp.color = th.info;
                    break;
                default:
                    sp.color = th.warning;
                    sp.underline = true;
                    break;
            }
            n++;
        }
        ed->Decorations(runs, n);
    }
    box->Child(ed->IntoEl());
    page->Child(box);
    return page;
}

STORY_PAGE(StoryEditor, EditorStory);
