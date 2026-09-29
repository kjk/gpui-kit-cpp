#include "ui/toolbar.h"

namespace gpui {

namespace component {

El* ToolbarItem::IntoEl(Ctx* cx, UiSize size) const {
    if (!build) {
        return content;
    }
    El* built = build(control, size);
    return UiInputH(Div(cx->a)->FlexRow()->ItemsCenter(), size)->Child(built);
}

ToolbarGroup* ToolbarGroup::New(Ctx* cx, Str id) {
    ToolbarGroup* g = ArenaNew<ToolbarGroup>(cx->a);
    g->a = cx->a;
    g->cx = cx;
    g->id = id;
    return g;
}

ToolbarGroup* ToolbarGroup::Label(Str value) {
    label = value;
    return this;
}

ToolbarGroup* ToolbarGroup::Gap(float value) {
    gap = value;
    return this;
}

ToolbarGroup* ToolbarGroup::Content(El* content) {
    if (content) {
        ToolbarItem item;
        item.content = content;
        items.Append(a, item);
    }
    return this;
}

ToolbarGroup* ToolbarGroup::WithSize(UiSize value) {
    size = ToolbarSize(value);
    return this;
}

El* ToolbarGroup::IntoEl() {
    gpui::ToolbarGroup* group = gpui::ToolbarGroup::New(cx, id);
    if (label.s) {
        group->Label(label);
    }
    if (gap > 0) {
        group->root->Gap(gap);
    }
    for (const ToolbarItem& item : items) {
        group->Child(item.IntoEl(cx, size));
    }
    return group->IntoEl();
}

Toolbar* Toolbar::New(Ctx* cx, Str id) {
    Toolbar* t = ArenaNew<Toolbar>(cx->a);
    t->a = cx->a;
    t->cx = cx;
    t->id = id;
    return t;
}

Toolbar* Toolbar::Disabled(bool value) {
    disabled = value;
    return this;
}

Toolbar* Toolbar::Content(El* content) {
    if (content) {
        ToolbarItem item;
        item.content = content;
        items.Append(a, item);
    }
    return this;
}

Toolbar* Toolbar::Contents(El* const* contents, int count) {
    for (int i = 0; contents && i < count; i++) {
        Content(contents[i]);
    }
    return this;
}

Toolbar* Toolbar::WithSize(UiSize value) {
    size = ToolbarSize(value);
    return this;
}

El* Toolbar::IntoEl() {
    gpui::Toolbar* bar = gpui::Toolbar::New(cx, id)->Disabled(disabled);
    El* root = bar->root->FlexRow()->ItemsCenter()->Shrink0();
    switch (size.kind) {
        case UiSize::Kind::XSmall:
            // h_7 p_1 gap_1 text_xs
            root->H(28)->Pad(4)->Gap(4)->Font(12);
            break;
        case UiSize::Kind::Small:
            // h_8 p_1 gap_1 text_sm
            root->H(32)->Pad(4)->Gap(4)->Font(14);
            break;
        default:
            // h_12 p_2 gap_2 text_sm
            root->H(48)->Pad(8)->Gap(8)->Font(14);
            break;
    }
    for (const ToolbarItem& item : items) {
        bar->Child(item.IntoEl(cx, size));
    }
    return bar->IntoEl();
}

} // namespace component
} // namespace gpui
