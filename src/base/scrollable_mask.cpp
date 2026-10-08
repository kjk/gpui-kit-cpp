#include "base/scrollable_mask.h"

#include "base/theme.h"
#include "gpui/paint.h"

namespace gpui {

static const float kNotchPi = 3.14159265f;

CornerNotch CornerNotchGeometry(float cornerX, float cornerY, float xDir,
                                float yDir, float radius) {
    // The arc is the short quarter nearest the corner. Angles are measured
    // clockwise from +x in y-down space, and the end angle is one quarter
    // turn from the start so the sweep stays the small arc.
    CornerNotch n = {};
    n.radius = radius;
    n.x = xDir > 0 ? cornerX : cornerX - radius;
    n.y = yDir > 0 ? cornerY : cornerY - radius;
    n.w = radius;
    n.h = radius;
    n.cx = cornerX + radius * xDir;
    n.cy = cornerY + radius * yDir;
    n.a0 = yDir > 0 ? -kNotchPi * 0.5f : kNotchPi * 0.5f;
    n.clockwise = xDir * yDir < 0;
    n.a1 = n.clockwise ? n.a0 + kNotchPi * 0.5f : n.a0 - kNotchPi * 0.5f;
    return n;
}

static Corners CoverRadii(const RoundedFrameCover* cover) {
    if (cover->viewport) {
        const Style& s = cover->viewport->style;
        if (s.hasCorners) {
            return s.corners;
        }
        return {s.radius, s.radius, s.radius, s.radius};
    }
    return cover->radii;
}

static void PaintRoundedFrameCover(PaintCtx* ctx, El* e, void* user) {
    RoundedFrameCover* cover = (RoundedFrameCover*)user;
    if (!cover) {
        return;
    }
    float x = e->x;
    float y = e->y;
    float w = e->w;
    float h = e->h;
    // Rust shifts its laid-out box up by one height, because that absolute
    // child lands one box below the viewport. Ours is placed on the parent's
    // origin. The viewport's own border box is what the notches have to
    // cover, including when the cover's layout box and the frame differ.
    if (cover->viewport) {
        x = cover->viewport->x;
        y = cover->viewport->y;
        w = cover->viewport->w;
        h = cover->viewport->h;
    }
    float border = cover->frameBorder;
    if (border != 0) {
        x -= border;
        y -= border;
        w += border * 2.f;
        h += border * 2.f;
    }
    Corners radii = CoverRadii(cover);
    if (radii.tl <= 0 && radii.tr <= 0 && radii.br <= 0 && radii.bl <= 0) {
        return;
    }
    Rgba color = cover->hasBackdrop ? cover->backdrop
                                    : base_theme::Theme::Global(ctx->app)
                                          .tokens.colors.background;
    struct Notch {
        float cornerX;
        float cornerY;
        float xDir;
        float yDir;
        float radius;
    };
    Notch notches[4] = {
        {x, y, 1.f, 1.f, radii.tl},
        {x + w, y, -1.f, 1.f, radii.tr},
        {x + w, y + h, -1.f, -1.f, radii.br},
        {x, y + h, 1.f, -1.f, radii.bl},
    };
    for (const Notch& notch : notches) {
        if (notch.radius <= 0) {
            continue;
        }
        CornerNotch geom = CornerNotchGeometry(
            notch.cornerX, notch.cornerY, notch.xDir, notch.yDir, notch.radius);
        Path* path = PathNew(ctx, true);
        PathMoveTo(path, notch.cornerX, notch.cornerY);
        PathLineTo(path, notch.cornerX + notch.radius * notch.xDir,
                   notch.cornerY);
        PathArcTo(path, geom.cx, geom.cy, notch.radius, geom.a0, geom.a1,
                  geom.clockwise);
        PathClose(path);
        PathFill(ctx, path, color);
        PathFree(path);
    }
}

RoundedFrameCover* RoundedFrameCover::Uniform(Ctx* cx, float radius) {
    RoundedFrameCover* cover = ArenaNew<RoundedFrameCover>(cx->a);
    cover->a = cx->a;
    cover->radii = {radius, radius, radius, radius};
    return cover;
}

RoundedFrameCover* RoundedFrameCover::FrameBorder(float width) {
    frameBorder = width;
    return this;
}

RoundedFrameCover* RoundedFrameCover::Backdrop(Rgba color) {
    backdrop = color;
    hasBackdrop = true;
    return this;
}

El* RoundedFrameCover::IntoEl() {
    // Absolute and parent-sized, the same layout ScrollableMask uses. No
    // hitbox: the cover only paints, and a press belongs to the frame under
    // it.
    El* layer =
        Div(a)->Absolute()->Left(0)->Top(0)->W(kFill)->H(kFill)->Grow(1);
    layer->customPaint = PaintRoundedFrameCover;
    layer->customUser = this;
    return layer;
}

ScrollableMask* ScrollableMask::New(Ctx* cx, Axis axis, El* element) {
    ScrollableMask* mask = ArenaNew<ScrollableMask>(cx->a);
    mask->a = cx->a;
    mask->axis = axis;
    mask->element = element;
    return mask;
}

El* ScrollableMask::Apply(El* element, Axis axis) {
    if (!element) {
        return nullptr;
    }
    element->ScrollMask(axis);
    if (!element->scrollId && !element->scrollFromPath) {
        element->ScrollFromPath();
    }
    return element;
}

ScrollableMask* ScrollableMask::Id(Str v) {
    id = v;
    return this;
}

ScrollableMask* ScrollableMask::Debug(bool v) {
    debug = v;
    return this;
}

El* ScrollableMask::IntoEl() {
    El* result = Apply(element, axis);
    if (!result) {
        return Div(a);
    }
    if (id.s) {
        result->PathId(id)->ScrollFromPath();
    }
    if (debug) {
        result->Border(1, Rgb(0xff, 0xff, 0));
    }
    return result;
}

El* HorizontalScrollArea(Ctx* cx, Str id, El* viewport) {
    if (!viewport) {
        return Div(cx->a);
    }
    // Rust wraps the viewport in a relative div and puts the mask beside it,
    // because a child would be prepainted with the scroll offset applied and
    // slide away from the frame. The mask is the viewport here, so there is
    // nothing to slide: the axis is marked on the element that scrolls.
    ScrollableMask::Apply(viewport, Axis::Horizontal);
    // The mask's id only matters when it has to keep its gesture axis lock
    // apart from another mask's; a viewport that already names its own scroll
    // state has that identity already.
    if (id.s && !viewport->scrollId && !viewport->scrollFromPath) {
        viewport->PathId(id)->ScrollFromPath();
    }
    // The mask stays on the viewport. The cover is a sibling so a scroll
    // offset cannot slide the notches off the frame's corners.
    RoundedFrameCover* cover = ArenaNew<RoundedFrameCover>(cx->a);
    cover->a = cx->a;
    cover->viewport = viewport;
    El* wrap = Div(cx->a)->W(kFill)->FlexCol();
    wrap->Child(viewport);
    wrap->Child(cover->IntoEl());
    return wrap;
}

} // namespace gpui
