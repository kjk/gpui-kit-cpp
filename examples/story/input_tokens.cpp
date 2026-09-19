#include "Story.h"

#include <string.h>

struct TokenExample {
    InputState field;
    InputContent saved;
    bool readonly = false;
    bool disabled = false;
    bool multiline = false;
    bool seeded = false;
    char status[192] = {};

    ~TokenExample();
    static El* Render(TokenExample* self, Ctx* cx);
    static void OnInsert(TokenExample* self, Ctx* cx, const ClickEvent*,
                         intptr_t kind);
    static void OnSend(TokenExample* self, Ctx* cx, const ClickEvent*);
    static void OnSave(TokenExample* self, Ctx* cx, const ClickEvent*);
    static void OnRestore(TokenExample* self, Ctx* cx, const ClickEvent*);
    static void OnReadonly(TokenExample* self, Ctx* cx, const ClickEvent*,
                           intptr_t checked);
    static void OnDisabled(TokenExample* self, Ctx* cx, const ClickEvent*,
                           intptr_t checked);
};

EntityId TokenExampleNew(App* app, bool multiline) {
    Entity<TokenExample> entity = EntityNew<TokenExample>(app);
    if (TokenExample* t = entity.Get(app)) {
        t->multiline = multiline;
    }
    return entity.id;
}

// crates/story/src/stories/input_tokens.rs.

enum {
    RefCommand = 0,
    RefImage,
    RefSkill,
    RefPerson,
    RefCount
};

struct TokenSample {
    const char* id;
    const char* text;
    const char* label;
    const char* insertId;
    const char* insertLabel;
    IconName icon;
};

static const TokenSample kSamples[RefCount] = {
    {"command:commit-pr", "/commit-pr", "/commit-pr", "insert-command",
     "Insert command", IconName::SquareTerminal},
    {"image:1", "[Image 1]", "Image 1", "insert-image", "Attach image",
     IconName::Image},
    {"skill:gpui-kit", "$gpui-kit", "gpui-kit", "insert-skill", "Insert skill",
     IconName::Sparkles},
    {"person:alice", "@alice", "Alice", "insert-person", "Mention someone",
     IconName::AtSign},
};

static InlineToken SampleToken(int kind) {
    const TokenSample& s = kSamples[kind];
    return InlineToken::New(Str(s.id), Str(s.text)).WithLabel(Str(s.label));
}

static InputContent SampleContent(bool multiline) {
    const char* text =
        multiline
            ? "/commit-pr for the token composer, then ask @alice to review "
              "[Image 1] against $gpui-kit 🙂\nSelect part of a reference, "
              "delete it, and undo."
            : "Ask @alice to review [Image 1] 🙂";
    InputContent content = InputContent::New(Str(text));
    int kinds[RefCount] = {RefCommand, RefImage, RefSkill, RefPerson};
    int n = multiline ? RefCount : 2;
    if (!multiline) {
        kinds[0] = RefPerson;
        kinds[1] = RefImage;
    }
    for (int i = 0; i < n; i++) {
        InlineToken tok = SampleToken(kinds[i]);
        const char* found = strstr(text, kSamples[kinds[i]].text);
        int start = found ? (int)(found - text) : 0;
        content.WithToken(start, start + (int)strlen(kSamples[kinds[i]].text),
                          tok);
    }
    return content;
}

static void SetStatus(TokenExample* self, Str s) {
    int n = len(s);
    if (n > (int)sizeof(self->status) - 1) {
        n = (int)sizeof(self->status) - 1;
    }
    if (n > 0 && s.s) {
        memcpy(self->status, s.s, (size_t)n);
    }
    self->status[n] = 0;
}

static void Seed(TokenExample* self) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;
    if (self->multiline) {
        self->field.kind = InputKind::Textarea;
        self->field.mode.kind = LayoutModeKind::AutoGrow;
        self->field.mode.minRows = 2;
        self->field.mode.maxRows = 6;
        LayoutModeSetRows(&self->field.mode, 2);
    }
    InputContent content = SampleContent(self->multiline);
    InputSetValue(&self->field, content);
    InputContentFree(&self->saved);
    self->saved = InputGetContent(&self->field);
    VecReset(content.tokens);
}

static bool EndsWithNonSpace(Str text, int start) {
    if (start <= 0) {
        return false;
    }
    int prev = Utf8Prev(text, start);
    uint32_t c = 0;
    Utf8At(text, prev, &c);
    return c != ' ' && c != '\t' && c != '\n' && c != '\r';
}

