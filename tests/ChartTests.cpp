/* crates/ui/src/chart/{radar,sankey}_chart.rs: public label values. */

#include "Test.h"

using namespace gpui::component;

static bool ChartColorEq(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void RadarLabelsRetainTextAndElements() {
    RadarLabel text = RadarLabel::Text(StrL("Sales"));
    El element = {};
    RadarLabel custom = RadarLabel::Element(&element);
    utassert(text.kind == RadarLabel::Kind::Text);
    utassert(base::StrEq(text.text, StrL("Sales")));
    utassert(text.element == nullptr);
    utassert(custom.kind == RadarLabel::Kind::Element);
    utassert(custom.element == &element);
    utassert(!custom.text.s);

    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    float values[3] = {1, 2, 3};
    El* labelElement =
        Div(a)->FlexCol()->Child(TextEl(a, StrL("custom label")));
    RadarLabel labels[3] = {RadarLabel::Text(StrL("one")),
                            RadarLabel::Element(labelElement),
                            RadarLabel::Text(StrL("three"))};
    Rgba red = RgbaHex(0xff0000);
    RadarChart* chart = RadarChart::New(&cx, values, 3)
                            ->Labels(labels)
                            ->LabelColor(red)
                            ->LabelGap(17)
                            ->GridLevels(0);
    El* root = chart->IntoEl();
    utassert(chart->labels == labels);
    utassert(chart->hasLabelColor);
    utassert(ChartColorEq(chart->labelColor, red));
    utassertnear(chart->labelGap, 17);
    utassert(chart->gridLevels == 1);
    utassert(root->customPaint != nullptr);
    utassert(root->customUser == chart);
    utassert(root->first == labelElement);
    utassert(labelElement->style.absolute);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void PlainRadarLabelsProjectToTheTaggedValue() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    float values[3] = {1, 2, 3};
    const char* names[3] = {"one", "two", "three"};
    RadarChart* chart = RadarChart::New(&cx, values, 3)->Labels(names);
    utassert(chart->labels != nullptr);
    for (int i = 0; i < 3; i++) {
        utassert(chart->labels[i].kind == RadarLabel::Kind::Text);
        utassert(base::StrEq(chart->labels[i].text, names[i]));
    }

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void SankeyLabelsCarryIndependentStylesAndDoNotCap() {
    Rgba red = RgbaHex(0xff0000);
    SankeyLabel plain = SankeyLabel::New(StrL("a"));
    SankeyLabel styled = SankeyLabel::New(StrL("b")).Color(red).FontSize(14);
    utassert(base::StrEq(plain.text, StrL("a")));
    utassert(!plain.hasColor);
    utassert(plain.fontSize == 0);
    // plot/label.rs: TEXT_SIZE 10 + TEXT_GAP 2.
    utassertnear(plain.LineHeight(), 12);
    utassert(styled.hasColor);
    utassert(ChartColorEq(styled.color, red));
    utassertnear(styled.LineHeight(), 16);

    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    SankeyChart* chart = SankeyChart::New(&cx)->Node(StrL("ignored"));
    for (int i = 0; i < 40; i++) {
        chart->CustomLabel(i & 1 ? styled : plain);
    }
    const SankeyChartNode& node = chart->nodes[0];
    utassert(node.hasCustomLabels);
    utassert(node.labels.len == 40);
    utassert(base::StrEq(node.labels[0].text, StrL("a")));
    utassert(base::StrEq(node.labels[39].text, StrL("b")));
    utassertnear(node.labels[39].fontSize, 14);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void UnchangedPlotLabelsKeepTheScene() {
#if GPUI_OS_WINDOWS
    TestSuite("plot label scene stability");
    PaintApp* app = PaintAppNew();
    utassert(app);
    if (!app) {
        return;
    }
    Arena* arena = ArenaNew();
    PaintCtx paint = {};
    paint.pa = app;
    paint.viewW = 320;
    paint.viewH = 200;
    paint.opacity = 1;
    plot::PlotLabel labels = plot::PlotLabel::New(arena);
    plot::Text label = plot::Text::New(StrL("axis label"), Point{100, 20},
                                       Rgba8(0, 0, 0, 255));
    label.Align(plot::PlotTextAlign::Center);
    labels.Add(label);
    for (int frame = 0; frame < 4; frame++) {
        TextMeasBeginFrame(&paint);
        scene::FrameBegin(&paint);
        // Colour and alignment are draw inputs, independent of the cached
        // shape. Both must still invalidate the scene when they change.
        if (frame == 2) {
            labels.items[0].color = Rgba8(255, 0, 0, 255);
        } else if (frame == 3) {
            labels.items[0].align = plot::PlotTextAlign::Right;
        }
        labels.Paint(&paint, Bounds{0, 0, 320, 200});
        Bounds damage = {};
        bool changed = scene::FrameEnd(&paint, &damage);
        utassert(changed == (frame != 1));
        TextMeasEndFrame(&paint);
    }
    TextMeasClear(&paint);
    scene::Free(&paint);
    ArenaDelete(arena);
    PaintAppFree(app);
#endif
}

static void UnchangedSankeyLabelsKeepTheScene() {
#if GPUI_OS_WINDOWS
    TestSuite("sankey label scene stability");
    App* app = AppNew();
    utassert(app);
    if (!app) {
        return;
    }
    component::Init(app);
    Arena* arena = ArenaNew();
    Ctx cx = {};
    cx.app = app;
    cx.a = arena;
    PaintCtx paint = {};
    paint.pa = app->paint;
    paint.app = app;
    paint.opacity = 1;
    bool ready = PaintTargetBeginOffscreen(&paint, 200, 200);
    utassert(ready);
    if (ready) {
        SankeyChart* chart = SankeyChart::New(&cx)
                                 ->Node(StrL("Source"))
                                 ->Node(StrL("Destination"))
                                 ->Link(0, 1, 10);
        El* el = chart->IntoEl();
        el->w = el->h = 200;
        for (int frame = 0; frame < 2; frame++) {
            TextMeasBeginFrame(&paint);
            scene::FrameBegin(&paint);
            el->customPaint(&paint, el, el->customUser);
            Bounds damage = {};
            bool changed = scene::FrameEnd(&paint, &damage);
            utassert(scene::Stats(&paint).prims > 0);
            utassert(changed == (frame == 0));
            TextMeasEndFrame(&paint);
        }
        uint8_t* pixels = (uint8_t*)Alloc(arena, 200 * 200 * 4);
        utassert(PaintTargetEndOffscreen(&paint, pixels));
    }
    TextMeasClear(&paint);
    scene::Free(&paint);
    ArenaDelete(arena);
    AppFree(app);
#endif
}

void TestChart() {
    TestSuite("chart labels");
    RadarLabelsRetainTextAndElements();
    PlainRadarLabelsProjectToTheTaggedValue();
    SankeyLabelsCarryIndependentStylesAndDoNotCap();
    UnchangedPlotLabelsKeepTheScene();
    UnchangedSankeyLabelsKeepTheScene();
}
