#include "ui/empty.h"

namespace gpui {
namespace component {

template <typename T>
static T* EmptyPartNew(Ctx* cx) {
    T* value = ArenaNew<T>(cx->a);
    value->a = cx->a;
    return value;
}

static void EmptyChildren(El* parent, const ArenaVec<El*>& children) {
    for (El* child : children) {
        parent->Child(child);
    }
}

EmptyMedia* EmptyMedia::New(Ctx* cx) {
    EmptyMedia* value = EmptyPartNew<EmptyMedia>(cx);
    value->cx = cx;
    return value;
}
EmptyMedia* EmptyMedia::WithVariant(EmptyMediaVariant value) {
    variant = value;
    return this;
}
EmptyMedia* EmptyMedia::Child(El* child) {
    children.Append(a, child);
    return this;
}
EmptyMedia* EmptyMedia::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* EmptyMedia::IntoEl() {
    const Theme& theme = ThemeNow(cx->app);
    El* root =
        Div(a)->FlexCol()->Shrink0()->ItemsCenter()->JustifyCenter()->MarginB(
            8);
    if (variant == EmptyMediaVariant::Icon) {
        root->W(32)
            ->H(32)
            ->Radius(theme.radiusLg)
            ->Bg(theme.muted)
            ->Fg(theme.foreground)
            ->Font(16);
    }
    root->Refine(style, styleSet);
    EmptyChildren(root, children);
    return root;
}

EmptyTitle* EmptyTitle::New(Ctx* cx) {
    return EmptyPartNew<EmptyTitle>(cx);
}
EmptyTitle* EmptyTitle::Child(El* child) {
    children.Append(a, child);
    return this;
}
EmptyTitle* EmptyTitle::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* EmptyTitle::IntoEl() {
    El* root = Div(a)->MaxW(kFill)->MinW(0)->Font(14)->Medium()->Wrap();
    root->Refine(style, styleSet);
    EmptyChildren(root, children);
    return root;
}

EmptyDescription* EmptyDescription::New(Ctx* cx) {
    EmptyDescription* value = EmptyPartNew<EmptyDescription>(cx);
    value->cx = cx;
    return value;
}
EmptyDescription* EmptyDescription::Child(El* child) {
    children.Append(a, child);
    return this;
}
EmptyDescription* EmptyDescription::Refine(const Style& value,
                                           uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* EmptyDescription::IntoEl() {
    const Theme& theme = ThemeNow(cx->app);
    El* root = Div(a)
                   ->W(kFill)
                   ->MinW(0)
                   ->Font(14)
                   ->LineHeight(1.625f)
                   ->Fg(theme.mutedFg)
                   ->Wrap();
    root->Refine(style, styleSet);
    EmptyChildren(root, children);
    return root;
}

EmptyContent* EmptyContent::New(Ctx* cx) {
    return EmptyPartNew<EmptyContent>(cx);
}
EmptyContent* EmptyContent::Child(El* child) {
    children.Append(a, child);
    return this;
}
EmptyContent* EmptyContent::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* EmptyContent::IntoEl() {
    El* root = Div(a)
                   ->FlexCol()
                   ->W(kFill)
                   ->MaxW(384)
                   ->MinW(0)
                   ->ItemsCenter()
                   ->Gap(10)
                   ->Font(14);
    root->Refine(style, styleSet);
    EmptyChildren(root, children);
    return root;
}

EmptyHeader* EmptyHeader::New(Ctx* cx) {
    return EmptyPartNew<EmptyHeader>(cx);
}
EmptyHeader* EmptyHeader::Media(EmptyMedia* value) {
    media = value;
    return this;
}
EmptyHeader* EmptyHeader::Title(EmptyTitle* value) {
    title = value;
    return this;
}
EmptyHeader* EmptyHeader::Description(EmptyDescription* value) {
    description = value;
    return this;
}
EmptyHeader* EmptyHeader::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* EmptyHeader::IntoEl() {
    El* root = Div(a)
                   ->FlexCol()
                   ->W(kFill)
                   ->MaxW(384)
                   ->MinW(0)
                   ->ItemsCenter()
                   ->Gap(8)
                   ->Refine(style, styleSet);
    if (media) root->Child(media->IntoEl());
    if (title) root->Child(title->IntoEl());
    if (description) root->Child(description->IntoEl());
    return root;
}

Empty* Empty::New(Ctx* cx) {
    Empty* value = EmptyPartNew<Empty>(cx);
    value->cx = cx;
    return value;
}
Empty* Empty::Header(EmptyHeader* value) {
    header = value;
    return this;
}
Empty* Empty::Content(EmptyContent* value) {
    content = value;
    return this;
}
Empty* Empty::Child(El* child) {
    children.Append(a, child);
    return this;
}
Empty* Empty::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* Empty::IntoEl() {
    const Theme& theme = ThemeNow(cx->app);
    El* root = Div(a)
                   ->FlexCol()
                   ->W(kFill)
                   ->MinW(0)
                   ->Flex1()
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->Gap(16)
                   ->Pad(24)
                   ->Radius(theme.radius * 2.f)
                   // border_dashed + border_color are styling hooks; Empty
                   // itself has no visible border until a caller refines one.
                   ->Border(0, theme.border)
                   ->Dashed()
                   ->Fg(theme.foreground)
                   ->Refine(style, styleSet);
    if (header) root->Child(header->IntoEl());
    if (content) root->Child(content->IntoEl());
    EmptyChildren(root, children);
    return root;
}

} // namespace component
} // namespace gpui
