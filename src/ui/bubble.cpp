#include "ui/bubble.h"
#include "ui/button.h"

namespace gpui {

namespace component {

BubbleContent* BubbleContent::New(Ctx* cx) {
    Arena* a = cx->a;
    BubbleContent* s = ArenaNew<BubbleContent>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

BubbleContent* BubbleContent::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

BubbleContent* BubbleContent::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* BubbleContent::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    bool dark = ThemeGet(cx->app) == ThemeMode::Dark;

    El* surface = Div(a)
                      ->MinW(0)
                      ->MaxW(kFill)
                      ->Radius(ThemeRadius2xl(th))
                      ->Border(1, th.transparent)
                      ->PadX(12)
                      ->PadY(8)
                      ->Font(14)
                      ->LineHeight(1.625f);
    // A ghost bubble has no surface to clip against; clipping would only cut
    // the shadows and overhanging controls of rich content.
    if (variant != BubbleVariant::Ghost) {
        surface->ClipX()->ClipY();
    }
    if (hasAlignment) {
        if (alignment == MessageAlignment::Start) {
            surface->SelfStart();
        } else {
            surface->SelfEnd();
        }
    }
    switch (variant) {
        case BubbleVariant::Filled:
            surface->Bg(th.tokens.primary)->Fg(th.primaryFg);
            break;
        // The theme's `secondary` role is tuned for buttons and sits a tier
        // darker than shadcn's conversation secondary; the near-background
        // `muted` tier matches shadcn's value in both themes.
        case BubbleVariant::Secondary:
            surface->Bg(th.tokens.muted)->Fg(th.secondaryFg);
            break;
        case BubbleVariant::Muted:
            surface->Bg(th.tokens.muted)->Fg(th.foreground);
            break;
        case BubbleVariant::Tinted:
            surface
                ->Bg(RgbaMixOklab(th.primary, th.background,
                                  dark ? 0.24f : 0.12f))
                ->Fg(th.foreground);
            break;
        case BubbleVariant::Outline:
            surface->Border(1, th.border)
                ->Bg(th.tokens.background)
                ->Fg(th.foreground);
            break;
        case BubbleVariant::Ghost:
            surface->Radius(0)
                ->Border(0, th.transparent)
                ->Bg(th.transparent)
                ->Fg(th.foreground)
                ->Pad(0);
            break;
        case BubbleVariant::Destructive:
            surface->Bg(RgbaOpacity(th.danger, dark ? 0.2f : 0.1f))
                ->Fg(th.danger);
            break;
    }
    if (styleSet) {
        surface->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        surface->Child(children[i]);
    }
    return surface;
}

BubbleGroup* BubbleGroup::New(Ctx* cx) {
    Arena* a = cx->a;
    BubbleGroup* s = ArenaNew<BubbleGroup>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

BubbleGroup* BubbleGroup::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

BubbleGroup* BubbleGroup::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* BubbleGroup::IntoEl() {
    El* column = Div(a)->FlexCol()->MinW(0)->Gap(8);
    if (styleSet) {
        column->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        column->Child(children[i]);
    }
    return column;
}

BubbleReactions* BubbleReactions::New(Ctx* cx) {
    Arena* a = cx->a;
    BubbleReactions* s = ArenaNew<BubbleReactions>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

BubbleReactions* BubbleReactions::Side(BubbleReactionSide value) {
    side = value;
    return this;
}

BubbleReactions* BubbleReactions::Alignment(MessageAlignment value) {
    alignment = value;
    return this;
}

BubbleReactions* BubbleReactions::Action(Button* action) {
    if (action) {
        BubbleReactionChild child;
        child.action = action;
        children.Append(a, child);
    }
    return this;
}

BubbleReactions* BubbleReactions::Child(El* e) {
    if (e) {
        BubbleReactionChild child;
        child.element = e;
        children.Append(a, child);
    }
    return this;
}

BubbleReactions* BubbleReactions::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* BubbleReactions::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    bool hasAction = false;
    for (int i = 0; i < children.len; i++) {
        if (children[i].action) {
            hasAction = true;
        }
    }

    El* pill = Div(a)
                   ->Absolute()
                   ->Flex()
                   ->FlexNone()
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->Gap(4)
                   ->Radius(th.radiusFull)
                   ->Border(3, th.background)
                   ->Bg(th.tokens.muted)
                   ->Fg(th.foreground)
                   ->Font(14);
    if (!hasAction) {
        pill->PadX(6)->PadY(2);
    }
    // Approximates shadcn's `translate-y-3/4`: there is no offset by a
    // fraction of the pill's own height here either, so this fixed value
    // leaves about three quarters of the default pill outside the bubble.
    if (side == BubbleReactionSide::Top) {
        pill->Top(-20);
    } else {
        pill->Bottom(-20);
    }
    if (alignment == MessageAlignment::Start) {
        pill->Left(12);
    } else {
        pill->Right(12);
    }
    if (styleSet) {
        pill->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        const BubbleReactionChild& child = children[i];
        if (child.action) {
            pill->Child(child.action->Rounded(th.radiusFull)->IntoEl());
        } else {
            pill->Child(child.element);
        }
    }
    return pill;
}

Bubble* Bubble::New(Ctx* cx) {
    Arena* a = cx->a;
    Bubble* s = ArenaNew<Bubble>(a);
    s->a = a;
    s->cx = cx;
    s->content = BubbleContent::New(cx);
    return s;
}

Bubble* Bubble::Alignment(MessageAlignment value) {
    alignment = value;
    hasAlignment = true;
    return this;
}

Bubble* Bubble::WithVariant(BubbleVariant value) {
    variant = value;
    return this;
}

Bubble* Bubble::Content(BubbleContent* value) {
    if (!value) {
        return this;
    }
    // Children already added directly to the bubble stay in front of the new
    // surface's own children. Rust moves the old list into the new surface;
    // the append-only ArenaVec goes the other way instead — the new surface's
    // children and its style land on the surface already holding them, which
    // is the same list in the same order.
    for (int i = 0; i < value->children.len; i++) {
        content->children.Append(a, value->children[i]);
    }
    StyleApplyFields(&content->style, value->style, value->styleSet);
    content->styleSet |= value->styleSet;
    return this;
}

Bubble* Bubble::Reactions(BubbleReactions* value) {
    reactions = value;
    return this;
}

Bubble* Bubble::Child(El* e) {
    content->Child(e);
    return this;
}

Bubble* Bubble::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* Bubble::IntoEl() {
    content->variant = variant;
    content->alignment = alignment;
    content->hasAlignment = hasAlignment;

    El* root = Div(a)->FlexCol()->MinW(0)->FlexNone()->Gap(4)->MaxWFrac(0.8f);
    if (variant == BubbleVariant::Ghost) {
        root->W(kFill)->MaxWFrac(1.f);
    }
    if (hasAlignment) {
        // `self_start().mr_auto()` / `self_end().ml_auto()`. There are no
        // auto margins here; align-self alone puts the box at the edge of a
        // column, which is every composition this component ships in.
        if (alignment == MessageAlignment::Start) {
            root->SelfStart();
        } else {
            root->SelfEnd();
        }
    }
    if (styleSet) {
        root->Refine(style, styleSet);
    }
    root->Child(content->IntoEl());
    if (reactions) {
        root->Child(reactions->IntoEl());
    }
    return root;
}

} // namespace component
} // namespace gpui
