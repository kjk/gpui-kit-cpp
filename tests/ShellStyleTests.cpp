/* crates/shell/src/style.rs: the script's style vocabulary. The names come
 * from the shell's own TypeScript declarations (Rust's `gpui-shell types`),
 * so a method the declarations promise and the materializer refuses fails
 * here rather than in a script. */

#include "Test.h"

#include <stdio.h>
#include <string.h>

using namespace gpui::shell;

namespace shell_style_tests {

// The no-argument methods the declarations list that are not styles but
// behaviors the shell records on its own: `IsBehavior` in runtime.cpp.
static const char* const kNullaryBehaviors[] = {
    "controls_right",       "aria_active_descendant", "overflow_scroll",
    "overflow_x_scroll",    "overflow_y_scroll",      "overflow_scrollbar",
    "overflow_x_scrollbar", "overflow_y_scrollbar",   "viewport_from_layout",
};

static bool IsNullaryBehavior(Str name) {
    for (const char* behavior : kNullaryBehaviors) {
        if (StrEq(name, behavior)) {
            return true;
        }
    }
    return false;
}

struct DeclaredStyles {
    Vec<Str> nullary;
    // Methods of the form `name(this: Self, value: T)`, with T.
    Vec<Str> param;
    Vec<Str> paramType;
    // Every other method that takes arguments.
    Vec<Str> other;
};

static Str LineAt(Str text, int* at) {
    int start = *at;
    int end = start;
    while (end < len(text) && text.s[end] != '\n') {
        end++;
    }
    *at = end < len(text) ? end + 1 : end;
    return Str(text.s + start, end - start);
}

// Reads the `NativeElement` interface of the generated declarations.
static void ReadDeclaredStyles(Str dts, DeclaredStyles* out) {
    int at = StrFind(dts, "  export interface NativeElement {");
    utassert(at >= 0);
    if (at < 0) {
        return;
    }
    LineAt(dts, &at);
    Str prefix = StrL("<Self extends Element>(this: Self");
    while (at < len(dts)) {
        Str line = LineAt(dts, &at);
        if (StrEq(line, StrL("  }"))) {
            break;
        }
        if (!StrStartsWith(line, "    ") || len(line) < 5 || line.s[4] < 'a' ||
            line.s[4] > 'z') {
            continue;
        }
        Str rest(line.s + 4, len(line) - 4);
        int open = StrFind(rest, prefix);
        if (open <= 0) {
            continue;
        }
        Str name(rest.s, open);
        Str after(rest.s + open + len(prefix), len(rest) - open - len(prefix));
        if (StrEq(after, StrL("): Self;"))) {
            VecAppend(out->nullary, name);
            continue;
        }
        Str valuePrefix = StrL(", value: ");
        Str suffix = StrL("): Self;");
        if (StrStartsWith(after, valuePrefix) && StrEndsWith(after, suffix)) {
            Str type(after.s + len(valuePrefix),
                     len(after) - len(valuePrefix) - len(suffix));
            if (StrEq(type, StrL("Length")) ||
                StrEq(type, StrL("DefiniteLength")) ||
                StrEq(type, StrL("AbsoluteLength")) ||
                StrEq(type, StrL("Color")) || StrEq(type, StrL("number")) ||
                StrEq(type, StrL("string"))) {
                VecAppend(out->param, name);
                VecAppend(out->paramType, type);
                continue;
            }
        }
        VecAppend(out->other, name);
    }
}

static SpecOp ParamOp(Str name, Bridged* args, int count) {
    SpecOp op;
    op.kind = SpecOpKind::ParamStyle;
    op.name = name;
    op.args = args;
    op.argCount = count;
    return op;
}

static bool ApplyParamValue(El* e, Str name, Bridged value, ShellError* error) {
    SpecOp op = ParamOp(name, &value, 1);
    return ApplyParamStyle(e, op, error);
}

static void EveryDeclaredStyleIsAccepted() {
    StrBuilder builtin;
    AppendBuiltinTypeDeclarations(&builtin);
    DeclaredStyles declared;
    ReadDeclaredStyles(Str(builtin.els, builtin.len), &declared);
    // Rust's the_reflection_table_is_populated, against the declarations.
    utassert(len(declared.nullary) > 3000);

    Arena* arena = ArenaNew();
    int refused = 0;
    int behaviors = 0;
    for (Str name : declared.nullary) {
        if (IsNullaryBehavior(name)) {
            behaviors++;
            // every_parametric_name_is_bound_and_disjoint_from_reflection, for
            // the behaviors: a name is a style or a behavior, never both.
            utassert(!IsNullaryStyleName(name));
            continue;
        }
        El* e = Div(arena);
        if (!IsNullaryStyleName(name) || !ApplyNullaryStyle(e, name)) {
            if (refused++ < 8) {
                printf("  nullary style refused: %.*s\n", len(name), name.s);
            }
        }
    }
    utassert(refused == 0);
    utassert(behaviors == (int)dimof(kNullaryBehaviors));

    // Style.rs PARAM_STYLES: 59 names, each bound, each accepting a value of
    // the type its declaration names.
    utassert(len(declared.param) == 59);
    int paramRefused = 0;
    for (int i = 0; i < len(declared.param); i++) {
        Str name = declared.param[i];
        Str type = declared.paramType[i];
        Bridged values[3];
        int count = 0;
        if (StrEq(type, StrL("Length"))) {
            values[count++] = Bridged::Number(12);
            values[count++] = Bridged::String(StrL("50%"));
            values[count++] = Bridged::String(StrL("auto"));
        } else if (StrEq(type, StrL("DefiniteLength"))) {
            values[count++] = Bridged::Number(12);
            values[count++] = Bridged::String(StrL("50%"));
        } else if (StrEq(type, StrL("AbsoluteLength"))) {
            values[count++] = Bridged::Number(8);
            values[count++] = Bridged::String(StrL("0.5rem"));
        } else if (StrEq(type, StrL("Color"))) {
            values[count++] = Bridged::String(StrL("#ff0000"));
        } else if (StrEq(type, StrL("number"))) {
            values[count++] =
                Bridged::Number(StrEq(name, StrL("font_weight")) ? 600 : 1);
        } else {
            values[count++] = Bridged::String(StrL("monospace"));
        }
        utassert(IsParamStyleName(name));
        utassert(!IsNullaryStyleName(name));
        for (int v = 0; v < count; v++) {
            El* e = Div(arena);
            ShellError error = {};
            if (!ApplyParamValue(e, name, values[v], &error) || error.IsSet()) {
                if (paramRefused++ < 8) {
                    printf("  param style refused: %.*s (%.*s): %.*s\n",
                           len(name), name.s, len(type), type.s,
                           len(error.message), error.message.s);
                }
            }
            ShellErrorClear(&error);
        }
    }
    utassert(paramRefused == 0);
    // No other method on the element reads as a style.
    int stray = 0;
    for (Str name : declared.other) {
        if (IsParamStyleName(name) || IsNullaryStyleName(name)) {
            stray++;
        }
    }
    utassert(stray == 0);
    ArenaDelete(arena);
}

static El* Styled(Arena* arena, const char* names) {
    El* e = Div(arena);
    const char* at = names;
    while (*at) {
        const char* end = strchr(at, ' ');
        int n = end ? (int)(end - at) : (int)strlen(at);
        utassert(ApplyNullaryStyle(e, Str((char*)at, n)));
        at += n;
        while (*at == ' ') {
            at++;
        }
    }
    return e;
}

// The numbers GPUI's style macros give each ramp: rems(n / 4) at a 16px rem
// for the spacing scale, px for borders, the corner ramp in rems.
static void NullaryStylesCarryGpuiValues() {
    Arena* arena = ArenaNew();
    El* e = Styled(arena, "w_4 h_0p5 min_w_1p5 max_w_128");
    utassertnear(e->style.width, 16);
    utassertnear(e->style.height, 2);
    utassertnear(e->style.minW, 6);
    utassertnear(e->style.maxW, 512);

    e = Styled(arena, "w_px h_full size_auto");
    utassertnear(e->style.width, kAuto);
    utassertnear(e->style.height, kAuto);
    e = Styled(arena, "w_px h_full");
    utassertnear(e->style.width, 1);
    utassertnear(e->style.height, kFill);

    e = Styled(arena, "w_1_2 h_1_3 max_w_2_3 min_h_full");
    utassertnear(e->style.widthFrac, 0.5f);
    utassertnear(e->style.heightFrac, 1.f / 3.f);
    utassertnear(e->style.maxWFrac, 2.f / 3.f);
    utassertnear(e->style.minH, kFill);
    e = Styled(arena, "w_5_6 w_3");
    utassertnear(e->style.widthFrac, 0);
    utassertnear(e->style.width, 12);

    // Negative variants: margins and insets move, sizes cannot go below 0.
    e = Styled(arena, "mt_neg_2 mx_neg_px inset_neg_1 left_neg_0p5 w_neg_4");
    utassertnear(e->style.margin.top, -8);
    utassertnear(e->style.margin.left, -1);
    utassertnear(e->style.margin.right, -1);
    utassertnear(e->style.absTop, -4);
    utassertnear(e->style.absBottom, -4);
    utassertnear(e->style.absLeft, -2);
    utassertnear(e->style.width, 0);

    e = Styled(arena, "top_1_2 right_full inset_auto");
    utassertnear(e->style.absTop, kAuto);
    utassertnear(e->style.absTopRel, 0);
    e = Styled(arena, "top_1_2 right_full");
    utassertnear(e->style.absTopRel, 0.5f);
    utassertnear(e->style.absRightRel, 1);

    e = Styled(arena, "m_2 ml_auto");
    utassert(e->style.marginAuto == kMarginAutoL);
    utassertnear(e->style.margin.top, 8);
    e = Styled(arena, "mx_auto ml_4");
    utassert(e->style.marginAuto == kMarginAutoR);
    utassertnear(e->style.margin.left, 16);

    e = Styled(arena, "p_2p5 px_3 pt_neg_1");
    utassertnear(e->style.pad.bottom, 10);
    utassertnear(e->style.pad.left, 12);
    utassertnear(e->style.pad.right, 12);
    utassertnear(e->style.pad.top, -4);

    e = Styled(arena, "gap_1p5 gap_x_4");
    utassertnear(e->style.gapX, 16);
    utassertnear(e->style.gapY, 6);
    // gap no longer turns an element into a flex container; `flex()` does.
    utassert(e->style.display == Display::Block);

    e = Styled(arena, "rounded_md");
    utassertnear(e->style.radius, 6);
    utassert(!e->style.hasCorners);
    e = Styled(arena, "rounded_sm rounded_t_lg rounded_br_none");
    utassert(e->style.hasCorners);
    utassertnear(e->style.corners.tl, 8);
    utassertnear(e->style.corners.tr, 8);
    utassertnear(e->style.corners.bl, 4);
    utassertnear(e->style.corners.br, 0);
    e = Styled(arena, "rounded_t_lg rounded_full");
    utassert(!e->style.hasCorners);
    utassertnear(e->style.radius, 9999);
    e = Styled(arena, "rounded_2xl");
    utassertnear(e->style.radius, 16);

    e = Styled(arena, "border_2");
    utassertnear(e->style.border, 2);
    e = Styled(arena, "border_t_4 border_x_32");
    utassertnear(e->style.borderT, 4);
    utassertnear(e->style.borderL, 32);
    utassertnear(e->style.borderR, 32);
    utassertnear(e->style.borderB, 0);
    // Narrowing one edge of an all-round border narrows only that edge.
    e = Styled(arena, "border_2 border_t_0");
    utassertnear(e->style.border, 0);
    utassertnear(e->style.borderT, 0);
    utassertnear(e->style.borderB, 2);
    utassertnear(e->style.borderL, 2);
    e = Styled(arena, "border_t_4 border_1");
    utassertnear(e->style.border, 1);
    utassertnear(e->style.borderT, 0);

    e = Styled(arena, "text_xs");
    utassertnear(e->style.fontSize, 12);
    e = Styled(arena, "text_3xl");
    utassertnear(e->style.fontSize, 30);
    e = Styled(arena,
               "text_lg font_semibold italic not_italic underline "
               "text_decoration_none line_through text_center");
    utassertnear(e->style.fontSize, 18);
    utassert(e->style.fontWeight == 600);
    utassert(!e->style.italic);
    utassert(!e->style.underline);
    utassert(e->style.strike);
    utassert(e->style.textAlign == (uint8_t)TextAlign::Center + 1);

    e = Styled(arena, "shadow_md");
    utassert(e->style.shadowCount == 2);
    if (e->style.shadowCount == 2) {
        utassertnear(e->style.shadows[0].y, 4);
        utassertnear(e->style.shadows[0].blur, 6);
        utassertnear(e->style.shadows[0].spread, -1);
        utassertnear(e->style.shadows[1].spread, -2);
        // hsla(0, 0, 0, 0.1), through eight bits.
        utassert(abs((int)e->style.shadows[0].color.a - 26) <= 1);
    }
    e = Styled(arena, "shadow_2xl shadow_none");
    utassert(e->style.shadowCount == 0);

    e = Styled(arena, "cursor_grab");
    utassert(e->cursor == CursorKind::OpenHand);
    e = Styled(arena, "cursor_move");
    utassert(e->cursor == CursorKind::ClosedHand);
    e = Styled(arena, "cursor_nwse_resize");
    utassert(e->cursor == CursorKind::ResizeUpLeftDownRight);

    // Alignment keywords set what they name and leave display alone.
    e = Styled(arena, "items_baseline justify_evenly content_between");
    utassert(e->style.display == Display::Block);
    utassert(e->style.align == FlexAlign::Baseline);
    utassert(e->style.justify == Justify::SpaceEvenly);
    utassert(e->style.alignContent ==
             (uint8_t)taffy::AlignContentKeyword::SpaceBetween + 1);
    e = Styled(arena, "content_center content_normal");
    utassert(e->style.alignContent == 0);
    e = Styled(arena, "h_flex");
    utassert(e->style.display == Display::Flex);
    utassert(e->style.dir == FlexDir::Row);
    utassert(e->style.align == FlexAlign::Center);
    e = Styled(arena, "flex flex_wrap_reverse self_baseline");
    utassert(e->style.flexWrap && e->style.flexWrapReverse);
    utassert(e->style.hasAlignSelf);
    utassert(e->style.alignSelf == FlexAlign::Baseline);
    e = Styled(arena, "flex_auto");
    utassertnear(e->style.flexGrow, 1);
    utassertnear(e->style.flexShrink, 1);
    utassertnear(e->style.flexBasis, kAuto);
    e = Styled(arena, "flex_1 flex_initial");
    utassertnear(e->style.flexGrow, 0);
    utassertnear(e->style.flexBasis, kAuto);

    e = Styled(arena, "hidden");
    utassert(e->style.display == Display::None);
    e = Styled(arena, "grid");
    utassert(e->style.display == Display::Grid);
    e = Styled(arena, "invisible");
    utassert(e->style.invisible);
    utassertnear(e->style.opacity, 1);
    e = Styled(arena, "invisible visible");
    utassert(!e->style.invisible);
    e = Styled(arena, "absolute relative");
    utassert(!e->style.absolute);
    e = Styled(arena, "truncate");
    utassert(e->style.truncate && !e->style.wrap);
    utassert(e->style.overflowX == Overflow::Hidden);
    e = Styled(arena, "aspect_square border_dashed");
    utassertnear(e->style.aspect, 1);
    utassert(e->style.borderDashed);

    // Names off the ramps are not styles.
    static const char* const refused[] = {
        "w_13",           "w_04",
        "w_1_7",          "w_neg_auto",
        "p_auto",         "gap_auto",
        "border_13",      "border_0p5",
        "rounded_huge",   "rounded_4",
        "gap_x",          "w",
        "rounded",        "text_decoration_3",
        "border_x_neg_2", "rounded_neg_md",
        "w_4p5",          "font_heavy",
        "grow",           "shrink_0",
        "fixed",
    };
    for (const char* name : refused) {
        utassert(!IsNullaryStyleName(Str(name)));
        utassert(!ApplyNullaryStyle(Div(arena), Str(name)));
    }
    ArenaDelete(arena);
}

static bool ParamRefused(Arena* arena, const char* name, Bridged value,
                         const char* needle) {
    ShellError error = {};
    bool ok = ApplyParamValue(Div(arena), Str(name), value, &error);
    bool matched = !ok && error.IsSet() && StrFind(error.message, needle) >= 0;
    if (!matched) {
        printf("  %s: %.*s\n", name, len(error.message), error.message.s);
    }
    ShellErrorClear(&error);
    return matched;
}

// style.rs's tests of apply_param, and the lengths each parametric method
// takes.
static void ParamStylesFollowTheLengthGrammar() {
    Arena* arena = ArenaNew();
    ShellError error = {};
    El* e = Div(arena);
    utassert(ApplyParamValue(e, StrL("p"), Bridged::Number(12), &error));
    utassertnear(e->style.pad.top, 12);
    utassert(
        ApplyParamValue(e, StrL("w"), Bridged::String(StrL("50%")), &error));
    utassertnear(e->style.widthFrac, 0.5f);
    utassert(
        ApplyParamValue(e, StrL("w"), Bridged::String(StrL("auto")), &error));
    utassertnear(e->style.width, kAuto);
    utassertnear(e->style.widthFrac, 0);
    utassert(
        ApplyParamValue(e, StrL("h"), Bridged::String(StrL(" 25% ")), &error));
    utassertnear(e->style.heightFrac, 0.25f);
    utassert(ApplyParamValue(e, StrL("size"), Bridged::String(StrL("1rem")),
                             &error));
    utassertnear(e->style.width, 16);
    utassertnear(e->style.height, 16);
    utassert(
        ApplyParamValue(e, StrL("m"), Bridged::String(StrL("auto")), &error));
    utassert(e->style.marginAuto ==
             (kMarginAutoL | kMarginAutoR | kMarginAutoT | kMarginAutoB));
    utassert(
        ApplyParamValue(e, StrL("top"), Bridged::String(StrL("50%")), &error));
    utassertnear(e->style.absTopRel, 0.5f);
    utassert(ApplyParamValue(e, StrL("rounded_tl"),
                             Bridged::String(StrL("12px")), &error));
    utassert(e->style.hasCorners);
    utassertnear(e->style.corners.tl, 12);
    utassert(ApplyParamValue(e, StrL("border_x"), Bridged::Number(3), &error));
    utassertnear(e->style.borderL, 3);
    utassertnear(e->style.borderR, 3);
    utassert(
        ApplyParamValue(e, StrL("line_height"), Bridged::Number(1.45), &error));
    utassertnear(e->style.lineHeight, 1.45f);
    utassert(
        ApplyParamValue(e, StrL("font_weight"), Bridged::Number(600), &error));
    utassert(e->style.fontWeight == 600);
    utassert(ApplyParamValue(e, StrL("bg"), Bridged::String(StrL("#ff0000")),
                             &error));
    utassert(e->style.hasBg && e->style.bg.color.r == 255);
    utassert(ApplyParamValue(e, StrL("flex_basis"),
                             Bridged::String(StrL("50%")), &error));
    utassertnear(e->style.flexBasisFrac, 0.5f);
    utassert(!error.IsSet());

    // font_weight_sets_gpui_font_weight_and_rejects_out_of_range_values
    utassert(ParamRefused(arena, "font_weight", Bridged::Number(99),
                          "between 100 and 900"));
    utassert(ParamRefused(arena, "font_weight", Bridged::Number(901),
                          "between 100 and 900"));
    // a_wrongly_typed_argument_names_the_expected_type
    utassert(
        ParamRefused(arena, "bg", Bridged::Number(1), "expected a string"));
    utassert(ParamRefused(arena, "p", Bridged::String(StrL("auto")),
                          "definite length"));
    utassert(ParamRefused(arena, "gap_x", Bridged::String(StrL("auto")),
                          "definite length"));
    utassert(ParamRefused(arena, "rounded", Bridged::String(StrL("50%")),
                          "absolute length"));
    utassert(ParamRefused(arena, "border_t", Bridged::String(StrL("auto")),
                          "absolute length"));
    utassert(ParamRefused(arena, "w", Bridged::String(StrL("wide")),
                          "expects a length"));
    utassert(ParamRefused(arena, "w", Bridged::String(StrL("xpx")),
                          "could not read a number"));
    utassert(ParamRefused(arena, "opacity", Bridged::String(StrL("1")),
                          "expected a number"));
    // a_missing_argument_names_the_method
    SpecOp missing = ParamOp(StrL("p"), nullptr, 0);
    utassert(!ApplyParamStyle(Div(arena), missing, &error));
    utassert(StrFind(error.message, "`p` expects at least 1 argument") >= 0);
    ShellErrorClear(&error);
    // A name that is not a parametric style is not refused, only not taken.
    Bridged one = Bridged::Number(1);
    SpecOp unknown = ParamOp(StrL("tooltip"), &one, 1);
    utassert(!ApplyParamStyle(Div(arena), unknown, &error));
    utassert(!error.IsSet());
    ArenaDelete(arena);
}

// Through the whole path: scripts chaining every declared style onto a div,
// recorded by QuickJS and materialized. In batches, since a render has a 50ms
// budget and a few thousand children in one would be racing it.
static void AScriptMayCallEveryDeclaredStyle() {
    StrBuilder builtin;
    AppendBuiltinTypeDeclarations(&builtin);
    DeclaredStyles declared;
    ReadDeclaredStyles(Str(builtin.els, builtin.len), &declared);

    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    const int kBatch = 250;
    int failed = 0;
    for (int first = 0; runtime && first < len(declared.nullary);
         first += kBatch) {
        StrBuilder source;
        source.Append(
            StrL("import { View, div } from 'gpui-kit';\n"
                 "export default class Main extends View { render(cx) {\n"
                 "  return div()"));
        for (int i = first; i < first + kBatch && i < len(declared.nullary);
             i++) {
            source.Append(StrL("\n    .child(div()."));
            source.Append(declared.nullary[i]);
            source.Append(StrL("())"));
        }
        source.Append(StrL(";\n} }\n"));
        ViewType* type = runtime
                             ->LoadSource(Str(fmt("styles%d.js", first)),
                                          Str(source.els, source.len), &error);
        ViewObject* object =
            type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
                 : nullptr;
        RenderSnapshot* snapshot =
            object ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr,
                                            &error)
                   : nullptr;
        Arena* frame = ArenaNew();
        Ctx cx = {&app, &window, frame, {}};
        El* root = snapshot ? ShellMaterialize(&cx, runtime, snapshot, &error)
                            : nullptr;
        if (!root || error.IsSet()) {
            failed++;
            printf("  styles %d: %.*s\n", first, len(error.message),
                   error.message.s);
        }
        ArenaDelete(frame);
        delete snapshot;
        ViewObjectRelease(object);
        ViewTypeRelease(type);
        ShellErrorClear(&error);
    }
    utassert(failed == 0);
    if (runtime) runtime->Release();
    AppGlobalClear(&app);
}

