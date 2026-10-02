#include "ui/marker.h"
#include "ui/spinner.h"
#include "base/motion.h"
#include <math.h>

namespace gpui {

namespace component {

MarkerIcon* MarkerIcon::New(Ctx* cx) {
    Arena* a = cx->a;
    MarkerIcon* s = ArenaNew<MarkerIcon>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

MarkerIcon* MarkerIcon::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

MarkerIcon* MarkerIcon::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* MarkerIcon::IntoEl() {
    // h_flex().size_4().flex_none().items_center().justify_center()
    El* row = Div(a)
                  ->FlexRow()
                  ->W(Rems(cx, 1))
                  ->H(Rems(cx, 1))
                  ->FlexNone()
                  ->ItemsCenter()
                  ->JustifyCenter();
    if (styleSet) {
        row->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        row->Child(children[i]);
    }
    return row;
}

MarkerContent* MarkerContent::New(Ctx* cx) {
    Arena* a = cx->a;
    MarkerContent* s = ArenaNew<MarkerContent>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

MarkerContent* MarkerContent::Text(Str text) {
    MarkerContentChild child;
    child.text = text;
    child.isText = true;
    children.Append(a, child);
    return this;
}

MarkerContent* MarkerContent::Child(El* e) {
    if (e) {
        MarkerContentChild child;
        child.element = e;
        children.Append(a, child);
    }
    return this;
}

MarkerContent* MarkerContent::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* MarkerContent::IntoEl() {
    bool animate = shimmer && !MotionReduced();
    bool hasText = false;
    for (int i = 0; i < children.len; i++) {
        if (children[i].isText) {
            hasText = true;
        }
    }
    float baseOpacity = (styleSet & StyleFieldOpacity) ? style.opacity : 1.f;

    El* content = Div(a)->MinW(0);
    if (separator) {
        // Between separator lines the label keeps its own width so the lines
        // take the rest; elsewhere it may shrink and wrap.
        content->FlexNone();
    }
    switch (alignment) {
        case MarkerAlignment::Start:
            content->TextLeft();
            break;
        case MarkerAlignment::Center:
            content->TextCenter();
            break;
        case MarkerAlignment::End:
            content->TextRight();
            break;
    }
    if (styleSet) {
        content->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        const MarkerContentChild& child = children[i];
        if (!child.isText) {
            content->Child(child.element);
            continue;
        }
        if (animate) {
            ShimmerText* text = ShimmerText::New(cx, child.text);
            // ("marker-loading-text", index)
            text->Id(ElementIdNamed(cx->a, StrL("marker-loading-text"),
                                    (uint64_t)i));
            text->WithShimmerStyle(shimmerStyle);
            if (hasFg) {
                text->Fg(fg);
            }
            content->Child(text->IntoEl());
        } else {
            // StyledText wraps inside the content box.
            content->Child(TextEl(a, child.text)->Wrap());
        }
    }

    if (animate && !hasText) {
        // with_animation("marker-loading-content", ..): a content slot with
        // no text of its own pulses instead of shimmering.
        float phase = ShimmerPhase(
            cx, MotionName(cx, StrL("marker-loading-content")), shimmerStyle);
        float highlight = cosf(phase * 2.f * kPi) * 0.5f + 0.5f;
        content->Opacity(baseOpacity * (highlight * 0.4f + 0.6f));
    }
    return content;
}

Marker* Marker::New(Ctx* cx) {
    Arena* a = cx->a;
    Marker* s = ArenaNew<Marker>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

Marker* Marker::Id(Str value) {
    id = value;
    hasId = true;
    return this;
}

Marker* Marker::Role(RoleOverride value) {
    role = value;
    return this;
}

Marker* Marker::WithVariant(MarkerVariant value) {
    variant = value;
    return this;
}

Marker* Marker::Alignment(MarkerAlignment value) {
    alignment = value;
    hasAlignment = true;
    return this;
}

MarkerAlignment Marker::ResolvedAlignment() const {
    if (hasAlignment) {
        return alignment;
    }
    return variant == MarkerVariant::Separator ? MarkerAlignment::Center
                                               : MarkerAlignment::Start;
}

Marker* Marker::Loading(bool value) {
    loading = value;
    return this;
}

Marker* Marker::WithLoadingStyle(MarkerLoadingStyle value) {
    loadingStyle = value;
    return this;
}

Marker* Marker::WithShimmerStyle(const ShimmerStyle& value) {
    shimmerStyle = value;
    return this;
}

Marker* Marker::SeparatorStyle(const Style& s, uint32_t fields) {
    StyleApplyFields(&separatorStyle, s, fields);
    separatorStyleSet |= fields;
    return this;
}

Marker* Marker::Icon(MarkerIcon* value) {
    if (value) {
        MarkerChild child;
        child.icon = value;
        children.Append(a, child);
    }
    return this;
}

Marker* Marker::Content(MarkerContent* value) {
    if (value) {
        MarkerChild child;
        child.content = value;
        children.Append(a, child);
    }
    return this;
}

Marker* Marker::Child(El* e) {
    if (e) {
        MarkerChild child;
        child.element = e;
        children.Append(a, child);
    }
    return this;
}

Marker* Marker::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* Marker::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    bool hasIcon = false;
    for (int i = 0; i < children.len; i++) {
        if (children[i].icon) {
            hasIcon = true;
        }
    }
    // The colour a shimmering content slot composites over. Rust reads the
    // inherited text style at paint time; the run's spans are computed while
    // the tree is built here, so the row's own colour is passed down.
    Rgba fg = (styleSet & StyleFieldColor) ? style.color : th.mutedFg;

    // h_flex().w_full().min_h(rems(1.)).gap_2().text_sm().line_height(1.5)
    El* row = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->W(kFill)
                  ->MinH(Rems(cx, 1))
                  ->Gap(Rems(cx, 0.5f))
                  ->Font(14)
                  ->LineHeight(1.5f)
                  ->Fg(th.mutedFg);
    MarkerAlignment align = ResolvedAlignment();
    switch (align) {
        case MarkerAlignment::Start:
            row->JustifyStart()->TextLeft();
            break;
        case MarkerAlignment::Center:
            row->JustifyCenter()->TextCenter();
            break;
        case MarkerAlignment::End:
            row->JustifyEnd()->TextRight();
            break;
    }
    if (variant == MarkerVariant::Border) {
        row->BorderB(1, th.border)->PadB(Rems(cx, 0.5f));
    }
    if (variant == MarkerVariant::Separator &&
        align != MarkerAlignment::Start) {
        El* rule = Div(a)
                       ->Flex1()
                       ->MinW(0)
                       ->H(1)
                       ->MarginR(Rems(cx, 0.25f))
                       ->Bg(th.border);
        if (separatorStyleSet) {
            rule->Refine(separatorStyle, separatorStyleSet);
        }
        row->Child(rule);
    }
    if (loading && loadingStyle == MarkerLoadingStyle::Spinner && !hasIcon) {
        row->Child(
            MarkerIcon::New(cx)
                ->Child(Spinner::New(cx)->WithSize(UiSize::XSmall)->IntoEl())
                ->IntoEl());
    }
    for (int i = 0; i < children.len; i++) {
        const MarkerChild& child = children[i];
        if (child.icon) {
            row->Child(child.icon->IntoEl());
        } else if (child.content) {
            child.content->shimmer =
                loading && loadingStyle == MarkerLoadingStyle::Shimmer;
            child.content->shimmerStyle = shimmerStyle;
            child.content->separator = variant == MarkerVariant::Separator;
            child.content->alignment = align;
            child.content->fg = fg;
            child.content->hasFg = true;
            row->Child(child.content->IntoEl());
        } else {
            row->Child(child.element);
        }
    }
    if (variant == MarkerVariant::Separator && align != MarkerAlignment::End) {
        El* rule = Div(a)
                       ->Flex1()
                       ->MinW(0)
                       ->H(1)
                       ->MarginL(Rems(cx, 0.25f))
                       ->Bg(th.border);
        if (separatorStyleSet) {
            rule->Refine(separatorStyle, separatorStyleSet);
        }
        row->Child(rule);
    }
    if (styleSet) {
        row->Refine(style, styleSet);
    }

    // `role` lives on the stateful element: an accessibility node needs the
    // stable identity only an element id provides.
    if (hasId) {
        row->PathId(id);
        if (role.kind == RoleOverrideKind::Role) {
            row->Role(role.role);
        }
    }
    return row;
}

} // namespace component
} // namespace gpui