static El* RenderToken(Ctx* cx, const InlineTokenContext* ctx, void*) {
    component::InputToken* token = component::InputToken::New(cx, *ctx);
    const InlineToken& t = ctx->Token();
    if (StrStartsWith(t.id, StrL("command:"))) {
        token->Icon(IconName::SquareTerminal);
    } else if (StrStartsWith(t.id, StrL("image:"))) {
        token->Icon(IconName::Image);
    } else if (StrStartsWith(t.id, StrL("skill:"))) {
        token->Icon(IconName::Sparkles);
    } else if (StrStartsWith(t.id, StrL("person:"))) {
        token->Icon(IconName::AtSign);
    }
    return token->IntoEl();
}

static void OnOpen(const InlineTokenClickEvent* ev, Ctx* cx, void* user) {
    TokenExample* self = (TokenExample*)user;
    if (!self || !ev) {
        return;
    }
    const InlineToken& token = ev->Token();
    if (StrStartsWith(token.id, StrL("command:"))) {
        SetStatus(self, StrDup(cx->a, fmt("Would run %s", token.text)));
    } else if (StrStartsWith(token.id, StrL("image:"))) {
        SetStatus(self, StrDup(cx->a, fmt("Would preview %s", token.label)));
    } else if (StrStartsWith(token.id, StrL("skill:"))) {
        SetStatus(self,
                  StrDup(cx->a, fmt("Would open the %s skill", token.label)));
    } else if (StrStartsWith(token.id, StrL("person:"))) {
        SetStatus(self,
                  StrDup(cx->a, fmt("Would open %s's profile", token.label)));
    } else {
        SetStatus(self, StrDup(cx->a, fmt("Unknown reference %s", token.id)));
    }
    if (cx) {
        Notify(cx);
    }
}

static void Describe(Arena* a, const InputContent& content, StrBuilder& out) {
    (void)a;
    for (int i = 0; i < content.tokens.len; i++) {
        if (i > 0) {
            out.Append(StrL(", "));
        }
        const InlineTokenSpan& span = content.tokens[i];
        out.Append(span.token.id);
        out.Append(StrDup(a, fmt(" [%d..%d)", span.start, span.end)));
    }
}

TokenExample::~TokenExample() {
    InputContentFree(&saved);
}

void TokenExample::OnInsert(TokenExample* self, Ctx* cx, const ClickEvent*,
                            intptr_t kind) {
    if (kind < 0 || kind >= RefCount || self->readonly || self->disabled) {
        return;
    }
    int start = self->field.selectedRange.start;
    Str text = InputValue(&self->field);
    if (EndsWithNonSpace(text, start)) {
        InputReplaceTextInRange(&self->field, cx->app, cx->win, nullptr,
                                StrL(" "));
    }
    InputReplaceWithToken(&self->field, cx->app, cx->win,
                          SampleToken((int)kind));
    InputReplaceTextInRange(&self->field, cx->app, cx->win, nullptr, StrL(" "));
    InputFocus(&self->field, cx->app, cx->win);
    Notify(cx);
}

void TokenExample::OnSend(TokenExample* self, Ctx* cx, const ClickEvent*) {
    InputContent content = InputGetContent(&self->field);
    int n = len(content.text);
    while (n > 0) {
        char c = content.text.s[n - 1];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            break;
        }
        n--;
    }
    SetStatus(self,
              StrDup(cx->a, fmt("Sent \"%s\" with %d references",
                                Str(content.text.s, n), content.tokens.len)));
    InputContentFree(&content);
    Notify(cx);
}

void TokenExample::OnSave(TokenExample* self, Ctx* cx, const ClickEvent*) {
    InputContentFree(&self->saved);
    self->saved = InputGetContent(&self->field);
    SetStatus(self, StrL("Draft saved"));
    Notify(cx);
}

void TokenExample::OnRestore(TokenExample* self, Ctx* cx, const ClickEvent*) {
    if (self->readonly || self->disabled) {
        return;
    }
    InputSetValue(&self->field, self->saved);
    SetStatus(self, StrL("Draft restored"));
    Notify(cx);
}

void TokenExample::OnReadonly(TokenExample* self, Ctx* cx, const ClickEvent*,
                              intptr_t checked) {
    self->readonly = checked != 0;
    Notify(cx);
}

void TokenExample::OnDisabled(TokenExample* self, Ctx* cx, const ClickEvent*,
                              intptr_t checked) {
    self->disabled = checked != 0;
    Notify(cx);
}

static El* Readout(TokenExample* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    InputContent content = InputGetContent(&self->field);
    auto row = [&](const char* label, Str value) {
        return Div(a)
            ->FlexRow()
            ->ItemsStart()
            ->Gap(12)
            ->Child(Div(a)
                        ->Shrink0()
                        ->W(88)
                        ->Fg(th.mutedFg)
                        ->Child(TextEl(a, Str(label))))
            ->Child(Div(a)->MinW(0)->Flex1()->Child(TextEl(a, value)->Wrap()));
    };
    StrBuilder refs(a);
    Describe(a, content, refs);
    El* col = Div(a)->FlexCol()->W(kFill)->Gap(4);
    col->Child(row("Text", StrDup(a, content.text)));
    col->Child(row("References", refs.TakeStr()));
    if (self->status[0]) {
        col->Child(row("Status", Str(self->status)));
    }
    InputContentFree(&content);
    return col;
}

