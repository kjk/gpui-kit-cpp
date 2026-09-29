/* Ported from crates/ui/src/window_border.rs.
 *
 * A client-decorated window keeps a band of shadow padding around its frame,
 * and the band a press counts as a resize straddles that inner frame. A side
 * the window manager has tiled flush against something has neither. */

#include "Test.h"

using namespace gpui::component;

static void TilingTakesTheShadowOffThatSide() {
    WindowTiling none;
    Edges e = WindowBorderInsets(20, none);
    utassertnear(e.top, 20.f);
    utassertnear(e.bottom, 20.f);
    utassertnear(e.left, 20.f);
    utassertnear(e.right, 20.f);

    WindowTiling leftTiled;
    leftTiled.left = true;
    e = WindowBorderInsets(20, leftTiled);
    utassertnear(e.left, 0.f);
    utassertnear(e.right, 20.f);

    WindowTiling all;
    all.top = all.bottom = all.left = all.right = true;
    e = WindowBorderInsets(20, all);
    utassertnear(e.top, 0.f);
    utassertnear(e.bottom, 0.f);
    utassertnear(e.left, 0.f);
    utassertnear(e.right, 0.f);
    utassert(all.AllTiled());
    utassert(!none.IsTiled());
}

// A 200x100 window with 20 of shadow: the frame runs from 20,20 to 180,80.
static void EachEdgeIsGrabbedAlongItsOwnSegment() {
    WindowTiling none;
    Edges insets = WindowBorderInsets(20, none);
    const float hit = 4;

    utassert(WindowResizeEdge(20, 50, 200, 100, insets, none, hit) ==
             WindowEdge::Left);
    utassert(WindowResizeEdge(180, 50, 200, 100, insets, none, hit) ==
             WindowEdge::Right);
    utassert(WindowResizeEdge(100, 20, 200, 100, insets, none, hit) ==
             WindowEdge::Top);
    utassert(WindowResizeEdge(100, 80, 200, 100, insets, none, hit) ==
             WindowEdge::Bottom);
    // The corners win over the sides they are made of.
    utassert(WindowResizeEdge(20, 20, 200, 100, insets, none, hit) ==
             WindowEdge::TopLeft);
    utassert(WindowResizeEdge(180, 20, 200, 100, insets, none, hit) ==
             WindowEdge::TopRight);
    utassert(WindowResizeEdge(20, 80, 200, 100, insets, none, hit) ==
             WindowEdge::BottomLeft);
    utassert(WindowResizeEdge(180, 80, 200, 100, insets, none, hit) ==
             WindowEdge::BottomRight);
    // Inside the frame, and out in the shadow past the corner, are neither.
    utassert(WindowResizeEdge(100, 50, 200, 100, insets, none, hit) ==
             WindowEdge::None);
    utassert(WindowResizeEdge(5, 5, 200, 100, insets, none, hit) ==
             WindowEdge::None);
    // A left edge does not run on down the extension line of the padding.
    utassert(WindowResizeEdge(20, 95, 200, 100, insets, none, hit) ==
             WindowEdge::None);
}

static void ATiledSideIsNeverGrabbed() {
    WindowTiling tiling;
    tiling.left = true;
    Edges insets = WindowBorderInsets(20, tiling);
    const float hit = 4;
    // The left edge is flush against something, so its band is gone — and
    // with it the two corners it was half of.
    utassert(WindowResizeEdge(0, 50, 200, 100, insets, tiling, hit) ==
             WindowEdge::None);
    utassert(WindowResizeEdge(0, 20, 200, 100, insets, tiling, hit) ==
             WindowEdge::Top);
    utassert(WindowResizeEdge(180, 50, 200, 100, insets, tiling, hit) ==
             WindowEdge::Right);
}

static void AWindowWithNoShadowIsGrabbedAtItsOwnEdges() {
    // What an undecorated window is: the frame is the whole box, so the band
    // straddles the client area's edges.
    WindowTiling none;
    Edges insets = WindowBorderInsets(0, none);
    const float hit = 4;
    utassert(WindowResizeEdge(1, 50, 200, 100, insets, none, hit) ==
             WindowEdge::Left);
    utassert(WindowResizeEdge(199, 99, 200, 100, insets, none, hit) ==
             WindowEdge::BottomRight);
    utassert(WindowResizeEdge(100, 50, 200, 100, insets, none, hit) ==
             WindowEdge::None);
    // The directions are numbered the way _NET_WM_MOVERESIZE numbers them,
    // which is what the X11 window sends.
    static_assert((int)WindowEdge::TopLeft == 0, "clockwise from top-left");
    static_assert((int)WindowEdge::Right == 3, "clockwise from top-left");
    static_assert((int)WindowEdge::Left == 7, "clockwise from top-left");
    static_assert((int)WindowEdge::None == -1, "no edge");
}

