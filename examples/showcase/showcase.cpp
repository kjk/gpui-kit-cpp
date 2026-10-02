#include "Showcase.h"

static ShowcaseRenderFn gRender[CompCount] = {};

void ShowcaseRegister(int comp, ShowcaseRenderFn render) {
    if (comp < 0 || comp >= CompCount) {
        return;
    }
    gRender[comp] = render;
}

El* ShowcaseRenderRegistered(ShowcaseApp* app, Ctx* cx, WinSize size) {
    int c = app->component;
    if (c >= 0 && c < CompCount && gRender[c]) {
        return gRender[c](app, cx, size);
    }
    if (c == CompOverview) {
        return ShowcaseOverview(app, cx);
    }
    return ScComingSoon(cx, CompSlug(c));
}

static const char* kSlugs[CompCount] = {
    "accordion",   "alert-dialog",   "avatar",      "button",
    "calendar",    "checkbox",       "collapsible", "color-picker",
    "combobox",    "date-picker",    "dialog",      "dock",
    "editor",      "hover-card",     "input",       "link",
    "nav-stack",   "number-input",   "otp-input",   "pagination",
    "popover",     "popup",          "progress",    "radio",
    "radio-group", "resizable",      "scrollbar",   "select",
    "sheet",       "slider",         "switch",      "table",
    "tabs",        "text-selection", "text-view",   "textarea",
    "time-field",  "toast",          "toggle",      "toggle-group",
    "toolbar",     "tooltip",        "tree",        "virtual-list",
};

const char* CompSlug(int i) {
    if (i < 0 || i >= CompCount) {
        return "overview";
    }
    return kSlugs[i];
}

int CompFromSlug(const char* slug) {
    if (!slug || !slug[0] || StrEqI(Str(slug), "overview")) {
        return CompOverview;
    }
    for (int i = 0; i < CompCount; i++) {
        if (base::StrEqI(Str(slug), kSlugs[i])) {
            return i;
        }
    }
    return CompOverview;
}

Str DupA(Ctx* cx, const char* s) {
    Arena* a = cx->a;
    return StrDup(a, Str(s));
}

El* ScTxt(Ctx* cx, Str s, float px, Rgba c) {
    Arena* a = cx->a;
    return TextEl(a, s)->Font(px)->Fg(c);
}

struct ScChevronPaint {
    bool up = false;
    Rgba color = {};
};

static void PaintScChevron(PaintCtx* ctx, El* e, void* user) {
    const ScChevronPaint* c = (const ScChevronPaint*)user;
    float cx = e->x + e->w / 2;
    float cy = e->y + e->h / 2;
    const float halfWidth = 3.5f;
    const float halfHeight = 1.75f;
    float tip = c->up ? -halfHeight : halfHeight;
    Path* path = PathNew(ctx, false);
    if (!path) {
        return;
    }
    PathMoveTo(path, cx - halfWidth, cy - tip);
    PathLineTo(path, cx, cy + tip);
    PathLineTo(path, cx + halfWidth, cy - tip);
    PathStroke(ctx, path, 1.25f, c->color);
    PathFree(path);
}

// components/mod.rs chevron: a disclosure chevron drawn as a path. The `⌄`
// and `⌃` glyphs sit at the baseline and cap height, so a centered row still
// shows them off center; a path is centered in its own box. Rust strokes in
// the current text color; here the caller names it.
El* ScChevron(Ctx* cx, bool up, Rgba color) {
    Arena* a = cx->a;
    ScChevronPaint* paint = ArenaNew<ScChevronPaint>(a);
    paint->up = up;
    paint->color = color;
    El* e = Div(a)->W(12)->H(12)->FlexNone();
    e->customPaint = PaintScChevron;
    e->customUser = paint;
    return e;
}

El* ScBtnGhost(Ctx* cx, int id, Listener onClick, Str label) {
    Arena* a = cx->a;
    return Div(a)
        ->H(28)
        ->PadX(8)
        ->ItemsCenter()
        ->JustifyCenter()
        ->Border(1, ScInk())
        ->Bg(ScWhite())
        ->HoverBg(ScHover())
        ->OnClick(onClick)
        ->FocusId(id)
        ->Child(ScTxt(cx, label, 12, ScInk()));
}

