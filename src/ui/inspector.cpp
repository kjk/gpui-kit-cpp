#include "ui/inspector.h"
#include "base/json.h"
#include "ui/alert.h"
#include "ui/button.h"
#include "ui/description_list.h"
#include "ui/input.h"
#include "ui/title_bar.h"

namespace gpui {

namespace component {

Inspector* Inspector::New(Ctx* cx) {
    Arena* a = cx->a;
    Inspector* i = ArenaNew<Inspector>(a);
    i->a = a;
    i->cx = cx;
    return i;
}
Inspector* Inspector::W(float v) {
    width = v;
    return this;
}

static void InspectorPickClick(void*, Ctx* cx, const ClickEvent*) {
    const InspectorState* st = WindowInspector(cx);
    WindowInspectorPick(cx->win, !(st && st->picking));
}

static void InspectorCloseClick(void*, Ctx* cx, const ClickEvent*) {
    WindowToggleInspector(cx->win);
}

static Str KindName(int kind) {
    switch ((ElKind)kind) {
        case ElKind::Text:
            return StrL("Text");
        case ElKind::Chart:
            return StrL("Chart");
        case ElKind::Progress:
            return StrL("Progress");
        case ElKind::Icon:
            return StrL("Icon");
        default:
            return StrL("Div");
    }
}

// ─── the style editor ────────────────────────────────────────────────────

// #rrggbb, or #rrggbbaa when the colour is not opaque — which is how a theme
// json spells one, so what comes out reads back in.
static void JsonColor(StrBuilder& sb, const char* key, Rgba c) {
    if (c.a == 255) {
        sb.Append(
            fmt("  \"%s\": \"#%02x%02x%02x\",\n", Str(key), c.r, c.g, c.b));
    } else {
        sb.Append(fmt("  \"%s\": \"#%02x%02x%02x%02x\",\n", Str(key), c.r, c.g,
                      c.b, c.a));
    }
}

static void JsonNum(StrBuilder& sb, const char* key, float v) {
    sb.Append(fmt("  \"%s\": %g,\n", Str(key), (double)v));
}

Str StyleToJson(Arena* a, const Style& style) {
    StrBuilder sb;
    sb.Append(StrL("{\n"));
    if (style.hasBg) {
        JsonColor(sb, "background", style.bg.color);
    }
    if (style.hasColor) {
        JsonColor(sb, "color", style.color);
    }
    JsonColor(sb, "border_color", style.borderColor);
    // padding is four numbers in GPUI too; the editor writes the object so an
    // element with uneven padding round-trips.
    sb
        .Append(fmt("  \"padding\": { \"top\": %g, \"right\": %g, "
                    "\"bottom\": %g, \"left\": %g },\n",
                    (double)style.pad.top, (double)style.pad.right,
                    (double)style.pad.bottom, (double)style.pad.left));
    // One number, the way GPUI's `gap_N` writes it. A style whose two axes
    // differ — `gap_x_2` on its own — reports the row gap here.
    JsonNum(sb, "gap", style.gapX);
    JsonNum(sb, "radius", style.radius);
    JsonNum(sb, "border", style.border);
    JsonNum(sb, "font_size", style.fontSize);
    JsonNum(sb, "opacity", style.opacity);
    // kAuto is "not set", which JSON says as null rather than as a number
    // that would pin the box to some width it never asked for.
    if (style.width != kAuto) {
        JsonNum(sb, "width", style.width);
    }
    if (style.height != kAuto) {
        JsonNum(sb, "height", style.height);
    }
    Str out = sb.TakeStr();
    // Drop the comma the last member left behind, then close the object.
    while (len(out) > 0 &&
           (out.s[len(out) - 1] == '\n' || out.s[len(out) - 1] == ' ')) {
        out.len--;
    }
    if (len(out) > 0 && out.s[len(out) - 1] == ',') {
        out.len--;
    }
    Str body = StrDup(a, out);
    StrFree(out);
    return StrDup(a, fmt("%s\n}", body));
}

// "#rgb" / "#rrggbb" / "#rrggbbaa", the three a theme json uses.
static bool ParseHexColor(Str s, Rgba* out) {
    if (!s.s || len(s) < 4 || s.s[0] != '#') {
        return false;
    }
    int n = len(s) - 1;
    if (n != 3 && n != 6 && n != 8) {
        return false;
    }
    int v[8] = {};
    for (int i = 0; i < n; i++) {
        char c = s.s[i + 1];
        if (c >= '0' && c <= '9') {
            v[i] = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v[i] = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v[i] = c - 'A' + 10;
        } else {
            return false;
        }
    }
    Rgba c = {};
    if (n == 3) {
        c = Rgba{(uint8_t)(v[0] * 17), (uint8_t)(v[1] * 17),
                 (uint8_t)(v[2] * 17), 255};
    } else {
        c = Rgba{(uint8_t)(v[0] * 16 + v[1]), (uint8_t)(v[2] * 16 + v[3]),
                 (uint8_t)(v[4] * 16 + v[5]), 255};
        if (n == 8) {
            c.a = (uint8_t)(v[6] * 16 + v[7]);
        }
    }
    *out = c;
    return true;
}

// One colour member: absent leaves the style alone, and a bad one is an error
// the editor shows rather than a value silently dropped.
static bool ReadColor(const JsonValue* root, const char* key, Rgba* out,
                      uint32_t bit, uint32_t* fields, Str* error) {
    const JsonValue* v = JsonGet(root, key);
    if (!v || v->kind == JsonKind::Null) {
        return true;
    }
    if (v->kind != JsonKind::String || !ParseHexColor(v->str, out)) {
        *error = StrDup(fmt("%s: expected \"#rrggbb\"", Str(key)));
        return false;
    }
    *fields |= bit;
    return true;
}

static bool ReadNum(const JsonValue* root, const char* key, float* out,
                    uint32_t bit, uint32_t* fields, Str* error) {
    const JsonValue* v = JsonGet(root, key);
    if (!v || v->kind == JsonKind::Null) {
        return true;
    }
    if (v->kind != JsonKind::Number) {
        *error = StrDup(fmt("%s: expected a number", Str(key)));
        return false;
    }
    *out = (float)v->num;
    *fields |= bit;
    return true;
}

bool StyleFromJson(Arena* a, Str text, Style* style, uint32_t* fields,
                   Str* error) {
    *fields = 0;
    *error = {};
    JsonValue* root = JsonParse(a, text);
    if (!root || root->kind != JsonKind::Object) {
        *error = StrDup(StrL("expected a JSON object"));
        return false;
    }
    Rgba bg = style->bg.color;
    if (!ReadColor(root, "background", &bg, StyleFieldBg, fields, error) ||
        !ReadColor(root, "color", &style->color, StyleFieldColor, fields,
                   error) ||
        !ReadColor(root, "border_color", &style->borderColor,
                   StyleFieldBorderColor, fields, error) ||
        !ReadNum(root, "gap", &style->gapX, StyleFieldGap, fields, error) ||
        !ReadNum(root, "radius", &style->radius, StyleFieldRadius, fields,
                 error) ||
        !ReadNum(root, "border", &style->border, StyleFieldBorder, fields,
                 error) ||
        !ReadNum(root, "font_size", &style->fontSize, StyleFieldFontSize,
                 fields, error) ||
        !ReadNum(root, "opacity", &style->opacity, StyleFieldOpacity, fields,
                 error) ||
        !ReadNum(root, "width", &style->width, StyleFieldWidth, fields,
                 error) ||
        !ReadNum(root, "height", &style->height, StyleFieldHeight, fields,
                 error)) {
        return false;
    }
    style->bg = bg;
    // The JSON says one gap; both axes take it, the way `gap_N` does.
    style->gapY = style->gapX;
    const JsonValue* pad = JsonGet(root, "padding");
    if (pad && pad->kind == JsonKind::Number) {
        float v = (float)pad->num;
        style->pad = Edges{v, v, v, v};
        *fields |= StyleFieldPad;
    } else if (pad && pad->kind == JsonKind::Object) {
        style->pad.top = (float)JsonNumber(JsonGet(pad, "top"), style->pad.top);
        style->pad
            .right = (float)JsonNumber(JsonGet(pad, "right"), style->pad.right);
        style->pad.bottom =
            (float)JsonNumber(JsonGet(pad, "bottom"), style->pad.bottom);
        style->pad
            .left = (float)JsonNumber(JsonGet(pad, "left"), style->pad.left);
        *fields |= StyleFieldPad;
    } else if (pad && pad->kind != JsonKind::Null) {
        *error = StrDup(StrL("padding: expected a number or an object"));
        return false;
    }
    return true;
}

// The editor's own state, which has to outlive the frame the panel is built
// on. Rust's is the `DivInspector` view beside the inspector; this is the
// window-keyed entity that stands in for one.
DivInspector::~DivInspector() {
    StrFree(applied);
    StrFree(error);
}

static void EditorLoad(DivInspector* e, Ctx* cx, const Style& style) {
    Str json = StyleToJson(cx->a, style);
    InputSetValue(&e->jsonInput, json);
    StrFree(e->applied);
    e->applied = StrDup(json);
    StrFree(e->error);
    e->error = {};
}

void DivInspector::UpdateInspectedElement(const InspectorPick& pick, Ctx* cx) {
    if (inspectorId == pick.id) {
        return;
    }
    inspectorId = pick.id;
    initialStyle = pick.style;
    jsonInput.kind = InputKind::Textarea;
    EditorLoad(this, cx, initialStyle);
}

bool DivInspector::EditJson(Str code, Ctx* cx) {
    StrFree(applied);
    applied = StrDup(code);
    Style style = initialStyle;
    uint32_t fields = 0;
    Str parseError = {};
    if (!StyleFromJson(cx->a, code, &style, &fields, &parseError)) {
        StrFree(error);
        error = parseError;
        Notify(cx);
        return false;
    }
    StyleOverrideSet(inspectorId, fields, style);
    StrFree(error);
    error = {};
    Notify(cx);
    return true;
}

void DivInspector::Reset(Ctx* cx) {
    StyleOverrideClear(inspectorId);
    EditorLoad(this, cx, initialStyle);
    Notify(cx);
}

void DivInspector::OnReset(DivInspector* self, Ctx* cx, const ClickEvent*) {
    self->Reset(cx);
}

void DivInspector::OnFocus(DivInspector* self, Ctx* cx, const ClickEvent*) {
    self->jsonInput.focused = true;
    Notify(cx);
}

El* DivInspector::Render(const InspectorPick& p, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    UpdateInspectedElement(p, cx);
    Str current = InputValue(&jsonInput);
    if (!base::StrEq(current, applied)) {
        EditJson(current, cx);
    }
    if (jsonInput.focused) {
        cx->win->input = &jsonInput;
    }

    // v_flex().flex_1().gap_y_3().h_2_5().flex_shrink_0(): the pane takes what
    // the panel has left rather than a height of its own, and the editor
    // inside it is h(relative(1.)).
    El* box = Div(a)->FlexCol()->W(kFill)->GapY(12)->Flex1();
    // h_flex().gap_x_2() with the label as the flex_1 child, which is what
    // pushes the button to the end without a justification.
    El* head = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->GapX(8);
    head->Child(Div(a)->Flex1()->Child(
        TextEl(a, StrL("JSON Styles"))->Font(14)->Fg(th.foreground)));
    head->Child(Button::New(cx, StrL("style-reset"))
                    ->Label(StrL("Reset"))
                    ->WithSize(UiSize::Small)
                    ->OnClick(Listen(cx, &DivInspector::OnReset))
                    ->IntoEl());
    box->Child(head);
    El* body = Div(a)->FlexCol()->W(kFill)->GapY(4)->Flex1();
    body->Child(Textarea::New(cx, StrL("style-json"), &jsonInput)
                    ->H(kFill)
                    ->OnFocus(Listen(cx, &DivInspector::OnFocus))
                    ->IntoEl());
    if (error.s) {
        body->Child(Alert::Error(cx, StrL("style-error"), error)
                        ->WithSize(UiSize::XSmall)
                        ->IntoEl());
    }
    box->Child(body);
    return box;
}

static El* StyleEditor(Ctx* cx, const InspectorPick& p) {
    // An element with no click id cannot be found again next frame, so there
    // is nothing an override could be keyed on.
    if (p.id == 0) {
        const Theme& th = ThemeNow(cx->app);
        return Div(cx->a)->W(kFill)->Child(
            TextEl(cx->a, StrL("This element has no id, so its style cannot be "
                               "edited."))
                ->Font(14)
                ->Fg(th.mutedFg));
    }
    Entity<DivInspector> editor = KeyedEntity<DivInspector>(
        cx, KeyedKey((uint32_t)HashClickId(StrL("inspector")),
                     (uint32_t)HashClickId(StrL("style"))));
    DivInspector* state = editor.Get(cx);
    if (!state) {
        return nullptr;
    }
    Ctx editorCx = *cx;
    editorCx.self = editor.id;
    return state->Render(p, &editorCx);
}

El* Inspector::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    const InspectorState* st = WindowInspector(cx);
    if (!st || !st->on) {
        return nullptr;
    }
    El* panel = Div(a)
                    ->FlexCol()
                    ->W(width)
                    ->H(kFill)
                    ->Bg(th.tokens.background)
                    ->BorderL(1, th.border);

