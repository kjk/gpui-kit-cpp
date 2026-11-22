#include "ui/icon.h"
#include "gpui/assets.h"
#include "gpui/asset_icons.h"

namespace gpui {

namespace component {

Icon* IconNameExt::View(Ctx* cx, IconName name) {
    return Icon::New(cx, name);
}

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
    data = len(svg) > 0 ? StrDup(a, svg) : Str{};
    source = IconSource::Data;
    return this;
}

Icon* Icon::Size(float v) {
    size = v;
    hasSize = true;
    return this;
}

Icon* Icon::Size(UiSize v) {
    size = UiIconPx(cx, v);
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

void SetIconResolver(App* app, IconPathResolveFn fn, void* user) {
    IconPathResolverSet(app, fn, user);
}

Str ResolveIconPath(Str path, const App* app, Arena* a) {
    return IconPathResolve(path, app, a);
}

// The paths already checked. Rust keeps a thread-local set; the port keeps
// it on the App, which is the only thread icons are built on.
struct IconCheckedPaths {
    Vec<Str> paths;
    ~IconCheckedPaths() { StrVecFree(&paths); }
};

bool WarnMissingIcon(Str path, App* app) {
    if (len(path) == 0 || !app) {
        return false;
    }
    IconCheckedPaths* checked = AppGlobalEnsure<IconCheckedPaths>(app);
    for (Str seen : checked->paths) {
        if (StrEq(seen, path)) {
            return false;
        }
    }
    VecAppend(checked->paths, StrDup(path));
    int builtIn = 0;
    if (AssetIconForPath(path, &builtIn) || AssetsExists(path)) {
        return false;
    }
    // The port's counterpart of `.with_assets(gpui_kit::assets::Assets)` is
    // an asset root (AssetsAddRoot / AssetsAddDefaultRoots) or an
    // application source (AssetsAddSource).
    logf(
        "icon `%s` renders blank: the asset source does not load it. Add the "
        "assets directory with AssetsAddDefaultRoots or AssetsAddRoot, or "
        "make your own AssetsAddSource serve this path.\n",
        path);
    return true;
}

El* Icon::IntoEl() {
    El* e = hasSize ? IconEl(a, name, size) : IconEl(a, name);
    if (source == IconSource::Data) {
        e->iconSvg = data;
    } else {
        // The path to draw. A named icon already has IconNamePath on the
        // element; an explicit Path replaces it, then the hook may too.
        Str drawn = path.s ? path : e->iconPath;
        if (cx && cx->app) {
            drawn = ResolveIconPath(drawn, cx->app, a);
#if defined(DEBUG)
            WarnMissingIcon(drawn, cx->app);
#endif
        }
        if (drawn.s) {
            e->iconPath = drawn;
        }
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