// style.rs a_close_typo_gets_a_suggestion and
// a_name_with_nothing_close_gets_no_suggestion; known_names_covers_both_halves
// as "every declared name is its own closest name".
static void ACloseTypoGetsASuggestion() {
    utassert(StrEq(StyleSuggestTemp(StrL("items_centre")), "items_center"));
    utassert(StrEq(StyleSuggestTemp(StrL("text_colour")), "text_color"));
    utassert(StrEq(StyleSuggestTemp(StrL("rounde")), "rounded"));
    utassert(len(StyleSuggestTemp(StrL("on_click"))) == 0);
    utassert(len(StyleSuggestTemp(StrL("completely_unrelated_name"))) == 0);
    // Equally close names: the alphabetically first, as Rust's sorted list
    // gives it.
    utassert(StrEq(StyleSuggestTemp(StrL("w_neg_7p5")), "w_neg_0p5"));

    StrBuilder builtin;
    AppendBuiltinTypeDeclarations(&builtin);
    DeclaredStyles declared;
    ReadDeclaredStyles(Str(builtin.els, builtin.len), &declared);
    int missing = 0;
    // A sample: every name against every name is millions of distances.
    for (int i = 0; i < len(declared.nullary); i += 23) {
        Str name = declared.nullary[i];
        if (IsNullaryBehavior(name)) continue;
        if (!StrEq(StyleSuggestTemp(name), name) && missing++ < 8) {
            printf("  not a known name: %.*s\n", len(name), name.s);
        }
    }
    for (Str name : declared.param) {
        if (!StrEq(StyleSuggestTemp(name), name) && missing++ < 8) {
            printf("  not a known name: %.*s\n", len(name), name.s);
        }
    }
    utassert(missing == 0);

    // The engine's unknown_method, with and without a candidate.
    utassert(StrEq(UnknownElementMethodTemp(StrL("items_centre")),
                   "unknown element method `items_centre` (did you mean "
                   "`items_center`?)"));
    utassert(StrStartsWith(UnknownElementMethodTemp(StrL("on_klick_me_now")),
                           "unknown element method `on_klick_me_now`; it is "
                           "neither a style method"));
}

