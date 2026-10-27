#include "Story.h"

#include <string.h>

/* crates/story/src/stories/input_completion.rs.

   An application-owned completion menu around a single-line Input.
   Typing `$` opens a menu of shell-style variables. InputState carries
   no completion state: the candidates, the popup and the keys it
   captures all live here. */

struct VariableRow {
    const char* name;
    const char* description;
};

static const VariableRow kVariables[] = {
    {"EDITOR", "default editor"},  {"HOME", "user home directory"},
    {"LANG", "locale identifier"}, {"PATH", "executable search path"},
    {"PWD", "working directory"},  {"SHELL", "login shell"},
    {"TERM", "terminal type"},     {"USER", "current user name"},
};

static const int kVariableCount =
    (int)(sizeof(kVariables) / sizeof(kVariables[0]));

struct VariableQuery {
    bool ok = false;
    int tokenStart = 0;
    int queryBegin = 0;
    int queryEnd = 0;
};

static bool AsciiAlnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

static bool AsciiSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

static char AsciiLower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

// The `$` must open the token, at the start of the text or after
// whitespace, so `a$b` never triggers.
VariableQuery ActiveVariableQuery(Str text, int cursor) {
    VariableQuery q = {};
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > len(text)) {
        cursor = len(text);
    }
    int start = cursor;
    while (start > 0 && (AsciiAlnum((unsigned char)text.s[start - 1]) ||
                         text.s[start - 1] == '_')) {
        start--;
    }
    if (start == 0 || text.s[start - 1] != '$') {
        return q;
    }
    if (start >= 2 && !AsciiSpace((unsigned char)text.s[start - 2])) {
        return q;
    }
    q.ok = true;
    q.tokenStart = start - 1;
    q.queryBegin = start;
    q.queryEnd = cursor;
    return q;
}

Str VariableQueryText(Str text, const VariableQuery& q) {
    if (!q.ok || !text.s || q.queryBegin < 0 || q.queryEnd < q.queryBegin ||
        q.queryEnd > len(text)) {
        return {};
    }
    return Str(text.s + q.queryBegin, q.queryEnd - q.queryBegin);
}

static bool NameContains(const char* name, Str query) {
    int n = (int)strlen(name);
    int qn = len(query);
    if (qn > n) {
        return false;
    }
    for (int i = 0; i <= n - qn; i++) {
        int k = 0;
        while (k < qn && AsciiLower(name[i + k]) == AsciiLower(query.s[k])) {
            k++;
        }
        if (k == qn) {
            return true;
        }
    }
    return false;
}

static bool NameStarts(const char* name, Str query) {
    int qn = len(query);
    for (int k = 0; k < qn; k++) {
        if (name[k] == 0 || AsciiLower(name[k]) != AsciiLower(query.s[k])) {
            return false;
        }
    }
    return true;
}

static void SortByName(int* ix, int n) {
    for (int i = 1; i < n; i++) {
        int v = ix[i];
        int j = i;
        while (j > 0 &&
               strcmp(kVariables[ix[j - 1]].name, kVariables[v].name) > 0) {
            ix[j] = ix[j - 1];
            j--;
        }
        ix[j] = v;
    }
}

// Prefix matches first, then substring matches, each alphabetical.
int RankVariables(Str query, int* out, int cap) {
    int prefix[8];
    int contained[8];
    int np = 0;
    int nc = 0;
    for (int i = 0; i < kVariableCount; i++) {
        if (!NameContains(kVariables[i].name, query)) {
            continue;
        }
        if (NameStarts(kVariables[i].name, query)) {
            prefix[np++] = i;
        } else {
            contained[nc++] = i;
        }
    }
    SortByName(prefix, np);
    SortByName(contained, nc);
    int n = 0;
    for (int i = 0; i < np && n < cap; i++) {
        out[n++] = prefix[i];
    }
    for (int i = 0; i < nc && n < cap; i++) {
        out[n++] = contained[i];
    }
    return n;
}

const char* VariableName(int ix) {
    if (ix < 0 || ix >= kVariableCount) {
        return "";
    }
    return kVariables[ix].name;
}