    // The title bar: the magnifier that starts picking, the name, and the
    // close button — the three things Rust puts there.
    El* bar = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->H(kTitleBarHeight)
                  ->PadX(8)
                  ->Gap(8)
                  ->ItemsCenter()
                  ->JustifyBetween()
                  ->Bg(th.tokens.titleBar)
                  ->BorderB(1, th.titleBarBorder);
    El* left = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    left->Child(Button::New(cx, StrL("inspect"))
                    ->Icon(IconName::Search)
                    ->Ghost()
                    ->Selected(st->picking)
                    ->OnClick(Listen(cx, &InspectorPickClick))
                    ->IntoEl());
    left->Child(TextEl(a, StrL("Inspector"))->Font(14)->Fg(th.foreground));
    bar->Child(left);
    bar->Child(Button::New(cx, StrL("inspector-close"))
                   ->Icon(IconName::X)
                   ->Ghost()
                   ->OnClick(Listen(cx, &InspectorCloseClick))
                   ->IntoEl());
    panel->Child(bar);

    El* body = Div(a)->FlexCol()->Flex1()->W(kFill)->Pad(12)->Gap(12);
    if (!st->hasPick) {
        body->Child(TextEl(a, StrL("Pick an element to inspect it."))
                        ->Font(14)
                        ->Fg(th.mutedFg));
        panel->Child(body);
        return panel;
    }