El* ScComingSoon(Ctx* cx, const char* name) {
    Arena* a = cx->a;
    return Div(a)
        ->FlexCol()
        ->Gap(8)
        ->W(280)
        ->Child(ScTxt(cx, DupA(cx, name), 16, ScInk())->Semibold())
        ->Child(ScTxt(cx, StrL("This component page is not ported yet."), 12,
                      ScMutedC()));
}

// Gallery navigation: the tile knows which component it opens.
static void OpenComp(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                     int64_t comp) {
    app->component = (int)comp;
    app->scrollY = 0;
    Notify(cx);
}

static void BackToOverview(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->component = CompOverview;
    app->scrollY = 0;
    Notify(cx);
}

El* ShowcaseOverview(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    (void)app;
    El* col = Div(a)->FlexCol()->Gap(16)->W(720)->MaxW(720);
    col->Child(
        Div(a)
            ->FlexCol()
            ->Gap(4)
            ->Child(ScTxt(cx, StrL("GPUI Base"), 18, ScInk())->Semibold())
            ->Child(ScTxt(
                cx, StrL("Choose a component to open its interactive example."),
                12, ScMutedC())));

    El* grid = Div(a)->FlexCol()->Gap(4)->W(kFill);
    for (int row = 0; row < CompCount; row += 3) {
        El* r = Div(a)->FlexRow()->Gap(4)->W(kFill);
        for (int c = 0; c < 3; c++) {
            int i = row + c;
            if (i >= CompCount) {
                r->Child(Div(a)->Flex1());
                continue;
            }
            r->Child(
                Div(a)
                    ->Flex1()
                    ->H(36)
                    ->PadX(12)
                    ->ItemsCenter()
                    ->JustifyStart()
                    ->Border(1, ScBorder())
                    ->Bg(ScWhite())
                    ->HoverBg(ScHover())
                    ->OnClick(Listen(cx, &OpenComp, i))
                    ->FocusId(HashClickId(DupFmt(cx, "overview-item-%d", i)))
                    ->Child(ScTxt(cx, Str(kSlugs[i]), 12, ScInk())));
        }
        grid->Child(r);
    }
    col->Child(grid);
    return col;
}

static El* RenderComp(ShowcaseApp* app, Ctx* cx, WinSize size) {
    return ShowcaseRenderRegistered(app, cx, size);
}

static void BindInput(ShowcaseApp* app, Window* win) {
    win->input = nullptr;
    app->input.focused =
        app->input.focused &&
        (app->component == CompInput || app->component == CompNumberInput ||
         (app->component == CompDialog && app->dialogOpen));
    app->comboQuery.focused = app->comboQuery.focused &&
                              app->component == CompCombobox &&
                              app->comboboxOpen;
    app->hexIn.focused = app->hexIn.focused &&
                         app->component == CompColorPicker && app->colorOpen;
    app->textareaOn = app->textareaOn && app->component == CompTextarea;
    app->editorOn = app->editorOn && app->component == CompEditor;
    app->textarea.focused = app->textareaOn;
    app->editor.focused = app->editorOn;
    if (app->comboQuery.focused) {
        win->input = &app->comboQuery;
    } else if (app->hexIn.focused) {
        win->input = &app->hexIn;
    } else if (app->input.focused) {
        win->input = &app->input;
    } else if (app->textareaOn) {
        win->input = &app->textarea;
    } else if (app->editorOn) {
        win->input = &app->editor;
    }
}