struct CompletionExample {
    InputState field;
    bool open = false;
    int tokenStart = 0;
    int items[8] = {};
    int n = 0;
    int selected = 0;
    char status[192] = "Enter submits when no menu is open.";
    bool seeded = false;

    static El* Render(CompletionExample* self, Ctx* cx);
    static void OnInput(CompletionExample* self, Ctx* cx, const InputEvent* ev);
    static void OnCapture(CompletionExample* self, Ctx* cx, const KeyEvent* ev);
    static void OnRow(CompletionExample* self, Ctx* cx, const ClickEvent*,
                      int64_t ix);

    bool Rescan();
    bool MenuIsCurrent() const;
    bool OwnKey(Ctx* cx, const KeyEvent* ev);
    void Select(Ctx* cx, int delta, const KeyEvent* ev);
    void Accept(Ctx* cx);
    void Submit(Ctx* cx);
    void SetStatus(Str text);
};

EntityId CompletionExampleNew(App* app) {
    return EntityNew<CompletionExample>(app).id;
}

bool CompletionExample::Rescan() {
    VariableQuery trigger =
        ActiveVariableQuery(InputValue(&field), InputCursor(&field));
    if (!trigger.ok) {
        bool was = open;
        open = false;
        return was;
    }
    Str query = VariableQueryText(InputValue(&field), trigger);
    int ranked[8];
    int count = RankVariables(query, ranked, 8);
    if (count == 0) {
        bool was = open;
        open = false;
        return was;
    }
    int nextSelected = 0;
    if (open) {
        nextSelected = selected;
        if (nextSelected > count - 1) {
            nextSelected = count - 1;
        }
    }
    bool changed = !open || tokenStart != trigger.tokenStart || n != count ||
                   selected != nextSelected;
    if (!changed) {
        for (int i = 0; i < count; i++) {
            if (items[i] != ranked[i]) {
                changed = true;
                break;
            }
        }
    }
    open = true;
    tokenStart = trigger.tokenStart;
    n = count;
    selected = nextSelected;
    for (int i = 0; i < count; i++) {
        items[i] = ranked[i];
    }
    return changed;
}

bool CompletionExample::MenuIsCurrent() const {
    if (!open) {
        return false;
    }
    VariableQuery trigger =
        ActiveVariableQuery(InputValue(&field), InputCursor(&field));
    return trigger.ok && trigger.tokenStart == tokenStart;
}

bool CompletionExample::OwnKey(Ctx* cx, const KeyEvent* ev) {
    if (!open) {
        return false;
    }
    if (!MenuIsCurrent()) {
        open = false;
        Notify(cx);
        return false;
    }
    if (ev) {
        const_cast<KeyEvent*>(ev)->propagate = false;
    }
    return true;
}

void CompletionExample::Select(Ctx* cx, int delta, const KeyEvent* ev) {
    if (!OwnKey(cx, ev)) {
        return;
    }
    int next = selected + delta;
    if (next < 0) {
        next = 0;
    }
    if (n > 0 && next > n - 1) {
        next = n - 1;
    }
    selected = next;
    Notify(cx);
}

void CompletionExample::SetStatus(Str text) {
    int nCopy = len(text);
    if (nCopy > (int)sizeof(status) - 1) {
        nCopy = (int)sizeof(status) - 1;
    }
    if (nCopy > 0 && text.s) {
        memcpy(status, text.s, (size_t)nCopy);
    }
    status[nCopy] = 0;
}

void CompletionExample::Accept(Ctx* cx) {
    if (!open || selected < 0 || selected >= n) {
        open = false;
        Notify(cx);
        return;
    }
    int nameIx = items[selected];
    int start = tokenStart;
    open = false;
    Notify(cx);
    Str text = InputValue(&field);
    int cursor = InputCursor(&field);
    VariableQuery trigger = ActiveVariableQuery(text, cursor);
    if (!trigger.ok || trigger.tokenStart != start) {
        return;
    }
    const char* name = VariableName(nameIx);
    InputSetSelectedRange(&field, cx->app, cx->win, start, cursor);
    InputReplace(&field, cx->app, cx->win, fmt("$%s ", Str(name)));
    InputFocus(&field, cx);
    SetStatus(fmt("Completed $%s", Str(name)));
}

