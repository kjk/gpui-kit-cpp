#include "ui/touch_selection.h"
#include "ui/button.h"
#include "ui/i18n.h"
#include "ui/popover.h"
#include "ui/separator.h"
#include "base/positioner.h"
#include "base/text_selection.h"

namespace gpui {

namespace component {

// The height of a menu row.
static const float kEditMenuRowHeight = 32.f;

EditMenu* EditMenu::New(Ctx* cx, Str id, Bounds anchor) {
    EditMenu* m = ArenaNew<EditMenu>(cx->a);
    m->a = cx->a;
    m->cx = cx;
    m->id = id;
    m->anchor = anchor;
    return m;
}

EditMenu* EditMenu::Item(Str label, Listener onClick) {
    items.Append(a, {label, onClick});
    return this;
}

El* EditMenu::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    const float radius = th.radiusLg;
    ButtonCustomVariant itemStyle = ButtonCustomVariant::New(cx->app)
                                        .Color(th.transparent)
                                        .Foreground(th.popoverFg)
                                        .Hover(th.accent)
                                        .Active(th.accent);
    El* row = Div(a)->FlexRow()->ItemsStretch()->ClipX()->ClipY()->Id(id);
    PopoverSurface(cx, row)->Radius(radius);
    int last = items.len > 0 ? items.len - 1 : 0;
    for (int i = 0; i < items.len; i++) {
        const EditMenuItem& item = items[i];
        if (i > 0) {
            row->Child(Separator::Vertical(cx)->IntoEl());
        }
        bool first = i == 0;
        bool lastItem = i == last;
        Button* button = Button::New(cx, item.label)
                             ->Custom(itemStyle)
                             ->WithSize(UiSize::Large)
                             ->Rounded(radius)
                             ->TabStop(false)
                             ->Label(item.label)
                             ->OnClick(item.onClick);
        button->joined = true;
        button->cornerTL = first;
        button->cornerBL = first;
        button->cornerTR = lastItem;
        button->cornerBR = lastItem;
        button->edgeL = first;
        button->edgeR = lastItem;
        row->Child(button->IntoEl()->H(kEditMenuRowHeight));
    }
    // The menu is a finger's surface wherever it shows: register the strip
    // it occupies so a press on it does not clear the selection underneath.
    const float menuH = kEditMenuRowHeight + 8.f;
    float w = anchor.w > 80.f ? anchor.w : 80.f;
    Bounds hit = {anchor.x, anchor.y - menuH, w, menuH};
    WindowSelectionRegisterTouchUi(cx->win, hit);
    return Positioner::Side(cx, anchor)
        ->Placement(Placement::Top)
        ->Offset(8.f)
        ->Occlude()
        ->Child(row)
        ->IntoEl()
        ->DeferredLayer(kPaintLayerPopup);
}

TouchSelectionOverlay* TouchSelectionOverlay::New(Ctx* cx, Str id) {
    TouchSelectionOverlay* o = ArenaNew<TouchSelectionOverlay>(cx->a);
    o->a = cx->a;
    o->cx = cx;
    o->id = id;
    return o;
}

TouchSelectionOverlay* TouchSelectionOverlay::Snapshot(
    const TouchSelectionSnapshot& snap) {
    snapshot = snap;
    hasSnapshot = true;
    return this;
}

TouchSelectionOverlay* TouchSelectionOverlay::Item(Str label,
                                                   Listener onClick) {
    items.Append(a, {label, onClick});
    return this;
}

El* TouchSelectionOverlay::IntoEl() {
    if (!hasSnapshot || items.len == 0 || !snapshot.menuOpen) {
        return nullptr;
    }
    Bounds anchor = {};
    if (!snapshot.BoundsOfVisible(&anchor)) {
        return nullptr;
    }
    if (!snapshot.IsEmpty()) {
        anchor.y -= TouchHandle::kExtent;
        anchor.h += TouchHandle::kExtent * 2.f;
    }
    EditMenu* menu = EditMenu::New(cx, id, anchor);
    for (int i = 0; i < items.len; i++) {
        menu->Item(items[i].label, items[i].onClick);
    }
    return menu->IntoEl();
}

static void OnCopy(void*, Ctx* cx, const ClickEvent*) {
    if (!cx || !cx->win) {
        return;
    }
    const int kCap = 64 * 1024;
    char* buf = (char*)Alloc(nullptr, kCap);
    if (!buf) {
        return;
    }
    int n = WindowSelectionText(cx->win, buf, kCap);
    Str text = StrTrimAscii(Str(buf, n));
    if (len(text) > 0) {
        ClipboardSetText(cx->win, text);
    }
    Free(nullptr, buf);
    WindowSelectionCloseEditMenu(cx->win);
    Notify(cx);
}

static void OnSelectAll(void*, Ctx* cx, const ClickEvent*) {
    if (!cx || !cx->win) {
        return;
    }
    WindowSelectionSelectAllTouched(cx->win);
    Notify(cx);
}

El* WindowTouchSelectionOverlay(Ctx* cx) {
    if (!cx || !cx->win) {
        return nullptr;
    }
    TouchSelectionSnapshot snap;
    if (!WindowSelectionTouchSnapshot(cx->win, &snap)) {
        return nullptr;
    }
    return TouchSelectionOverlay::New(cx, StrL("window-touch-selection"))
        ->Snapshot(snap)
        ->Item(Tr("Input.Copy"), Listen(cx, &OnCopy))
        ->Item(Tr("Input.Select All"), Listen(cx, &OnSelectAll))
        ->IntoEl();
}

} // namespace component
} // namespace gpui