static component::InputGroupButton* InsertButton(TokenExample* self, Ctx* cx,
                                                 int kind) {
    const TokenSample& s = kSamples[kind];
    return component::InputGroupButton::New(cx, Str(s.insertId))
        ->Icon(s.icon)
        ->AriaLabel(Str(s.insertLabel))
        ->Tooltip(Str(s.insertLabel))
        ->Disabled(self->readonly)
        ->OnClick(Listen(cx, &TokenExample::OnInsert, kind));
}

El* TokenExample::Render(TokenExample* self, Ctx* cx) {
    Seed(self);
    Arena* a = cx->a;
    component::InputGroup* group =
        component::InputGroup::New(cx, StrL("token-composer"))
            ->Readonly(self->readonly)
            ->Disabled(self->disabled);
    if (self->multiline) {
        component::Textarea* area =
            component::Textarea::New(cx, StrL("Message"), &self->field)
                ->AriaLabel(StrL("Message"))
                ->Token(&RenderToken)
                ->OnTokenClick(&OnOpen, self)
                ->Rows(6);
        group->Input(area);
        component::InputGroupAddon* addon =
            component::InputGroupAddon::New(cx, StrL("composer-actions"))
                ->Align(component::InputGroupAddonAlignment::BlockEnd);
        for (int i = 0; i < RefCount; i++) {
            addon->Child(InsertButton(self, cx, i)->IntoEl());
        }
        El* send = component::InputGroupButton::New(cx, StrL("send-message"))
                       ->WithVariant(component::ButtonVariant::Primary)
                       ->Label(StrL("Send"))
                       ->OnClick(Listen(cx, &TokenExample::OnSend))
                       ->IntoEl();
        send->MarginL(kAuto);
        addon->Child(send);
        group->Addon(addon);
    } else {
        component::Input* input =
            component::Input::New(cx, StrL("Message"), &self->field)
                ->AriaLabel(StrL("Message"))
                ->Token(&RenderToken)
                ->OnTokenClick(&OnOpen, self);
        group->Input(input);
        component::InputGroupAddon* addon =
            component::InputGroupAddon::New(cx, StrL("composer-actions"))
                ->Align(component::InputGroupAddonAlignment::InlineEnd);
        for (int i = 0; i < RefCount; i++) {
            addon->Child(InsertButton(self, cx, i)->IntoEl());
        }
        addon->Child(component::InputGroupButton::New(cx, StrL("send-message"))
                         ->WithVariant(component::ButtonVariant::Primary)
                         ->Label(StrL("Send"))
                         ->OnClick(Listen(cx, &TokenExample::OnSend))
                         ->IntoEl());
        group->Addon(addon);
    }

    El* page = Div(a)->FlexCol()->W(kFill)->Gap(16);
    page->Child(group->IntoEl());
    El* tools = Div(a)->FlexRow()->Gap(24);
    El* drafts = Div(a)->FlexRow()->Gap(8);
    drafts->Child(component::Button::New(cx, StrL("save-draft"))
                      ->Outline()
                      ->WithSize(UiSize::Small)
                      ->Label(StrL("Save draft"))
                      ->OnClick(Listen(cx, &TokenExample::OnSave))
                      ->IntoEl());
    drafts->Child(component::Button::New(cx, StrL("restore-draft"))
                      ->Outline()
                      ->WithSize(UiSize::Small)
                      ->Label(StrL("Restore draft"))
                      ->Disabled(self->readonly || self->disabled)
                      ->OnClick(Listen(cx, &TokenExample::OnRestore))
                      ->IntoEl());
    tools->Child(drafts);
    El* flags = Div(a)->FlexRow()->Gap(16);
    flags->Child(component::Checkbox::New(cx, StrL("tokens-readonly"))
                     ->WithSize(UiSize::Small)
                     ->Label(StrL("Readonly"))
                     ->Checked(self->readonly)
                     ->OnClick(Listen(cx, &TokenExample::OnReadonly))
                     ->IntoEl());
    flags->Child(component::Checkbox::New(cx, StrL("tokens-disabled"))
                     ->WithSize(UiSize::Small)
                     ->Label(StrL("Disabled"))
                     ->Checked(self->disabled)
                     ->OnClick(Listen(cx, &TokenExample::OnDisabled))
                     ->IntoEl());
    tools->Child(flags);
    page->Child(tools);
    page->Child(Readout(self, cx));
    return page;
}
