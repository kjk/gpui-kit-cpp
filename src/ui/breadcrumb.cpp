#include "ui/breadcrumb.h"

namespace gpui {

namespace component {

BreadcrumbItem* BreadcrumbItem::New(Ctx* cx, Str label) {
    Arena* a = cx->a;
    BreadcrumbItem* it = ArenaNew<BreadcrumbItem>(a);
    it->a = a;
    it->cx = cx;
    it->label = label;
    return it;
}
BreadcrumbItem* BreadcrumbItem::Disabled(bool v) {
    disabled = v;
    return this;
}
BreadcrumbItem* BreadcrumbItem::OnClick(Listener fn) {
    onClick = fn;
    return this;
}

El* BreadcrumbItem::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // The last level is where you are, so it gets the foreground; a disabled
    // one stays muted whatever its position.
    bool lit = isLast && !disabled;
    bool clickable = !disabled && onClick.IsValid();
    El* el = Div(a)
                 ->Role(clickable ? AccessibilityRole::Link
                                  : AccessibilityRole::ListItem)
                 ->AriaLabel(label)
                 ->AriaDisabled(disabled)
                 ->Child(TextEl(a, label)->Font(14)->Fg(lit ? th.foreground
                                                            : th.mutedFg));
    if (clickable) {
        // breadcrumb.rs asks for the hand in the same `when_some(on_click)`
        // that binds the click: a level with nowhere to go is not a link.
        el->Cursor(CursorKind::Pointer);
        BindClick(el, ElementIdInteger(a, (uint64_t)ix), onClick);
    }
    return el;
}

Breadcrumb* Breadcrumb::New(Ctx* cx) {
    Arena* a = cx->a;
    Breadcrumb* b = ArenaNew<Breadcrumb>(a);
    b->a = a;
    b->cx = cx;
    return b;
}
Breadcrumb* Breadcrumb::Child(BreadcrumbItem* item) {
    if (item) {
        items.Append(a, item);
    }
    return this;
}
Breadcrumb* Breadcrumb::Child(Str label) {
    return Child(BreadcrumbItem::New(cx, label));
}

El* Breadcrumb::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)->FlexRow()->ItemsCenter()->Gap(6);
    for (int i = 0; i < items.len; i++) {
        if (i) {
            row->Child(IconEl(a, IconName::ChevronRight, 14)->Fg(th.mutedFg));
        }
        BreadcrumbItem* it = items[i];
        it->ix = i;
        it->isLast = i == items.len - 1;
        row->Child(it->IntoEl());
    }
    return row;
}

} // namespace component
} // namespace gpui
