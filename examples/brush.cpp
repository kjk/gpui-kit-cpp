/* crates/story/examples/brush.rs — a canvas you draw on with the mouse.

   The controls are a size slider (1..50), an opacity slider (0.1..1), a Clear
   Canvas button, a Show Grid checkbox and eight colour swatches; under them
   the canvas itself, which is one element with a custom paint. A press starts
   a stroke, every move a pixel or more from the last point adds one, and the
   release keeps the stroke if it has more than one point — which is Rust's
   own rule, down to the one-pixel threshold.

   The strokes are paths: `PathBuilder::stroke(size)` with a move_to and a
   line_to per point is `PathNew` / `PathMoveTo` / `PathLineTo` / `PathStroke`
   here. The grid is the same, a line every forty pixels at a fifth of the
   border's alpha. */

#include "gpui.h"

using namespace gpui;

// A stroke as it is drawn: the points in the canvas' own coordinates, the
// colour it was started with, and the width the slider said.
struct Stroke {
    static const int kMaxPoints = 4096;
    Point points[kMaxPoints];
    int n = 0;
    Rgba color = {};
    float size = 5;
};

// As many strokes as one drawing holds; Rust's Vec has no ceiling, and this
// says what it costs to have one: a stroke is 32 KB of points, so a thousand
// of them is the drawing this example is worth.
static const int kMaxDrawStrokes = 1000;

static const Rgba kColors[] = {
    Rgb(0x00, 0x00, 0x00),
    Rgb(0xff, 0xff, 0xff),
    Rgb(0xff, 0x00, 0x00),
    Rgb(0x00, 0xff, 0x00),
    Rgb(0x00, 0x00, 0xff),
    Rgb(0xff, 0xff, 0x00),
    // hsla(0.58, 1, 0.5) and hsla(0.083, 1, 0.5): the purple and the orange.
    RgbaHsla(0.58f, 1.f, 0.5f, 1.f),
    RgbaHsla(0.083f, 1.f, 0.5f, 1.f),
};
static const int kNColors = (int)(sizeof(kColors) / sizeof(kColors[0]));

struct BrushApp {
    SliderState brushSize = SliderStateNew(1, 50, SliderSingle(5), 1);
    SliderState brushOpacity =
        SliderStateNew(0.1f, 1.f, SliderSingle(1.f), 0.05f);
    Rgba brushColor = Rgb(0, 0, 0);
    bool showGrid = false;

    Stroke* strokes = nullptr;
    int nStrokes = 0;
    Stroke current;
    bool drawing = false;
    // The canvas' own box, so a pointer in the window can be read in the
    // coordinates the strokes are kept in — Rust's `on_prepaint` bounds.
    Bounds canvas = {};

    ~BrushApp() { Free(nullptr, strokes); }
    static El* Render(BrushApp* self, Ctx* cx);
};

// build_stroke_path: nothing under two points, and a line from each to the
// next in the canvas' coordinates.
static void PaintStroke(PaintCtx* ctx, const Stroke& s, const Bounds& b) {
    if (s.n < 2) {
        return;
    }
    Path* p = PathNew(ctx, false);
    if (!p) {
        return;
    }
    PathMoveTo(p, b.x + s.points[0].x, b.y + s.points[0].y);
    for (int i = 1; i < s.n; i++) {
        PathLineTo(p, b.x + s.points[i].x, b.y + s.points[i].y);
    }
    PathStroke(ctx, p, s.size, s.color);
    PathFree(p);
}

static void PaintCanvas(PaintCtx* ctx, El* e, void* user) {
    auto* self = (BrushApp*)user;
    const Theme& th = ThemeNow(ctx->app);
    Bounds b = e->Bounds();
    if (self->showGrid) {
        Rgba grid = RgbaOpacity(th.border, 0.2f);
        const float kGrid = 40.f;
        // A one-pixel path per line, which is what PathBuilder::stroke(1)
        // builds on the other side.
        for (float x = 0; x <= b.w; x += kGrid) {
            if (Path* p = PathNew(ctx, false)) {
                PathMoveTo(p, b.x + x, b.y);
                PathLineTo(p, b.x + x, b.y + b.h);
                PathStroke(ctx, p, 1.f, grid);
                PathFree(p);
            }
        }
        for (float y = 0; y <= b.h; y += kGrid) {
            if (Path* p = PathNew(ctx, false)) {
                PathMoveTo(p, b.x, b.y + y);
                PathLineTo(p, b.x + b.w, b.y + y);
                PathStroke(ctx, p, 1.f, grid);
                PathFree(p);
            }
        }
    }
    for (int i = 0; i < self->nStrokes; i++) {
        PaintStroke(ctx, self->strokes[i], b);
    }
    if (self->drawing) {
        PaintStroke(ctx, self->current, b);
    }
}