void CompletionExample::Submit(Ctx* cx) {
    SetStatus(fmt("Submitted \"%s\"", InputValue(&field)));
    Notify(cx);
}

void CompletionExample::OnInput(CompletionExample* self, Ctx* cx,
                                const InputEvent* ev) {
    if (!ev) {
        return;
    }
    if (ev->kind == InputEventKind::Change) {
        if (self->Rescan()) {
            Notify(cx);
        }
        return;
    }
    if (ev->kind == InputEventKind::PressEnter && !ev->secondary) {
        if (self->MenuIsCurrent()) {
            self->Accept(cx);
        } else {
            self->open = false;
            self->Submit(cx);
        }
        return;
    }
    if (ev->kind == InputEventKind::Blur) {
        self->open = false;
    }
}

void CompletionExample::OnCapture(CompletionExample* self, Ctx* cx,
                                  const KeyEvent* ev) {
    if (!ev || ev->shift || ev->ctrl || ev->alt || ev->platform ||
        ev->function) {
        return;
    }
    if (ev->vk == KeyUp) {
        self->Select(cx, -1, ev);
    } else if (ev->vk == KeyDown) {
        self->Select(cx, 1, ev);
    } else if (ev->vk == KeyEscape) {
        if (self->OwnKey(cx, ev)) {
            self->open = false;
            Notify(cx);
        }
    } else if (ev->vk == KeyTab) {
        if (self->OwnKey(cx, ev)) {
            self->Accept(cx);
        }
    }
}

void CompletionExample::OnRow(CompletionExample* self, Ctx* cx,
                              const ClickEvent*, int64_t ix) {
    if (self->open && ix >= 0 && ix < self->n) {
        self->selected = (int)ix;
    }
    self->Accept(cx);
}

El* CompletionExample::Render(CompletionExample* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        InputSetPlaceholder(&self->field,
                            StrL("Type $ to complete a variable"));
    }
    if (self->field.focused) {
        cx->win->input = &self->field;
    }
    self->field.onChange = Listen(cx, &CompletionExample::OnInput);
    // Caret movement emits no event, so the menu is rescanned here too.
    self->Rescan();

    El* host = Div(a)
                   ->W(kFill)
                   ->CaptureKeyDown(Listen(cx, &CompletionExample::OnCapture))
                   ->Child(component::Input::New(cx, StrL("completion-input"),
                                                 &self->field)
                               ->IntoEl());
    if (self->open) {
        El* panel = Div(a)
                        ->Id(StrL("completion-menu"))
                        ->FlexCol()
                        ->W(Rems(cx, 18))
                        ->MaxH(Rems(cx, 16))
                        ->ClipY()
                        ->Pad(4)
                        ->Gap(2);
        for (int i = 0; i < self->n; i++) {
            const VariableRow& row = kVariables[self->items[i]];
            El* label =
                Div(a)
                    ->FlexRow()
                    ->Gap(8)
                    ->Font(14)
                    ->Child(TextEl(a, Str(row.name)))
                    ->Child(TextEl(a, Str(row.description))->Fg(th.mutedFg));
            panel->Child(component::ListItem::New(cx, label)
                             ->Selected(i == self->selected)
                             ->IntoEl(StrDup(a, fmt("completion-%d", i)),
                                      Listen(cx, &CompletionExample::OnRow, i),
                                      {}));
        }
        host->Child(
            Div(a)->Deferred()->Absolute()->Left(0)->TopRel(1)->PadT(4)->Child(
                component::PopoverSurface(cx, panel)));
    }
    El* column = Div(a)->FlexCol()->W(kFill)->Gap(8)->Child(host);
    column->Child(TextEl(a, Str(self->status))->Font(14)->Fg(th.mutedFg));
    return column;
}
