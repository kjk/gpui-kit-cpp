// crates/base/benches/text_view_scroll.rs text_view_append_long_paragraph.
#define GPUI_INCLUDE_PRIVATE_API 1
#include "Bench.h"

static constexpr Str kParagraphChunk =
    StrL("Some prose with `inline code` and **bold text** here. ");

struct TextViewFrameBench {
    Entity<TextViewState> state;

    static El* Render(TextViewFrameBench* self, Ctx* cx) {
        return TextView::New(cx, self->state)->IntoEl()->W(480);
    }
};

struct TextViewFrameSample {
    App* app = nullptr;
    Window* win = nullptr;
    TextViewState* state = nullptr;
    Str paragraph;
};

static void ResetLongParagraph(TextViewFrameSample* sample) {
    sample->state->SetText(sample->paragraph, sample->app, sample->win);
    TestRunUntilParked(sample->app);
}

static void AppendLongParagraph(TextViewFrameSample* sample) {
    sample->state->PushStr(kParagraphChunk, sample->app, sample->win);
    TestRunUntilParked(sample->app);
}

void BenchTextViewFrames() {
    const char* group = "text_view";
    const char* name = "append_long_paragraph";
    if (!BenchWanted(group, name)) {
        return;
    }

    Arena* arena = ArenaNew();
    StrBuilder source(arena);
    for (int i = 0; i < 500; i++) {
        source.Append(kParagraphChunk);
    }
    Str paragraph = source.TakeStr();
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<TextViewFrameBench>(app);
    auto state = TextViewState::Markdown(app, paragraph);
    view.Get(app)->state = state;
    Window* win = TestWindowOpen(app, view, 480, 600);
    TextViewFrameSample sample = {app, win, state.Get(app), paragraph};
    BenchCase(group, name, "bytes", len(paragraph),
              MkFunc0(ResetLongParagraph, &sample),
              MkFunc0(AppendLongParagraph, &sample));
    TestAppFree(app);
    ArenaDelete(arena);
}
