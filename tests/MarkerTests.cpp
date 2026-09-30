/* Ported from the tests in crates/ui/src/marker.rs: test_marker_builder and
 * test_marker_resolved_alignment. */

#include "Test.h"

using namespace gpui::component;

static void TheBuilderCarriesVariantLoadingAndSlots() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Marker* marker =
        Marker::New(&cx)
            ->WithVariant(MarkerVariant::Separator)
            ->Loading(true)
            ->WithLoadingStyle(MarkerLoadingStyle::Shimmer)
            ->WithShimmerStyle(ShimmerStyle::New().Reverse(true))
            ->SeparatorStyle(Style{}, 0)
            ->Content(MarkerContent::New(&cx)->Child(TextEl(a, StrL("Today"))));

    utassert(marker->variant == MarkerVariant::Separator);
    utassert(marker->loading);
    utassert(marker->loadingStyle == MarkerLoadingStyle::Shimmer);
    utassert(marker->children.len == 1);
    utassert(Marker::New(&cx)->variant == MarkerVariant::Plain);
    utassert(!Marker::New(&cx)->loading);
    utassert(Marker::New(&cx)->loadingStyle == MarkerLoadingStyle::Spinner);
    utassert(!Marker::New(&cx)->hasAlignment);

    Marker* centered = Marker::New(&cx)->Alignment(MarkerAlignment::Center);
    utassert(centered->hasAlignment &&
             centered->alignment == MarkerAlignment::Center);

    Marker* contentFirst =
        Marker::New(&cx)
            ->Content(MarkerContent::New(&cx)->Text(StrL("Thinking")))
            ->WithLoadingStyle(MarkerLoadingStyle::Shimmer)
            ->Loading(true);
    utassert(contentFirst->loading);
    utassert(contentFirst->loadingStyle == MarkerLoadingStyle::Shimmer);
    utassert(contentFirst->children[0].content != nullptr);

    Marker* customIcon =
        Marker::New(&cx)
            ->Loading(true)
            ->Icon(MarkerIcon::New(&cx)->Child(TextEl(a, StrL("custom"))))
            ->Content(MarkerContent::New(&cx)->Text(StrL("Loading")));
    utassert(customIcon->children.len == 2);
    utassert(customIcon->children[0].icon != nullptr);
    // The spinner slot is only added when nothing else filled the icon slot.
    El* customRow = customIcon->IntoEl();
    int customChildren = 0;
    for (El* child = customRow->first; child; child = child->next) {
        customChildren++;
    }
    utassert(customChildren == 2);

    // A loading Spinner marker with no icon of its own grows one.
    El* spinnerRow = Marker::New(&cx)
                         ->Loading(true)
                         ->Content(MarkerContent::New(&cx)->Text(StrL("Wait")))
                         ->IntoEl();
    int spinnerChildren = 0;
    for (El* child = spinnerRow->first; child; child = child->next) {
        spinnerChildren++;
    }
    utassert(spinnerChildren == 2);

    utassert(Marker::New(&cx)->role.kind == RoleOverrideKind::Implicit);
    utassert(!Marker::New(&cx)->hasId);
    Marker* status =
        Marker::New(&cx)
            ->Id(StrL("sync-status"))
            ->Role(RoleOverride::Explicit(AccessibilityRole::Status));
    utassert(status->hasId && base::StrEq(status->id, StrL("sync-status")));
    utassert(status->role.kind == RoleOverrideKind::Role);
    utassert(status->role.role == AccessibilityRole::Status);

    Style faded = {};
    faded.opacity = 0.37f;
    Marker* styled = Marker::New(&cx)
                         ->Refine(faded, StyleFieldOpacity)
                         ->Child(TextEl(a, StrL("Status")))
                         ->Child(TextEl(a, StrL("Details")));
    utassert((styled->styleSet & StyleFieldOpacity) != 0);
    utassertnear(styled->style.opacity, 0.37f);
    utassert(styled->children.len == 2);

    MarkerIcon* icon = MarkerIcon::New(&cx)->Child(TextEl(a, StrL("icon")));
    utassert(icon->children.len == 1);

    MarkerContent* content = MarkerContent::New(&cx)
                                 ->Text(StrL("Thinking"))
                                 ->Child(TextEl(a, StrL("…")))
                                 ->Text(StrL("正在思考"));
    utassert(content->children.len == 3);
    utassert(content->children[0].isText);
    utassert(!content->children[1].isText);
    utassert(content->children[2].isText);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// The variants' own geometry, which the builder test cannot see: a separator