static void StrokeAddPoint(Stroke* s, float x, float y) {
    if (s->n >= Stroke::kMaxPoints) {
        return;
    }
    s->points[s->n].x = x;
    s->points[s->n].y = y;
    s->n++;
}

static void OnCanvasDown(BrushApp* self, Ctx* cx, const MouseDownEvent* ev) {
    if (ev->button != MouseButton::Left) {
        return;
    }
    float opacity = self->brushOpacity.value.End();
    self->current.n = 0;
    self->current.color = RgbaOpacity(self->brushColor, opacity);
    self->current.size = self->brushSize.value.End();
    StrokeAddPoint(&self->current, ev->x - self->canvas.x,
                   ev->y - self->canvas.y);
    self->drawing = true;
    Notify(cx);
}

static void OnCanvasMove(BrushApp* self, Ctx* cx, const MouseMoveEvent* ev) {
    if (!self->drawing) {
        return;
    }
    float x = ev->x - self->canvas.x;
    float y = ev->y - self->canvas.y;
    if (self->current.n > 0) {
        // A point a pixel or more from the last one, on either axis.
        Point last = self->current.points[self->current.n - 1];
        float dx = x > last.x ? x - last.x : last.x - x;
        float dy = y > last.y ? y - last.y : last.y - y;
        if (dx < 1.f && dy < 1.f) {
            return;
        }
    }
    StrokeAddPoint(&self->current, x, y);
    Notify(cx);
}

static void OnCanvasUp(BrushApp* self, Ctx* cx, const MouseUpEvent*) {
    if (!self->drawing) {
        return;
    }
    self->drawing = false;
    if (self->current.n > 1 && self->nStrokes < kMaxDrawStrokes) {
        if (!self->strokes) {
            self->strokes = AllocArray<Stroke>(kMaxDrawStrokes);
        }
        if (self->strokes) {
            self->strokes[self->nStrokes++] = self->current;
        }
    }
    self->current.n = 0;
    Notify(cx);
}

static void OnClear(BrushApp* self, Ctx* cx, const ClickEvent*) {
    self->nStrokes = 0;
    self->current.n = 0;
    self->drawing = false;
    Notify(cx);
}

static void OnToggleGrid(BrushApp* self, Ctx* cx, const ClickEvent*) {
    self->showGrid = !self->showGrid;
    Notify(cx);
}

static void OnPickColor(BrushApp* self, Ctx* cx, const ClickEvent*,
                        intptr_t ix) {
    if (ix >= 0 && ix < kNColors) {
        self->brushColor = kColors[ix];
    }
    Notify(cx);
}

static void OnSlider(BrushApp*, Ctx* cx, const SliderEvent*) {
    Notify(cx);
}

// GroupBox::new().outline().title(..): the two sections the example is in.
static El* Section(Ctx* cx, Str title, El* content) {
    return component::GroupBox::New(cx, title)
        ->Outline()
        ->Child(content)
        ->IntoEl();
}