// render.rs an_unknown_style_method_suggests_the_closest_name: a typo fails
// the render, and the error names what was meant.
static void AnUnknownStyleMethodSuggestsTheClosestName() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (!runtime) return;
    Str source = StrL(
        "import { View, div } from 'gpui-kit';\n"
        "export default class Typo extends View {\n"
        "  render() { return div().items_centre(); }\n"
        "}\n");
    ViewType* type = runtime->LoadSource(StrL("typo.js"), source, &error);
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    RenderSnapshot* snapshot =
        object
            ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr, &error)
            : nullptr;
    Arena* frame = ArenaNew();
    Ctx cx = {&app, &window, frame, {}};
    if (snapshot) ShellMaterialize(&cx, runtime, snapshot, &error);
    utassert(error.IsSet());
    utassert(StrContains(error.message, StrL("items_center")));
    ArenaDelete(frame);
    delete snapshot;
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    ShellErrorClear(&error);
    runtime->Release();
    AppGlobalClear(&app);
}

} // namespace shell_style_tests

void TestShellStyle() {
    TestSuite("shell_style");
    shell_style_tests::EveryDeclaredStyleIsAccepted();
    shell_style_tests::NullaryStylesCarryGpuiValues();
    shell_style_tests::ParamStylesFollowTheLengthGrammar();
    shell_style_tests::AScriptMayCallEveryDeclaredStyle();
    shell_style_tests::ACloseTypoGetsASuggestion();
    shell_style_tests::AnUnknownStyleMethodSuggestsTheClosestName();
}