// puts a rule either side of its content, and a border draws a bottom edge.
static void TheVariantsDrawTheirOwnDecoration() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;
    const Theme& th = ThemeNow(&app);

    El* plain =
        Marker::New(&cx)
            ->Content(MarkerContent::New(&cx)->Child(TextEl(a, StrL("x"))))
            ->IntoEl();
    utassertnear(plain->style.gapX, 8.f);
    utassertnear(plain->style.minH, 16.f);
    utassertnear(plain->style.fontSize, 14.f);
    utassertnear(plain->style.lineHeight, 1.5f);
    utassert(plain->first && plain->first->next == nullptr);

    El* separator =
        Marker::New(&cx)
            ->WithVariant(MarkerVariant::Separator)
            ->Content(MarkerContent::New(&cx)->Child(TextEl(a, StrL("x"))))
            ->IntoEl();
    utassert(separator->style.justify == Justify::Center);
    int rules = 0;
    for (El* child = separator->first; child; child = child->next) {
        if (child->style.height == 1) {
            rules++;
            utassert(RgbaEq(child->style.bg.color, th.border));
        }
    }
    utassert(rules == 2);

    El* border =
        Marker::New(&cx)
            ->WithVariant(MarkerVariant::Border)
            ->Content(MarkerContent::New(&cx)->Child(TextEl(a, StrL("x"))))
            ->IntoEl();
    utassertnear(border->style.borderB, 1.f);
    utassertnear(border->style.pad.bottom, 8.f);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// test_marker_resolved_alignment.
static void TheResolvedAlignmentFollowsTheVariantUnlessSet() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    // Unset: only the separator centers its label.
    utassert(Marker::New(&cx)->ResolvedAlignment() == MarkerAlignment::Start);
    utassert(Marker::New(&cx)
                 ->WithVariant(MarkerVariant::Border)
                 ->ResolvedAlignment() == MarkerAlignment::Start);
    utassert(Marker::New(&cx)
                 ->WithVariant(MarkerVariant::Separator)
                 ->ResolvedAlignment() == MarkerAlignment::Center);

    // Explicit alignment wins over the variant default, in either order.
    utassert(Marker::New(&cx)
                 ->Alignment(MarkerAlignment::End)
                 ->WithVariant(MarkerVariant::Separator)
                 ->ResolvedAlignment() == MarkerAlignment::End);
    utassert(Marker::New(&cx)
                 ->WithVariant(MarkerVariant::Plain)
                 ->Alignment(MarkerAlignment::Center)
                 ->ResolvedAlignment() == MarkerAlignment::Center);

    // A separator keeps only the line on the far side of its label, and the
    // row places its children where the alignment says.
    const MarkerAlignment aligns[] = {MarkerAlignment::Start,
                                      MarkerAlignment::End};
    for (MarkerAlignment align : aligns) {
        El* row =
            Marker::New(&cx)
                ->WithVariant(MarkerVariant::Separator)
                ->Alignment(align)
                ->Content(MarkerContent::New(&cx)->Child(TextEl(a, StrL("x"))))
                ->IntoEl();
        utassert(
            row->style.justify ==
            (align == MarkerAlignment::Start ? Justify::Start : Justify::End));
        int rules = 0;
        for (El* child = row->first; child; child = child->next) {
            if (child->style.height == 1) {
                rules++;
            }
        }
        utassert(rules == 1);
        bool ruleFirst = row->first->style.height == 1;
        utassert(ruleFirst == (align == MarkerAlignment::End));
    }

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static El* FirstTextEl(El* e) {
    if (!e) {
        return nullptr;
    }
    if (e->kind == ElKind::Text) {
        return e;
    }
    for (El* c = e->first; c; c = c->next) {
        if (El* t = FirstTextEl(c)) {
            return t;
        }
    }
    return nullptr;
}