El* BrushApp::Render(BrushApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Listener onSlider = Listen(cx, &OnSlider);

    El* left = Div(a)->FlexCol()->Gap(16)->Flex1();
    El* sizeRow = Div(a)->FlexRow()->Gap(16)->ItemsCenter();
    sizeRow->Child(TextEl(a, StrL("Size:")));
    sizeRow
        ->Child(component::Slider::New(cx, StrL("brush-size"), &self->brushSize)
                    ->W(200)
                    ->OnChange(onSlider)
                    ->IntoEl());
    sizeRow->Child(TextEl(
        a, StrDup(a, fmt("%.0fpx", (double)self->brushSize.value.End()))));
    left->Child(sizeRow);

    El* opacityRow = Div(a)->FlexRow()->Gap(16)->ItemsCenter();
    opacityRow->Child(TextEl(a, StrL("Opacity:")));
    opacityRow->Child(
        component::Slider::New(cx, StrL("brush-opacity"), &self->brushOpacity)
            ->W(200)
            ->OnChange(onSlider)
            ->IntoEl());
    opacityRow->Child(TextEl(
        a, StrDup(a, fmt("%.0f%%",
                         (double)(self->brushOpacity.value.End() * 100.f)))));
    left->Child(opacityRow);

    El* buttons = Div(a)->FlexRow()->Gap(12)->ItemsCenter();
    buttons->Child(component::Button::New(cx, StrL("clear-canvas"))
                       ->Icon(IconName::X)
                       ->Label(StrL("Clear Canvas"))
                       ->WithSize(UiSize::Small)
                       ->OnClick(Listen(cx, &OnClear))
                       ->IntoEl());
    buttons->Child(component::Checkbox::New(cx, StrL("show-grid"))
                       ->Label(StrL("Show Grid"))
                       ->Checked(self->showGrid)
                       ->OnClick(Listen(cx, &OnToggleGrid))
                       ->IntoEl());
    left->Child(buttons);

    El* right = Div(a)->FlexCol()->Gap(8)->Flex1();
    right->Child(TextEl(a, StrL("Color:")));
    El* swatches = Div(a)->FlexRow()->FlexWrap()->Gap(12);
    Listener pick = Listen(cx, &OnPickColor);
    for (int i = 0; i < kNColors; i++) {
        bool selected = self->brushColor.r == kColors[i].r &&
                        self->brushColor.g == kColors[i].g &&
                        self->brushColor.b == kColors[i].b;
        El* swatch = Div(a)
                         ->W(40)
                         ->H(40)
                         ->Radius(th.radius)
                         ->Bg(kColors[i])
                         ->Border(2, selected ? th.primary : th.border)
                         ->Cursor(CursorKind::Pointer)
                         ->Click(HashClickId(StrDup(a, fmt("swatch-%d", i))))
                         ->OnClick(ListenerArg(pick, i));
        swatches->Child(swatch);
    }
    right->Child(swatches);

    El* controls = Div(a)->FlexRow()->Gap(32)->W(kFill)->ItemsStart();
    controls->Child(left);
    controls->Child(right);

    // The canvas: one box that paints itself and takes the three mouse
    // events, with a crosshair over it. Rust gives the GroupBox holding it a
    // `content_style` of flex_1 and size_full; a GroupBox here sizes to its
    // content, so the canvas takes the height the window has left.
    El* canvas = Div(a)
                     ->W(kFill)
                     ->H(WindowSize(cx->win).dipH - 285)
                     ->Bg(th.tokens.background)
                     ->Cursor(CursorKind::Crosshair)
                     ->BoundsOut(&self->canvas)
                     ->Id(StrL("canvas"))
                     ->Click(HashClickId(StrL("canvas")))
                     ->OnMouseDown(Listen(cx, &OnCanvasDown))
                     ->OnMouseUp(Listen(cx, &OnCanvasUp));
    canvas->customPaint = PaintCanvas;
    canvas->customUser = self;

    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->Gap(24)
        ->Pad(16)
        ->Bg(th.tokens.background)
        ->Child(Section(cx, StrL("Controls"), controls))
        ->Child(Section(cx, StrL("Drawing Canvas"), canvas));
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    AssetsClear();
    AssetsAddDefaultRoots(StrL("brush"));
    Entity<BrushApp> view = EntityNew<BrushApp>(app);
    Window* win = KitOpenWindow(app, StrL("Brush Example"), 1100, 860, view.id,
                                WinOpts{});
    // A move is the window's here rather than an element's — GPUI hangs
    // `on_mouse_move` off the div, and this tree reports moves to the window
    // and lets the handler decide, which is what the drawing flag is for.
    WindowOnMouseMove(win, ListenTo(view, &OnCanvasMove));
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