    const InspectorPick& p = st->pick;
    // Rust leads with the element's source location, as a Link beside a
    // Clipboard. GPUI stamps one on every element from `#[track_caller]`;
    // nothing here records where an El was built, so there is nothing to
    // show and the element leads with what it is and which id it answers to.
    // Its "Rust Styles" pane is out for a harder reason: it is a code editor
    // over the element's Rust source with an LSP completion provider behind
    // it, and an LSP client is a standing non-goal (AGENTS.md). The JSON
    // pane below is the half of DivInspector that can exist here.
    body->Child(TextEl(a, KindName(p.kind))->Font(14)->Fg(th.foreground));
    // DescriptionList::new().columns(1).label_width(px(110.)).bordered(false)
    DescriptionList* dl =
        DescriptionList::New(cx)->Columns(1)->LabelWidth(110)->Bordered(false);
    if (p.elId.s) {
        dl->Item(StrL("id"), p.elId);
    }
    if (p.id) {
        dl->Item(StrL("click id"), StrDup(a, fmt("%d", p.id)));
    }
    dl->Item(StrL("origin"),
             StrDup(a, fmt("%d, %d", (int)p.bounds.x, (int)p.bounds.y)));
    dl->Item(StrL("size"),
             StrDup(a, fmt("%d × %d", (int)p.bounds.w, (int)p.bounds.h)));
    dl->Item(StrL("depth"), StrDup(a, fmt("%d", p.depth)));
    dl->Item(StrL("direction"), p.row ? StrL("row") : StrL("column"));
    if (p.pad > 0) {
        dl->Item(StrL("padding"), StrDup(a, fmt("%d", (int)p.pad)));
    }
    if (p.gap > 0) {
        dl->Item(StrL("gap"), StrDup(a, fmt("%d", (int)p.gap)));
    }
    if (p.radius > 0) {
        dl->Item(StrL("radius"), StrDup(a, fmt("%d", (int)p.radius)));
    }
    if (p.border > 0) {
        dl->Item(StrL("border"), StrDup(a, fmt("%d", (int)p.border)));
    }
    if (p.font > 0) {
        dl->Item(StrL("font size"), StrDup(a, fmt("%d", (int)p.font)));
    }
    if (p.hasBg) {
        dl->Item(StrL("background"),
                 StrDup(a, fmt("#%02x%02x%02x", p.bg.r, p.bg.g, p.bg.b)));
    }
    if (p.text.s && len(p.text) > 0) {
        dl->Item(StrL("text"), p.text);
    }
    body->Child(dl->IntoEl());
    // The live edit, which is what Rust's DivInspector is for.
    if (El* editor = StyleEditor(cx, p)) {
        body->Child(editor);
    }
    panel->Child(body);
    return panel;
}

} // namespace component
} // namespace gpui
