#ifndef GPUI_UI_DOCK_H_
#define GPUI_UI_DOCK_H_
/* Themed dock — crates/ui/src/dock

   DockArea renders a DockState: the centre item, the three Docks around it,
   and for every tab group a TabBar whose tabs can be dragged into another
   group or onto its edge to split it. */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// panel.rs. Base's POD panel definition is already the object-safe function
// table Rust obtains from its Panel and PanelView traits. These source-named
// aliases keep the public split visible without adding a second allocation or
// a C++ class hierarchy merely to erase it again.
using Panel = DockPanelDef;
using PanelView = DockPanelDef;
using PanelStyle = DockPanelStyle;
using PanelControl = DockPanelControl;

struct TitleStyle {
    Rgba background = {};
    Rgba foreground = {};
};

// The concrete presentation handle Rust needs to cross from
// Arc<dyn ui::PanelView> to Arc<dyn base::PanelView>. C++'s function table is
// already concrete, so the wrapper owns a value copy and exposes it by name.
struct PanelHandle {
    PanelView view = {};

    static PanelHandle New(const PanelView& panel);
    static PanelHandle FromView(const PanelView& panel);
    static const PanelView* Of(const PanelView* panel);
    const PanelView* Get() const;
    PanelView IntoPanelView() const;
};

PanelHandle panel_handle(const PanelView& panel);

// The preview following a dragged tab. Base owns the DragPanel payload; this
// is its component appearance, restored as a source-named frame builder.
struct DragPanelPreview {
    Ctx* cx = nullptr;
    const PanelView* panel = nullptr;

    static DragPanelPreview* New(Ctx* cx, const PanelView* panel);
    El* IntoEl();
};

// The skin is a retained settings handle. Rust shares it with a DockArea via
// Rc; here the stable DockState entity is that shared point, and a view keeps
// this POD value when it wants to adjust the skin later.
struct DockSkin {
    Entity<DockState> state = {};

    static DockSkin New(Entity<DockState> state);
    PanelStyle GetPanelStyle(App* app) const;
    void SetPanelStyle(App* app, Window* win, PanelStyle style);
    bool IsToggleButtonVisible(App* app) const;
    void SetToggleButtonVisible(App* app, Window* win, bool visible);
    const DockRenderer* Renderer() const;
};

// InvalidPanel::render: what stands in for a panel a saved layout names and
// the registry cannot build. Hand it to DockLoad; it is given the
// DockInvalidPanel holding the name that was asked for.
El* DockInvalidPanelRender(Ctx* cx, void* data);

// The tab bar of a tab group (TabPanel::render_title_bar).
const float kDockTabBarH = 30;

struct DockArea {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<DockState> state = {};
    const DockSkin* skin = nullptr;

    static DockArea* New(Ctx* cx, Str id, Entity<DockState> state);
    DockArea* WithSkin(const DockSkin* value);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_DOCK_H_