El* ShowcaseApp::Render(ShowcaseApp* app, Ctx* cx) {
    Arena* frame = cx->a;
    Window* win = cx->win;
    WinSize size = WindowSize(win);
    BindInput(app, win);
    bool showBack = app->navigationEnabled && app->component != CompOverview;

    // activate_palette(window) at the top of the render, so every page below
    // resolves its light literals against the appearance in force. Rust reads
    // `window.appearance()`; there is no portable seam for the desktop
    // setting here, so this is the theme mode the example chose — see
    // examples/showcase/palette.h.
    PaletteActivate(ThemeNow(cx->app).mode == ThemeMode::Dark);

    // The canvas is its own token: the dark palette darkens it below the
    // surfaces that sit on it, where the light one has both white.
    El* root = Div(frame)->FlexCol()->SizeFull()->Bg(ExampleCanvas());
    if (showBack) {
        root->Child(
            Div(frame)
                ->H(40)
                ->PadX(12)
                ->ItemsCenter()
                ->Shrink0()
                ->BorderB(1, ScLine())
                ->Child(ScBtnGhost(cx, HashClickId(StrL("back-to-overview")),
                                   Listen(cx, &BackToOverview),
                                   StrL("All components"))));
    }

    El* content = RenderComp(app, cx, size);
    // Surfaces rather than parts: a few pages take the whole viewport.
    // Centring one inside a `flex_none` box leaves a percentage size with
    // nothing to resolve against, and it collapses.
    bool fillsViewport = app->component == CompDock;
    El* page = Div(frame)
                   ->W(kFill)
                   ->MinH(size.dipH - (showBack ? 40.f : 0.f))
                   ->Pad(16);
    El* wrap = Div(frame)->FlexCol();
    if (fillsViewport) {
        // Rust's row, whose height is its content's: `size_full` resolves
        // against a parent with no definite height, so what is left is the
        // 420 floor the page names.
        page->FlexRow();
        wrap->Flex1()->SizeFull()->MinH(420);
    } else {
        page->FlexCol()->ItemsCenter()->JustifyCenter();
        wrap->Shrink0();
    }
    El* scroller = Div(frame)
                       ->Flex1()
                       ->ClipY()
                       ->ScrollY(app->scrollY)
                       ->W(kFill)
                       ->Child(page->Child(wrap->Child(content)));
    root->Child(scroller);
    return root;
}

// The OTP page is a row of single-character cells, not a text field, so it
// still keeps a plain buffer.
static void OtpPush(char* buf, int* len, uint32_t cp) {
    if (cp < '0' || cp > '9' || *len >= 6) {
        return;
    }
    buf[(*len)++] = (char)cp;
    buf[*len] = 0;
}

static void OtpPop(char* buf, int* len) {
    if (*len > 0) {
        buf[--(*len)] = 0;
    }
}

static void ParseHexIn(ShowcaseApp* app) {
    const char* s = InputCStr(&app->hexIn);
    if (s[0] == '#') {
        s++;
    }
    unsigned v = 0;
    if (sscanf(s, "%x", &v) == 1) {
        app->colorHex = v & 0xffffff;
    }
}

void ShowcaseChar(ShowcaseApp* app, Window* win, uint32_t cp) {
    (void)win;
    // The window routes a typed character into whichever InputState has focus;
    // only the OTP cells and the hex readout need anything on top of that.
    if (app->component == CompOtpInput && app->otpOn) {
        OtpPush(app->otp, &app->otpLen, cp);
        return;
    }
    if (app->component == CompColorPicker && app->hexIn.focused) {
        ParseHexIn(app);
    }
}

void ShowcaseKey(ShowcaseApp* app, Window* win, int vk, bool down) {
    (void)win;
    if (!down) {
        return;
    }
    if (vk == KeyBack) {
        if (app->component == CompOtpInput && app->otpOn) {
            OtpPop(app->otp, &app->otpLen);
        }
        return;
    }
    if (vk == KeyEscape) {
        app->colorOpen = false;
        app->comboboxOpen = false;
        app->selectOpen = false;
        app->dateOpen = false;
        app->popupOpen = false;
        app->popoverOpen = false;
        app->hexIn.focused = false;
        app->comboQuery.focused = false;
        return;
    }
    if (vk == KeyReturn) {
        if (app->component == CompColorPicker && app->colorOpen) {
            ParseHexIn(app);
            app->colorOpen = false;
            app->hexIn.focused = false;
        }
    }
}

void ShowcaseWheel(ShowcaseApp* app, float x, float y, float delta) {
    (void)x;
    (void)y;
    app->scrollY -= delta;
    app->scrollY = ClampF(app->scrollY, 0, 4000);
}

// The text-selection page keeps no offsets of its own any more: its runs
// are SelectableText, so the window owns the gesture and the copy, and the
// page reads WindowSelectionText when it draws its footer.

// What the window would copy, read while the frame that painted the runs is
// still the current one. Rust's page gets the same string from its
// TextSelectionEvent subscription.
static void ShowcaseReadSelection(ShowcaseApp* app, Window* win) {
    if (app->component != CompTextSelection) {
        return;
    }
    int n = WindowSelectionText(win, app->selText, (int)sizeof(app->selText));
    app->selActive = n > 0;
    AppInvalidate(win);
}