static void WindowPaddingsFollowDecorationAndStableInset() {
    Window window;
    window.clientInset = 20;
    window.tiling.left = true;
#if GPUI_OS_LINUX
    // A native X11 window is required to distinguish a successful Motif
    // client-frame request from a window manager that retained its frame.
    // With no adapter this test window is therefore server-decorated.
    Edges e = WindowPaddings(&window);
    utassertnear(e.top, 0.f);
    utassertnear(e.left, 0.f);
#else
    window.opts.clientTitleBar = true;
    Edges e = WindowPaddings(&window);
    utassertnear(e.top, 20.f);
    utassertnear(e.bottom, 20.f);
    utassertnear(e.left, 0.f);
    utassertnear(e.right, 20.f);
#endif
    Edges none = WindowPaddings(nullptr);
    utassertnear(none.top, 0.f);
    utassertnear(none.right, 0.f);
}

// window_content_insets: the shadow padding plus the frame's own border on
// every side it is not tiled against, which is what lays a sheet flush
// against the inside of the frame.
static void ContentInsetsAddTheFrameBorderToThePaddings() {
    Window window;
    window.clientInset = 20;
    window.tiling.left = true;
#if GPUI_OS_LINUX
    // Without a native adapter this test window is server-decorated, and a
    // server-decorated window has no frame of ours to inset from.
    Edges e = WindowContentInsets(&window);
    utassertnear(e.top, 0.f);
    utassertnear(e.left, 0.f);
#else
    window.opts.clientTitleBar = true;
    Edges pad = WindowPaddings(&window);
    Edges e = WindowContentInsets(&window);
    // A tiled side has neither shadow nor border; every other side is the
    // padding plus one border.
    utassertnear(e.left, 0.f);
    utassertnear(pad.left, 0.f);
    utassertnear(e.top, pad.top + kWindowBorderSize);
    utassertnear(e.bottom, pad.bottom + kWindowBorderSize);
    utassertnear(e.right, pad.right + kWindowBorderSize);
#endif
    Edges none = WindowContentInsets(nullptr);
    utassertnear(none.top, 0.f);
    utassertnear(none.right, 0.f);
}

static void BuilderPublishesPlatformResizeSettings() {
    App app;
    Window* window = new Window();
    window->app = &app;
    window->opts.clientTitleBar = true;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, window, arena, {}};
    WindowTiling tiled;
    tiled.left = true;

    El* child = Div(arena);
    El* border = WindowBorder::New(&cx)
                     ->ShadowSize(17)
                     ->ResizeHitSize(6)
                     ->Tiling(tiled)
                     ->Child(child)
                     ->IntoEl();
    utassert(border != nullptr);
    utassertnear(window->resizeHitSize, 6.f);
    utassert(window->tiling.left);
#if GPUI_OS_LINUX
    // No PlatWindow in this pure test means server decorations won.
    utassertnear(window->clientInset, 0.f);
#else
    utassertnear(window->clientInset, 17.f);
#endif

    ArenaDelete(arena);
    delete window;
}

static void BuilderCarriesTheExactTwoSourceShadows() {
    App app;
    ThemeSet(&app, ThemeMode::Dark);
    Window* window = new Window();
    window->app = &app;
    window->opts.clientTitleBar = true;
    window->active = true;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, window, arena, {}};

    El* backdrop = WindowBorder::New(&cx)->ShadowSize(20)->IntoEl();
#if GPUI_OS_LINUX
    // A pure Linux test has no native window, so WindowClientDecorated
    // correctly selects server decorations and there is no client shadow.
    utassert(backdrop->first == nullptr);
#else
    El* frame = backdrop->first;
    utassert(frame != nullptr);
    utassert(frame->style.shadowCount == 2);
    const BoxShadow& ambient = frame->style.shadows[0];
    const BoxShadow& contact = frame->style.shadows[1];
    utassertnear(ambient.x, 0.f);
    utassertnear(ambient.y, 2.f);
    utassertnear(ambient.blur, 10.f);
    utassertnear(ambient.spread, -1.f);
    utassert(ambient.color.a == 46);
    utassertnear(contact.y, 1.f);
    utassertnear(contact.blur, 3.f);
    utassertnear(contact.spread, 0.f);
#endif

    ArenaDelete(arena);
    delete window;
}

// window_border.rs: only_client_decorated_windows_use_the_client_border. A
// server-decorated window's content passes through unchanged.
static void OnlyClientDecoratedWindowsUseTheClientBorder() {
    App app;
    Window* window = new Window();
    window->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, window, arena, {}};
    utassert(!WindowClientDecorated(window));
    El* child = Div(arena);
    utassert(WindowBorder::New(&cx)->Child(child)->IntoEl() == child);
    ArenaDelete(arena);
    delete window;
}

void TestWindowBorder() {
    TestSuite("window_border");
    TilingTakesTheShadowOffThatSide();
    EachEdgeIsGrabbedAlongItsOwnSegment();
    ATiledSideIsNeverGrabbed();
    AWindowWithNoShadowIsGrabbedAtItsOwnEdges();
    WindowPaddingsFollowDecorationAndStableInset();
    ContentInsetsAddTheFrameBorderToThePaddings();
    BuilderPublishesPlatformResizeSettings();
    BuilderCarriesTheExactTwoSourceShadows();
    OnlyClientDecoratedWindowsUseTheClientBorder();
}
