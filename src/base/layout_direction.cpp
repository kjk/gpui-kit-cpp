#include "base/layout_direction.h"
#include "gpui/platform.h"

namespace gpui {

struct GlobalLayoutDirection {
    LayoutDirection direction = LayoutDirection::Ltr;
};

LayoutDirection LayoutDirectionGlobal(const App* app) {
    const auto* value = AppGlobalGet<GlobalLayoutDirection>(app);
    return value ? value->direction : LayoutDirection::Ltr;
}

void LayoutDirectionSetGlobal(App* app, LayoutDirection direction) {
    if (!app) return;
    const auto* current = AppGlobalGet<GlobalLayoutDirection>(app);
    if (current && current->direction == direction) return;
    if (auto* value = AppGlobalEnsure<GlobalLayoutDirection>(app)) {
        value->direction = direction;
        AppRefreshWindows(app);
    }
}

LayoutDirection LayoutDirectionFromLocale(Str locale) {
    int end = 0;
    while (end < len(locale) && locale.s[end] != '-' && locale.s[end] != '_' &&
           locale.s[end] != '.') {
        end++;
    }
    Str language(locale.s, end);
    static const Str rtl[] = {StrL("ar"), StrL("ckb"), StrL("dv"), StrL("fa"),
                              StrL("he"), StrL("iw"),  StrL("ks"), StrL("ps"),
                              StrL("sd"), StrL("ug"),  StrL("ur"), StrL("yi")};
    for (Str tag : rtl) {
        if (StrEqI(language, tag)) return LayoutDirection::Rtl;
    }
    return LayoutDirection::Ltr;
}

LayoutDirection LayoutDirectionSystem() {
    return LayoutDirectionFromLocale(PlatPreferredLocaleTemp());
}

void LayoutDirectionInit(App* app) {
    if (app && !AppGlobalGet<GlobalLayoutDirection>(app)) {
        auto* value = AppGlobalEnsure<GlobalLayoutDirection>(app);
        value->direction = TestPlatformNow(nullptr) ? LayoutDirection::Ltr
                                                    : LayoutDirectionSystem();
    }
}

Anchor LogicalAnchor(Anchor anchor, const App* app) {
    if (!LayoutIsRtl(app)) return anchor;
    switch (anchor) {
        case Anchor::TopLeft:
            return Anchor::TopRight;
        case Anchor::TopRight:
            return Anchor::TopLeft;
        case Anchor::BottomLeft:
            return Anchor::BottomRight;
        case Anchor::BottomRight:
            return Anchor::BottomLeft;
        case Anchor::LeftCenter:
            return Anchor::RightCenter;
        case Anchor::RightCenter:
            return Anchor::LeftCenter;
        default:
            return anchor;
    }
}

IconName DirectionalChevron(const App* app, bool towardEnd) {
    return towardEnd != LayoutIsRtl(app) ? IconName::ChevronRight
                                         : IconName::ChevronLeft;
}

El* El::FlexRowDir(const App* app) {
    return LayoutIsRtl(app) ? FlexRowReverse() : FlexRow();
}
El* El::PadS(float v, const App* app) {
    return LayoutIsRtl(app) ? PadR(v) : PadL(v);
}
El* El::PadE(float v, const App* app) {
    return LayoutIsRtl(app) ? PadL(v) : PadR(v);
}
El* El::MarginS(float v, const App* app) {
    return LayoutIsRtl(app) ? MarginR(v) : MarginL(v);
}
El* El::MarginE(float v, const App* app) {
    return LayoutIsRtl(app) ? MarginL(v) : MarginR(v);
}
El* El::InsetS(float v, const App* app) {
    return LayoutIsRtl(app) ? Right(v) : Left(v);
}
El* El::InsetE(float v, const App* app) {
    return LayoutIsRtl(app) ? Left(v) : Right(v);
}
El* El::BorderS(float width, Rgba color, const App* app) {
    return LayoutIsRtl(app) ? BorderR(width, color) : BorderL(width, color);
}
El* El::BorderE(float width, Rgba color, const App* app) {
    return LayoutIsRtl(app) ? BorderL(width, color) : BorderR(width, color);
}

static El* SetSideRadius(El* el, float radius, bool left) {
    Style& style = el->style;
    gpui::Corners corners = style.hasCorners
                                ? style.corners
                                : gpui::Corners{style.radius, style.radius,
                                                style.radius, style.radius};
    if (left) {
        corners.tl = corners.bl = radius;
    } else {
        corners.tr = corners.br = radius;
    }
    return el->Corners(corners.tl, corners.tr, corners.br, corners.bl);
}

El* El::RadiusS(float radius, const App* app) {
    return SetSideRadius(this, radius, !LayoutIsRtl(app));
}
El* El::RadiusE(float radius, const App* app) {
    return SetSideRadius(this, radius, LayoutIsRtl(app));
}
El* El::TextStart(const App* app) {
    return LayoutIsRtl(app) ? TextRight() : TextLeft();
}
El* El::TextEnd(const App* app) {
    return LayoutIsRtl(app) ? TextLeft() : TextRight();
}

} // namespace gpui
