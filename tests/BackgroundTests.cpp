/* gpui::Background — the fill a `.bg(..)` takes, and where a CSS
   linear-gradient puts its two ends inside a box. Upstream's own tests for
   this live in crates/ui/src/theme/color.rs (the parser, tested in
   ThemeRegistryTests) and in gpui's geometry; the line arithmetic is the
   part this tree writes out, so it is what is checked here. */

#include "Test.h"

static bool Same(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void ASolidFillIsJustAColor() {
    Background b = Rgb(0x12, 0x34, 0x56);
    utassert(BackgroundIsSolid(b));
    utassert(Same(b.color, Rgb(0x12, 0x34, 0x56)));
    // The default is `to bottom`, which is what a two-argument
    // linear-gradient means.
    utassertnear(b.angle, 180.f);
}

static void AGradientKeepsItsFirstStopAsItsFlatColor() {
    Rgba from = Rgb(0x1e, 0x29, 0x3b), to = Rgb(0x0f, 0x17, 0x2a);
    Background b =
        BackgroundLinear(180.f, ColorStopAt(from, 0.f), ColorStopAt(to, 1.f));
    utassert(b.gradient && !BackgroundIsSolid(b));
    // try_parse_theme_color: the solid colour a gradient stands in for.
    utassert(Same(b.color, from));
    utassert(Same(b.to.color, to));
}

// The gradient line: the four cardinal angles, and the corner case that gives
// the rule its name.
static void TheLineRunsThroughTheCenterAtTheAngle() {
    Bounds box = {0, 0, 100, 100};
    Point p0 = {}, p1 = {};
    Rgba a = Rgb(0, 0, 0), b = Rgb(255, 255, 255);

    // 180deg / `to bottom`: down the middle, top edge to bottom edge.
    BackgroundLine(
        BackgroundLinear(180.f, ColorStopAt(a, 0.f), ColorStopAt(b, 1.f)), box,
        &p0, &p1);
    utassertnear(p0.x, 50.f);
    utassertnear(p0.y, 0.f);
    utassertnear(p1.x, 50.f);
    utassertnear(p1.y, 100.f);

    // 0deg / `to top` is the same line the other way round.
    BackgroundLine(
        BackgroundLinear(0.f, ColorStopAt(a, 0.f), ColorStopAt(b, 1.f)), box,
        &p0, &p1);
    utassertnear(p0.y, 100.f);
    utassertnear(p1.y, 0.f);

    // 90deg / `to right`.
    BackgroundLine(
        BackgroundLinear(90.f, ColorStopAt(a, 0.f), ColorStopAt(b, 1.f)), box,
        &p0, &p1);
    utassertnear(p0.x, 0.f);
    utassertnear(p0.y, 50.f);
    utassertnear(p1.x, 100.f);
    utassertnear(p1.y, 50.f);

    // 270deg / `to left`.
    BackgroundLine(
        BackgroundLinear(270.f, ColorStopAt(a, 0.f), ColorStopAt(b, 1.f)), box,
        &p0, &p1);
    utassertnear(p0.x, 100.f);
    utassertnear(p1.x, 0.f);
}

static void TheCornersLandOnTheEnds() {
    // 45deg on a square is `to top right`, and CSS makes the line long enough
    // that the two corners it points between are exactly 0% and 100% — a
    // shorter line would leave the corner flat.
    Bounds box = {0, 0, 100, 100};
    Point p0 = {}, p1 = {};
    Rgba a = Rgb(0, 0, 0), b = Rgb(255, 255, 255);
    BackgroundLine(
        BackgroundLinear(45.f, ColorStopAt(a, 0.f), ColorStopAt(b, 1.f)), box,
        &p0, &p1);
    utassertnear(p0.x, 0.f);
    utassertnear(p0.y, 100.f);
    utassertnear(p1.x, 100.f);
    utassertnear(p1.y, 0.f);
}

static void AStopPercentageMovesItsEnd() {
    // The endpoints come back at the stops' own percentages: the backend gets
    // two points and two colours and clamps past both, so a 25%..75% gradient
    // paints the first quarter flat.
    Bounds box = {0, 0, 100, 200};
    Point p0 = {}, p1 = {};
    Rgba a = Rgb(0, 0, 0), b = Rgb(255, 255, 255);
    BackgroundLine(
        BackgroundLinear(180.f, ColorStopAt(a, 0.25f), ColorStopAt(b, 0.75f)),
        box, &p0, &p1);
    utassertnear(p0.y, 50.f);
    utassertnear(p1.y, 150.f);
}

// The box is not always at the origin, and not always square.
static void TheLineFollowsTheBox() {
    Bounds box = {10, 20, 40, 60};
    Point p0 = {}, p1 = {};
    Rgba a = Rgb(0, 0, 0), b = Rgb(255, 255, 255);
    BackgroundLine(
        BackgroundLinear(180.f, ColorStopAt(a, 0.f), ColorStopAt(b, 1.f)), box,
        &p0, &p1);
    utassertnear(p0.x, 30.f);
    utassertnear(p0.y, 20.f);
    utassertnear(p1.y, 80.f);
}

static void FadingScalesEveryStopAndCappingDoesNot() {
    Rgba a = Rgba8(0, 0, 0, 200), b = Rgba8(255, 255, 255, 100);
    Background g =
        BackgroundLinear(180.f, ColorStopAt(a, 0.f), ColorStopAt(b, 1.f));

    // Background::opacity: one factor over all of it, so the two stops keep
    // their ratio.
    Background faded = BackgroundOpacity(g, 0.5f);
    utassert(faded.from.color.a == 100);
    utassert(faded.to.color.a == 50);
    utassert(faded.color.a == 100);

    // try_parse_background_clamped: each stop capped on its own, so the
    // brighter one cannot push the highlight past the cap.
    Background capped = BackgroundClampAlpha(g, 0.5f);
    utassert(capped.from.color.a == 127 || capped.from.color.a == 128);
    utassert(capped.to.color.a == 100);

    // And a solid one is capped the same way.
    Background solid = BackgroundClampAlpha(Background(a), 0.5f);
    utassert(solid.color.a == 127 || solid.color.a == 128);
}

void TestBackground() {
    TestSuite("background");
    ASolidFillIsJustAColor();
    AGradientKeepsItsFirstStopAsItsFlatColor();
    TheLineRunsThroughTheCenterAtTheAngle();
    TheCornersLandOnTheEnds();
    AStopPercentageMovesItsEnd();
    TheLineFollowsTheBox();
    FadingScalesEveryStopAndCappingDoesNot();
}
