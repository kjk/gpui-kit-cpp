#include "ui/icon.h"

namespace gpui {

namespace component {

IconNamed IconNamed::From(IconName name) {
    return {IconNamePath(name)};
}

Icon* Icon::New(Ctx* cx, IconName name) {
    Arena* a = cx->a;
    Icon* i = ArenaNew<Icon>(a);
    i->a = a;
    i->cx = cx;
    i->name = name;
    return i;
}

Icon* Icon::New(Ctx* cx, IconNamed named) {
    Icon* i = New(cx, IconName::None);
    i->path = named.path;
    return i;
}

Icon* Icon::Empty(Ctx* cx) {
    return New(cx, IconName::None);
}

Icon* Icon::Path(Str assetPath) {
    path = assetPath;
    source = IconSource::Path;
    return this;
}

Icon* Icon::Data(Str svg) {
    data = svg.len > 0 ? StrDup(a, svg) : Str{};
    source = IconSource::Data;
    return this;
}

Icon* Icon::Size(float v) {
    size = v;
    hasSize = true;
    return this;
}

Icon* Icon::Size(UiSize v) {
    size = UiIconPx(v);
    hasSize = true;
    return this;
}

Icon* Icon::Color(Rgba c) {
    color = c;
    hasColor = true;
    return this;
}

Icon* Icon::Transform(float turns) {
    rotation = turns;
    return this;
}

Icon* Icon::Rotate(float turns) {
    return Transform(turns);
}

El* Icon::IntoEl() {
    El* e = hasSize ? IconEl(a, name, size) : IconEl(a, name);
    if (source == IconSource::Data) {
        e->iconSvg = data;
    } else if (path.s) {
        e->iconPath = path;
    }
    if (hasColor) {
        e->Fg(color);
    }
    if (rotation != 0) {
        e->Rotate(rotation);
    }
    return e;
}

} // namespace component
} // namespace gpui
