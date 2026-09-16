#include "ui/group_box.h"

namespace gpui {

namespace component {

GroupBoxVariant GroupBoxVariantFromStr(Str text) {
    if (StrEqI(text, "fill")) {
        return GroupBoxVariant::Fill;
    }
    if (StrEqI(text, "outline")) {
        return GroupBoxVariant::Outline;
    }
    return GroupBoxVariant::Normal;
}

Str GroupBoxVariantAsStr(GroupBoxVariant variant) {
    switch (variant) {
        case GroupBoxVariant::Fill:
            return StrL("fill");
        case GroupBoxVariant::Outline:
            return StrL("outline");
        default:
            return StrL("normal");
    }
}

GroupBox* GroupBox::New(Ctx* cx) {
    Arena* a = cx->a;
    GroupBox* g = ArenaNew<GroupBox>(a);
    g->a = a;
    g->cx = cx;
    return g;
}

GroupBox* GroupBox::New(Ctx* cx, Str title) {
    GroupBox* g = New(cx);
    g->title = title;
    g->hasTitle = title.s != nullptr;
    return g;
}

GroupBox* GroupBox::Id(Str value) {
    id = value;
    return this;
}

GroupBox* GroupBox::Title(El* e) {
    titleEl = e;
    hasTitle = true;
    return this;
}

GroupBox* GroupBox::Child(El* e) {
    children.Append(a, e);
    return this;
}

GroupBox* GroupBox::WithVariant(GroupBoxVariant value) {
    variant = value;
    return this;
}

GroupBox* GroupBox::Normal() {
    return WithVariant(GroupBoxVariant::Normal);
}

GroupBox* GroupBox::Fill() {
    return WithVariant(GroupBoxVariant::Fill);
}

GroupBox* GroupBox::Outline() {
    return WithVariant(GroupBoxVariant::Outline);
}

GroupBox* GroupBox::Filled(bool v) {
    if (v) {
        variant = GroupBoxVariant::Fill;
    } else if (variant == GroupBoxVariant::Fill) {
        variant = GroupBoxVariant::Normal;
    }
    return this;
}

GroupBox* GroupBox::Refine(const Style& style, uint32_t fields) {
    StyleApplyFields(&rootStyle, style, fields);
    rootStyleSet |= fields;
    return this;
}

GroupBox* GroupBox::TitleStyle(const Style& style, uint32_t fields) {
    StyleApplyFields(&titleStyle, style, fields);
    titleStyleSet |= fields;
    return this;
}

GroupBox* GroupBox::ContentStyle(const Style& style, uint32_t fields) {
    StyleApplyFields(&contentStyle, style, fields);
    contentStyleSet |= fields;
    return this;
}

GroupBox* GroupBox::TitleSemibold(bool v) {
    titleSemibold = v;
    return this;
}
GroupBox* GroupBox::TitlePadX(float px) {
    titlePadX = px;
    return this;
}
GroupBox* GroupBox::ContentBg(Background c) {
    contentBg = c;
    hasContentBg = true;
    return this;
}
GroupBox* GroupBox::ContentRadius(float px) {
    contentRadius = px;
    return this;
}
GroupBox* GroupBox::ContentPad(float px) {
    contentPad = px;
    return this;
}
GroupBox* GroupBox::ContentBorder(float px) {
    contentBorder = px;
    return this;
}

El* GroupBox::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // The title sits outside the surface: only the content container takes
    // the fill, the border and the padding. Normal has none of the three, so
    // it spaces title and content further apart (gap_4 against gap_3).
    bool padded = variant != GroupBoxVariant::Normal;
    El* box = Div(a)
                  ->FlexCol()
                  ->Id(id)
                  ->W(kFill)
                  ->Gap(padded ? 12.f : 16.f)
                  ->Refine(rootStyle, rootStyleSet);
    if (hasTitle) {
        El* titleContent = titleEl ? titleEl : TextEl(a, title);
        El* titleBox = Div(a)
                           ->Fg(th.mutedFg)
                           ->LineHeight(1.25f)
                           ->Refine(titleStyle, titleStyleSet)
                           ->Child(titleContent);
        if (titleSemibold) {
            titleBox->Semibold();
        }
        if (titlePadX > 0) {
            titleBox->PadX(titlePadX);
        }
        box->Child(titleBox);
    }
    El* content = Div(a)->FlexCol()->W(kFill)->Gap(16)->Fg(th.groupBoxFg);
    content->Radius(contentRadius >= 0 ? contentRadius : th.radius);
    if (variant == GroupBoxVariant::Fill) {
        content->Bg(th.groupBox);
    }
    if (variant == GroupBoxVariant::Outline) {
        content->Border(1, th.border);
    }
    if (padded) {
        content->Pad(16);
    }
    if (hasContentBg) {
        content->Bg(contentBg);
    }
    if (contentPad >= 0) {
        content->Pad(contentPad);
    }
    if (contentBorder >= 0) {
        content->Border(contentBorder, th.border);
    }
    content->Refine(contentStyle, contentStyleSet);
    for (El* child : children) {
        content->Child(child);
    }
    box->Child(content);
    return box;
}

} // namespace component
} // namespace gpui