// marker.rs text_left / text_center / text_right on the content: a label
// that wraps puts every one of its lines at that edge of its box, and a
// click on a line lands on the glyph drawn there.
static void AWrappedLabelAlignsEveryLine() {
    App app = {};
    component::Init(&app);
    app.paint = PaintAppNew();
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    win->paint.pa = app.paint;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    PaintCtx* pc = &win->paint;

    const MarkerAlignment aligns[] = {
        MarkerAlignment::Start, MarkerAlignment::Center, MarkerAlignment::End};
    for (MarkerAlignment align : aligns) {
        Str label = StrL(
            "A marker label long enough to wrap onto several "
            "lines of uneven width here");
        El* row = Marker::New(&cx)
                      ->Alignment(align)
                      ->Content(MarkerContent::New(&cx)->Text(label))
                      ->IntoEl();
        El* root = Div(a)->W(180)->Child(row);
        LayoutEl(pc, root, 0, 0, 180, 600, 14, Rgba{});
        El* t = FirstTextEl(row);
        utassert(t != nullptr);
        if (!t) {
            continue;
        }
        Bounds lines[16] = {};
        int n = ElTextRangeRects(pc, t, 0, len(t->text), lines, 16);
        utassert(n >= 2);
        // A wrapped line's rect keeps the space the wrap left behind, which
        // the alignment does not count; allow for it.
        const float slack = 6.f;
        bool pushed = false;
        for (int i = 0; i < n; i++) {
            float left = lines[i].x - t->x;
            float right = t->x + t->w - (lines[i].x + lines[i].w);
            if (align == MarkerAlignment::Start) {
                utassert(left >= -0.5f && left < 0.5f);
            } else if (align == MarkerAlignment::Center) {
                utassert(left - right > -slack && left - right < slack);
            } else {
                utassert(right > -slack && right < slack);
            }
            if (left > 2.f) {
                pushed = true;
            }
        }
        // Lines of different widths: something moved off the leading edge
        // exactly when the label is not start-aligned.
        utassert(pushed == (align != MarkerAlignment::Start));

        // The hit test reads the same line positions the paint does: just
        // inside the second line's leading edge is that line's first glyph.
        float relX = lines[1].x - t->x + 1.f;
        float relY = lines[1].y - t->y + lines[1].h * 0.5f;
        TextAlign ta = align == MarkerAlignment::Start    ? TextAlign::Left
                       : align == MarkerAlignment::Center ? TextAlign::Center
                                                          : TextAlign::Right;
        int at =
            TextIndexAt(pc, t->text, t->laidFont, t->laidMaxW, t->style.wrap,
                        relX, relY, false, t->style.lineHeight, ta);
        Bounds glyph = {};
        utassert(ElTextRangeRects(pc, t, at, at + 1, &glyph, 1) == 1);
        utassertnear(glyph.x, lines[1].x);
        a->Reset();
    }

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    AppGlobalClear(&app);
    PaintAppFree(app.paint);
}

// The runtime half on its own: a run that does not wrap but is given a wider
// box sits at the box's far edge, and what the scene records for it covers
// where it was drawn.
static void AnAlignedRunCoversWhereItIsDrawn() {
    PaintApp* pa = PaintAppNew();
    utassert(pa);
    if (!pa) {
        return;
    }
    PaintCtx paint = {};
    paint.pa = pa;
    Size plain = {};
    TextLayout* left =
        TextLayoutNew(&paint, StrL("edge"), 14, 0, false, 0, 0, &plain);
    Size size = {};
    TextLayout* right = TextLayoutNew(&paint, StrL("edge"), 14, 200, false, 0,
                                      0, &size, TextAlign::Right);
    utassert(left && right);
    if (left && right) {
        // Alignment does not change what the text measures.
        utassertnear(size.w, plain.w);
        Bounds r = {};
        utassert(TextLayoutRangeRects(right, StrL("edge"), 0, 4, &r, 1) == 1);
        utassert(r.x + r.w > 199.f && r.x + r.w < 201.f);
        utassert(TextLayoutSize(right).w > 199.f);
        utassert(TextLayoutHitPoint(right, StrL("edge"), r.x + 0.5f, 5) == 0);
        utassert(TextLayoutHitPoint(right, StrL("edge"), 10, 5) == 0);
        utassertnear(TextLayoutSize(left).w, plain.w);
    }
    TextLayoutRelease(left);
    TextLayoutRelease(right);
    PaintAppFree(pa);
}

void TestMarker() {
    TestSuite("marker");
    TheBuilderCarriesVariantLoadingAndSlots();
    TheVariantsDrawTheirOwnDecoration();
    TheResolvedAlignmentFollowsTheVariantUnlessSet();
    AWrappedLabelAlignsEveryLine();
    AnAlignedRunCoversWhereItIsDrawn();
}