void ShowcaseMouseMove(ShowcaseApp* app, Window* win,
                       const MouseMoveEvent* ev) {
    (void)ev;
    if (win->mouseDown) {
        ShowcaseReadSelection(app, win);
    }
}

void ShowcaseMouseDown(ShowcaseApp* app, Window* win,
                       const MouseDownEvent* ev) {
    (void)ev;
    ShowcaseReadSelection(app, win);
}

void ShowcaseMouseUp(ShowcaseApp* app, Window* win, const MouseUpEvent* ev) {
    (void)ev;
    ShowcaseReadSelection(app, win);
}

// A click no element claimed dismisses whatever is open, the way GPUI closes
// an overlay on an outside click.
static void OnUnhandledClick(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->colorOpen = false;
    app->comboboxOpen = false;
    app->selectOpen = false;
    app->dateOpen = false;
    app->popupOpen = false;
    app->popoverOpen = false;
    app->hexIn.focused = false;
    app->comboQuery.focused = false;
    Notify(cx);
}

static void OnKey(ShowcaseApp* app, Ctx* cx, const KeyEvent* ev) {
    if (ev->ch != 0) {
        ShowcaseChar(app, cx->win, ev->ch);
        return;
    }
    ShowcaseKey(app, cx->win, ev->vk, ev->down);
}

static void OnWheel(ShowcaseApp* app, Ctx* cx, const ScrollWheelEvent* ev) {
    (void)cx;
    ShowcaseWheel(app, ev->x, ev->y, ev->deltaY);
}

static void OnMouseDown(ShowcaseApp* app, Ctx* cx, const MouseDownEvent* ev) {
    ShowcaseMouseDown(app, cx->win, ev);
}

static void OnMouseUp(ShowcaseApp* app, Ctx* cx, const MouseUpEvent* ev) {
    ShowcaseMouseUp(app, cx->win, ev);
}

static void OnMouseMove(ShowcaseApp* app, Ctx* cx, const MouseMoveEvent* ev) {
    ShowcaseMouseMove(app, cx->win, ev);
}

// The page to open, if one was named on the command line.
static Str ParseSlug(int argc, char** argv) {
    return argc >= 2 && argv[1] ? Str(argv[1]) : Str{};
}

int GpuiMain(int argc, char** argv) {
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);

    Entity<ShowcaseApp> view = EntityNew<ShowcaseApp>(app);
    ShowcaseApp* self = view.Get(app);
    Str slug = ParseSlug(argc, argv);
    self->component = CompFromSlug(slug.s);
    // 0..100 with the thumb where the page has always shown it.
    SliderSetValue(&self->slider, SliderSingle(64.f));
    self->navigationEnabled = (self->component == CompOverview);

    InputSetPlaceholder(&self->input, StrL("Type something…"));
    InputSetValue(&self->input, self->component == CompNumberInput
                                    ? StrL("12")
                                    : StrL("Hello GPUI"));
    if (self->component == CompInput || self->component == CompNumberInput) {
        self->input.focused = true;
    } else if (self->component == CompEditor) {
        self->editorOn = true;
    } else if (self->component == CompOtpInput) {
        self->otpOn = true;
    }
    InputSetPlaceholder(&self->comboQuery, StrL("Search frameworks…"));
    InputSetPlaceholder(&self->hexIn, StrL("#2563EB"));
    InputSetValue(&self->hexIn, StrL("#2563EB"));
    self->textarea.kind = InputKind::Textarea;
    InputSetValue(&self->textarea,
                  StrL("Build focused interfaces.\nKeep behavior composable."));
    self->editor.kind = InputKind::Editor;

    Window* win = WindowOpenView(app, StrL("GPUI Base C++"), 840, 640, view.id,
                                 WinOpts{});
    WindowOnUnhandledClick(win, ListenTo(view, &OnUnhandledClick));
    WindowOnKey(win, ListenTo(view, &OnKey));
    WindowOnScrollWheel(win, ListenTo(view, &OnWheel));
    WindowOnMouseDown(win, ListenTo(view, &OnMouseDown));
    WindowOnMouseUp(win, ListenTo(view, &OnMouseUp));
    WindowOnMouseMove(win, ListenTo(view, &OnMouseMove));
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
