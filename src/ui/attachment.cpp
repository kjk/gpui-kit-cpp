#include "ui/attachment.h"
#include "ui/button.h"
#include "ui/i18n.h"
#include "ui/progress.h"
#include "ui/spinner.h"
#include "ui/tooltip.h"
#include "base/lib.h"
#include "base/motion.h"
#include "gpui/paint.h"

namespace gpui {

namespace component {

// How far the remove control rides outside the card's upper trailing corner.
static const float kRemoveOverhang = 6.f;
// The remove control: a small surface-coloured disc with a hairline border.
static const float kRemoveButtonSize = 20.f;
// The retry control over an image preview.
static const float kRetryButtonSize = 24.f;
// How much an image preview is darkened while it uploads or processes.
static const float kProgressScrim = 0.35f;
// How much an image preview is darkened once it has failed.
static const float kFailedScrim = 0.55f;
// The upload bar's thickness.
static const float kUploadBarThickness = 2.f;
// The card's border width; the padding box's corners are this much tighter.
static const float kCardBorder = 1.f;
// How long an edge fade takes to appear or disappear.
static const float kEdgeFadeTransitionMs = 200.f;

AttachmentCardMetrics AttachmentMetrics(const Ctx* cx, UiSize size) {
    // card_metrics, in rems at the window's rem size.
    auto r = [cx](float rems) { return Rems(cx, rems); };
    AttachmentCardMetrics m;
    switch (size) {
        case UiSize::XSmall:
            m = {r(2.5f),   r(11.f),  r(1.75f),  r(0.875f),  r(0.25f),
                 r(0.375f), r(0.25f), r(0.375f), r(0.6875f), r(0.625f)};
            break;
        case UiSize::Small:
            m = {r(3.f),  r(12.5f),  r(2.f),  r(1.f),   r(0.375f),
                 r(0.5f), r(0.375f), r(0.5f), r(0.75f), r(0.6875f)};
            break;
        case UiSize::Large:
            m = {r(4.f), r(17.f),  r(2.75f), r(1.5f),   r(0.625f),
                 r(1.f), r(0.75f), r(0.75f), r(0.875f), r(0.8125f)};
            break;
        case UiSize::Size: {
            // A custom density scales the medium geometry from its base value.
            float v = size.pixels;
            m = {v * 3.5f,  v * 14.5f, v * 2.375f, v * 1.25f,   v * 0.5f,
                 v * 0.75f, v * 0.5f,  v * 0.625f, v * 0.8125f, v * 0.75f};
            break;
        }
        default:
            m = {r(3.5f),  r(14.5f), r(2.375f), r(1.25f),   r(0.5f),
                 r(0.75f), r(0.5f),  r(0.625f), r(0.8125f), r(0.75f)};
            break;
    }
    return m;
}

// card_radius: radius.md at XSmall, radius.lg otherwise.
static float CardRadius(UiSize size, const Theme& th) {
    return size == UiSize::XSmall ? th.radius : th.radiusLg;
}

// Styled::shadow_sm at the pinned GPUI: 0 1px 3px 0 and 0 1px 2px -1px, both
// black at 10%.
static El* ShadowSm(Arena* a, El* e) {
    BoxShadow* shadows = (BoxShadow*)Alloc(a, (int)sizeof(BoxShadow) * 2);
    if (!shadows) {
        return e;
    }
    shadows[0] = {0, 1, 3, 0, Rgba8(0, 0, 0, 26), false};
    shadows[1] = {0, 1, 2, -1, Rgba8(0, 0, 0, 26), false};
    return e->Shadows(shadows, 2);
}

// A control's name under the attachment's id, the way Rust keys it on
// `(id, "remove")`.
static Str ControlId(Arena* a, Str id, const char* control) {
    return StrDup(a, fmt("%s/%s", id, Str(control)));
}

AttachmentMedia* AttachmentMedia::New(Ctx* cx) {
    Arena* a = cx->a;
    AttachmentMedia* s = ArenaNew<AttachmentMedia>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

AttachmentMedia* AttachmentMedia::Src(Str value) {
    source = value;
    hasSource = true;
    return this;
}

AttachmentMedia* AttachmentMedia::Overlay(El* overlay) {
    if (overlay) {
        overlays.Append(a, Div(a)
                               ->Absolute()
                               ->Left(0)
                               ->Top(0)
                               ->Right(0)
                               ->Bottom(0)
                               ->Flex()
                               ->ItemsCenter()
                               ->JustifyCenter()
                               ->Child(overlay));
    }
    return this;
}

AttachmentMedia* AttachmentMedia::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

AttachmentMedia* AttachmentMedia::WithSize(UiSize value) {
    size = value;
    hasSize = true;
    return this;
}

AttachmentMedia* AttachmentMedia::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

AttachmentMedia* AttachmentMedia::Layout(const AttachmentSlotLayout& layout) {
    if (!hasSize) {
        size = layout.size;
        hasSize = true;
    }
    status = layout.status;
    axis = layout.axis;
    flush = layout.flush;
    retry = layout.retry;
    progress = layout.progress;
    id = layout.id;
    hasId = layout.hasId;
    return this;
}

// retry_button: the retry control over a failed image preview, a
// surface-coloured disc with the destructive refresh glyph.
static El* RetryButton(Ctx* cx, Str id, Listener onRetry) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* button = Button::New(cx, ControlId(a, id, "retry"))
                     ->Ghost()
                     ->AccessibilityLabel(Tr("Attachment.Retry"))
                     ->Child(IconEl(a, IconName::RefreshCw, 12)->Fg(th.danger))
                     ->Size(kRetryButtonSize)
                     ->OnClick(onRetry)
                     ->IntoEl();
    button->Pad(0)->Radius(kRadiusFull)->Bg(th.tokens.background);
    return ShadowSm(a, button);
}

El* AttachmentMedia::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    UiSize resolved = hasSize ? size : UiSize::Medium;
    AttachmentCardMetrics metrics = AttachmentMetrics(cx, resolved);
    // Flush media sits inside the card's 1px border, so its corners are one
    // border width tighter than the card's to stay concentric. radius.sm is
    // half the theme radius and radius.md the radius itself.
    float radius = flush ? std::max(CardRadius(resolved, th) - 1.f, 0.f)
                   : resolved == UiSize::XSmall ? th.radius * 0.5f
                                                : th.radius;
    // A caller's `.rounded()` refinement, which the image and the scrim take
    // too.
    if (styleSet & StyleFieldRadius) {
        radius = style.radius;
    }
    float glyph = metrics.mediaGlyph;
    UiSize spinnerSize =
        resolved == UiSize::XSmall ? UiSize::XSmall : UiSize::Small;
    float ringSize = resolved == UiSize::XSmall ? 14.f : 20.f;
    bool failedMedia = AttachmentStatusIsFailed(status) && !hasSource;
    Str ringId =
        hasId ? ControlId(a, id, "progress") : StrL("attachment-progress");
    // In progress: a determinate ring while uploading with a known
    // percentage, a spinner otherwise.
    auto busy = [&](Rgba color) -> El* {
        if (progress >= 0) {
            return ProgressCircle::New(cx)
                ->Id(ringId)
                ->Value(progress)
                ->Color(color)
                ->Size(ringSize)
                ->IntoEl();
        }
        return Spinner::New(cx)->WithSize(spinnerSize)->Color(color)->IntoEl();
    };
    El* box = Div(a)
                  ->Flex()
                  ->Shrink0()
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->ClipX()
                  ->ClipY();
    if (axis == Axis::Horizontal) {
        box->W(metrics.media)->H(metrics.media);
    }
    // An icon child without its own size follows the slot's text size.
    box->Font(FontPx(cx, glyph));
    if (axis == Axis::Vertical) {
        box->W(kFill)->Aspect(1.f);
    }
    box->Radius(radius);
    box->Bg(failedMedia ? RgbaOpacity(th.danger, 0.1f) : th.muted);
    box->Fg(failedMedia ? th.danger : th.foreground);
    if (hasSource) {
        // GPUI clips rectangularly, so the slot's overflow_hidden cannot
        // round the image: it carries the slot's radius itself.
        box->Child(ImageEl(a, source)
                       ->Absolute()
                       ->Left(0)
                       ->Top(0)
                       ->Right(0)
                       ->Bottom(0)
                       ->SizeFull()
                       ->Radius(radius));
    }
    // An icon slot shows the status itself; children come back with
    // Complete.
    if (!hasSource && AttachmentStatusIsInProgress(status)) {
        box->Child(busy(th.primary));
    } else if (!hasSource && AttachmentStatusIsFailed(status)) {
        // Failed with no picture: the retry button itself when a retry is
        // offered, the ban glyph for a rejection that cannot be retried.
        box->Child(retry.IsValid() && hasId ? RetryButton(cx, id, retry)
                                            : IconEl(a, IconName::Ban, glyph));
    } else {
        for (int i = 0; i < children.len; i++) {
            box->Child(children[i]);
        }
    }
    // An image keeps its colours and takes a scrim instead, so the white
    // control on top stays legible on any picture.
    if (hasSource && (AttachmentStatusIsInProgress(status) ||
                      AttachmentStatusIsFailed(status))) {
        float opacity = kProgressScrim;
        El* control = nullptr;
        if (AttachmentStatusIsInProgress(status)) {
            control = busy(Rgb(0xff, 0xff, 0xff));
        } else {
            opacity = kFailedScrim;
            control = retry.IsValid() && hasId
                          ? RetryButton(cx, id, retry)
                          : IconEl(a, IconName::Ban, glyph)
                                ->Fg(Rgb(0xff, 0xff, 0xff));
        }
        box->Child(Div(a)
                       ->Absolute()
                       ->Left(0)
                       ->Top(0)
                       ->Right(0)
                       ->Bottom(0)
                       ->Flex()
                       ->ItemsCenter()
                       ->JustifyCenter()
                       ->Radius(radius)
                       ->Bg(Rgba8(0, 0, 0, (uint8_t)(opacity * 255.f + 0.5f)))
                       ->Child(control));
    }
    for (int i = 0; i < overlays.len; i++) {
        box->Child(overlays[i]);
    }
    if (styleSet) {
        box->Refine(style, styleSet);
    }
    return box;
}

AttachmentTitle* AttachmentTitle::New(Ctx* cx, Str text) {
    Arena* a = cx->a;
    AttachmentTitle* s = ArenaNew<AttachmentTitle>(a);
    s->a = a;
    s->cx = cx;
    s->text = text;
    return s;
}

AttachmentTitle* AttachmentTitle::Status(AttachmentStatus value) {
    status = value;
    hasStatus = true;
    return this;
}

AttachmentTitle* AttachmentTitle::WithShimmerStyle(const ShimmerStyle& value) {
    shimmerStyle = value;
    hasShimmerStyle = true;
    return this;
}

AttachmentTitle* AttachmentTitle::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* AttachmentTitle::IntoEl() {
    bool loading = hasStatus && AttachmentStatusIsInProgress(status);
    El* box = Div(a)->MaxW(kFill)->MinW(0)->Truncate()->Medium();
    if (loading) {
        ShimmerText* shimmer = ShimmerText::New(cx, text);
        if (hasShimmerStyle) {
            shimmer->WithShimmerStyle(shimmerStyle);
        }
        if (styleSet & StyleFieldColor) {
            shimmer->Fg(style.color);
        }
        box->Child(shimmer->IntoEl());
    } else {
        // El::Truncate is a property of the run, not of the box around it,
        // so the ellipsis goes on the text element rather than on the div
        // Rust puts `.truncate()` on.
        box->Child(TextEl(a, text)->Truncate()->MaxW(kFill));
    }
    if (styleSet) {
        box->Refine(style, styleSet);
    }
    return box;
}

AttachmentDescription* AttachmentDescription::New(Ctx* cx, Str text) {
    Arena* a = cx->a;
    AttachmentDescription* s = ArenaNew<AttachmentDescription>(a);
    s->a = a;
    s->cx = cx;
    s->text = text;
    return s;
}

AttachmentDescription* AttachmentDescription::Status(AttachmentStatus value) {
    status = value;
    hasStatus = true;
    return this;
}

AttachmentDescription* AttachmentDescription::Refine(const Style& s,
                                                     uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

El* AttachmentDescription::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Rgba color = (hasStatus && AttachmentStatusIsFailed(status)) ? th.danger
                                                                 : th.mutedFg;
    float font = FontPx(
        cx, AttachmentMetrics(cx, hasSize ? size : UiSize::Medium).description);
    El* box = Div(a)
                  ->MaxW(kFill)
                  ->MinW(0)
                  ->Truncate()
                  ->Font(font)
                  ->LineHeight(1.25f)
                  ->Fg(color)
                  ->Child(TextEl(a, text)->Truncate()->MaxW(kFill));
    if (styleSet) {
        box->Refine(style, styleSet);
    }
    return box;
}

AttachmentContent* AttachmentContent::New(Ctx* cx) {
    Arena* a = cx->a;
    AttachmentContent* s = ArenaNew<AttachmentContent>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

AttachmentContent* AttachmentContent::Title(AttachmentTitle* value) {
    if (value) {
        AttachmentContentChild child;
        child.title = value;
        children.Append(a, child);
    }
    return this;
}

AttachmentContent* AttachmentContent::Description(
    AttachmentDescription* value) {
    if (value) {
        AttachmentContentChild child;
        child.description = value;
        children.Append(a, child);
    }
    return this;
}

AttachmentContent* AttachmentContent::Child(El* e) {
    if (e) {
        AttachmentContentChild child;
        child.element = e;
        children.Append(a, child);
    }
    return this;
}

AttachmentContent* AttachmentContent::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

AttachmentContent* AttachmentContent::Layout(
    const AttachmentSlotLayout& layout) {
    verticalLayout = layout.axis == Axis::Vertical;
    status = layout.status;
    retry = layout.retry;
    retryId = layout.id;
    progress = layout.progress;
    for (int i = 0; i < children.len; i++) {
        AttachmentContentChild& child = children[i];
        if (child.title && !child.title->hasStatus) {
            child.title->Status(layout.status);
        }
        if (child.description) {
            if (!child.description->hasStatus) {
                child.description->Status(layout.status);
            }
            if (!child.description->hasSize) {
                child.description->size = layout.size;
                child.description->hasSize = true;
            }
        }
    }
    return this;
}

El* AttachmentContent::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // The retry link follows the first typed description while failed; the
    // percentage joins it while uploading.
    bool retryPending = retry.IsValid() && AttachmentStatusIsFailed(status);
    bool showProgress = progress >= 0 && AttachmentStatusIsUploading(status);
    El* column = Div(a)
                     ->FlexCol()
                     ->MaxW(kFill)
                     ->MinW(0)
                     // `flex_1()` upstream. `Grow(1)` — grow 1, shrink 1,
                     // basis *auto* — because this port's flex intrinsic
                     // sizing gives a `basis: 0` item no max-content
                     // contribution. The chip is a fixed width now, so the
                     // column takes the slack either way.
                     ->Grow(1)
                     ->Gap(Rems(cx, 0.125f))
                     ->LineHeight(1.25f);
    if (verticalLayout) {
        column->W(kFill)->PadX(Rems(cx, 0.25f));
    }
    for (int i = 0; i < children.len; i++) {
        const AttachmentContentChild& child = children[i];
        if (child.title) {
            column->Child(child.title->IntoEl());
        } else if (child.description) {
            if (retryPending) {
                retryPending = false;
                column->Child(
                    Div(a)
                        ->FlexRow()
                        ->ItemsCenter()
                        ->MaxW(kFill)
                        ->MinW(0)
                        ->Gap(Rems(cx, 0.25f))
                        ->Child(child.description->IntoEl())
                        ->Child(TextEl(a, StrL("\xC2\xB7"))
                                    ->Font(12)
                                    ->Fg(th.mutedFg))
                        ->Child(Button::New(cx, ControlId(a, retryId, "retry"))
                                    ->Link()
                                    ->WithSize(UiSize::XSmall)
                                    ->Label(Tr("Attachment.Retry"))
                                    ->OnClick(retry)
                                    ->IntoEl()));
                continue;
            }
            if (showProgress) {
                child.description->text =
                    StrDup(a, fmt("%s \xC2\xB7 %d%%", child.description->text,
                                  (int)lroundf(progress)));
            }
            column->Child(child.description->IntoEl());
        } else {
            column->Child(child.element);
        }
    }
    if (styleSet) {
        column->Refine(style, styleSet);
    }
    return column;
}

AttachmentActions* AttachmentActions::New(Ctx* cx) {
    Arena* a = cx->a;
    AttachmentActions* s = ArenaNew<AttachmentActions>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

AttachmentActions* AttachmentActions::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

AttachmentActions* AttachmentActions::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

AttachmentActions* AttachmentActions::LayoutForAxis(Axis axis) {
    verticalLayout = axis == Axis::Vertical;
    return this;
}

El* AttachmentActions::IntoEl() {
    El* row = Div(a)->Flex()->Shrink0()->ItemsCenter()->Gap(Rems(cx, 0.25f));
    if (verticalLayout) {
        row->Absolute()->Top(Rems(cx, 0.75f))->Right(Rems(cx, 0.75f));
    }
    // The actions cluster owns its presses: an action, or the gap between
    // actions, must not also arm the whole-card click layer below.
    row->StopMouseDown();
    for (int i = 0; i < children.len; i++) {
        row->Child(children[i]);
    }
    if (styleSet) {
        row->Refine(style, styleSet);
    }
    return row;
}

Attachment* Attachment::New(Ctx* cx) {
    Arena* a = cx->a;
    Attachment* s = ArenaNew<Attachment>(a);
    s->a = a;
    s->cx = cx;
    return s;
}

Attachment* Attachment::Id(Str value) {
    id = value;
    hasId = true;
    return this;
}

Attachment* Attachment::OnClick(Listener handler) {
    onClick = handler;
    return this;
}

Attachment* Attachment::OnRemove(Listener handler) {
    onRemove = handler;
    return this;
}

Attachment* Attachment::OnRetry(Listener handler) {
    onRetry = handler;
    return this;
}

Attachment* Attachment::Progress(float percent) {
    progress = std::min(std::max(percent, 0.f), 100.f);
    return this;
}

Attachment* Attachment::Tooltip(Str text) {
    tooltip = text;
    hasTooltip = true;
    return this;
}

Attachment* Attachment::Status(AttachmentStatus value) {
    status = value;
    return this;
}

Attachment* Attachment::WithAxis(Axis value) {
    axis = value;
    return this;
}

Attachment* Attachment::Media(AttachmentMedia* value) {
    media = value;
    return this;
}

Attachment* Attachment::Content(AttachmentContent* value) {
    content = value;
    return this;
}

Attachment* Attachment::Actions(AttachmentActions* value) {
    actions = value;
    return this;
}

Attachment* Attachment::WithSize(UiSize value) {
    size = value;
    return this;
}

Attachment* Attachment::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

Listener Attachment::RetryControl() const {
    if (!AttachmentStatusIsFailed(status) || !hasId) {
        return Listener{};
    }
    return onRetry;
}

void Attachment::LayoutSlots() {
    AttachmentSlotLayout layout;
    layout.size = size;
    layout.status = status;
    layout.axis = axis;
    // A vertical card without content is an image tile: the media fills the
    // card flush with its border.
    layout.flush = axis == Axis::Vertical && !content;
    layout.retry = RetryControl();
    // Progress is only meaningful while uploading; processing is
    // indeterminate.
    layout.progress = AttachmentStatusIsUploading(status) ? progress : -1.f;
    layout.id = id;
    layout.hasId = hasId;
    if (media) {
        media->Layout(layout);
    }
    if (content) {
        content->Layout(layout);
    }
    if (actions) {
        actions->LayoutForAxis(axis);
    }
}

int AttachmentUploadBarPath(float w, float h, float percent, float radius,
                            Point* out) {
    const int kSteps = 6;
    float r = radius - kCardBorder;
    float limit = std::min(w, h) / 2.f;
    r = std::min(std::max(r, 0.f), std::max(limit, 0.f));
    float top = h - kUploadBarThickness;
    float end = w * std::min(std::max(percent / 100.f, 0.f), 1.f);
    if (end <= 0.f || top <= 0.f) {
        return 0;
    }
    // The corner circles' centre line; rows below it lie in the arcs.
    float centreY = h - r;
    auto arcDx = [&](float y) {
        if (r > 0.f && y > centreY) {
            return sqrtf(std::max(r * r - (y - centreY) * (y - centreY), 0.f));
        }
        return r;
    };
    auto leftAt = [&](float y) { return r - arcDx(y); };
    auto rightAt = [&](float y) { return std::min(w - r + arcDx(y), end); };
    int n = 0;
    out[n++] = {leftAt(top), top};
    // Down the right end, then back up the left arc.
    for (int i = 0; i <= kSteps; i++) {
        float y = top + kUploadBarThickness * (float)i / (float)kSteps;
        out[n++] = {rightAt(y), y};
    }
    for (int i = kSteps; i >= 0; i--) {
        float y = top + kUploadBarThickness * (float)i / (float)kSteps;
        out[n++] = {leftAt(y), y};
    }
    return n;
}

struct AttachmentUploadBar {
    float percent = 0;
    float radius = 0;
    Rgba color = {};
};

static void PaintUploadBar(PaintCtx* ctx, El* e, void* user) {
    const AttachmentUploadBar* bar = (const AttachmentUploadBar*)user;
    Point points[kAttachmentUploadBarPoints];
    int n =
        AttachmentUploadBarPath(e->w, e->h, bar->percent, bar->radius, points);
    if (n <= 0) {
        return;
    }
    Path* path = PathNew(ctx, true);
    if (!path) {
        return;
    }
    PathMoveTo(path, e->x + points[0].x, e->y + points[0].y);
    for (int i = 1; i < n; i++) {
        PathLineTo(path, e->x + points[i].x, e->y + points[i].y);
    }
    PathClose(path);
    PathFill(ctx, path, bar->color);
    PathFree(path);
}

// upload_bar: a thin bar along the card's bottom edge. GPUI clips
// rectangularly, so a plain rectangle could not follow the corner curve; the
// bar is a filled path whose ends trace the inner corner arcs.
static El* UploadBar(Arena* a, float percent, float radius, Rgba color) {
    AttachmentUploadBar* bar = ArenaNew<AttachmentUploadBar>(a);
    bar->percent = percent;
    bar->radius = radius;
    bar->color = color;
    El* e = Div(a)->Absolute()->Left(0)->Top(0)->Right(0)->Bottom(0);
    e->customPaint = PaintUploadBar;
    e->customUser = bar;
    return e;
}

// remove_button: a surface-coloured disc with a hairline border and the
// foreground glyph. The glyph goes in as a child: an icon-only Button scales
// its icon with the button, and this disc wants a much smaller one.
static El* RemoveButton(Ctx* cx, Str id, Listener onRemove) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Rgba muted = th.tokens.muted.color;
    // The custom variant thins its colour to 20%; the disc must stay opaque,
    // so the surface is set on the instance instead.
    El* button = Button::New(cx, ControlId(a, id, "remove"))
                     ->Custom(ButtonCustomVariant::New(cx->app)
                                  .Hover(muted)
                                  .Active(muted)
                                  .Foreground(th.foreground))
                     ->AccessibilityLabel(Tr("Attachment.Remove"))
                     ->Child(IconEl(a, IconName::Close, 10)->Fg(th.foreground))
                     ->Size(kRemoveButtonSize)
                     ->OnClick(onRemove)
                     ->IntoEl();
    button->Pad(0)
        ->Radius(kRadiusFull)
        ->Bg(th.tokens.background)
        ->Border(1, th.border);
    return ShadowSm(a, button);
}

El* Attachment::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    bool hasMedia = media != nullptr;
    bool hasContent = content != nullptr;
    bool clickable = hasId && onClick.IsValid();
    bool progressBar = progress >= 0 && AttachmentStatusIsUploading(status) &&
                       axis == Axis::Horizontal;
    AttachmentCardMetrics metrics = AttachmentMetrics(cx, size);
    float radius = CardRadius(size, th);
    bool flush = axis == Axis::Vertical && !hasContent;

    LayoutSlots();

    El* card = Div(a)
                   ->Flex()
                   ->FlexNone()
                   ->MaxW(kFill)
                   ->MinW(0)
                   ->Radius(radius)
                   ->Border(1, AttachmentStatusIsFailed(status) ? th.danger
                                                                : th.border)
                   ->Bg(th.tokens.background)
                   ->Fg(th.foreground)
                   ->LineHeight(1.25f)
                   ->Font(FontPx(cx, metrics.text));
    if (AttachmentStatusIsPending(status)) {
        card->Dashed();
    }
    if (clickable) {
        card->HoverBg(BackgroundOpacity(th.tokens.muted, 0.5f));
    }
    if (axis == Axis::Horizontal) {
        // A chip: fixed height, fixed width once it carries content, the
        // media slot flush to the leading padding.
        card->ItemsCenter()
            ->H(metrics.height)
            ->Gap(metrics.gap)
            ->PadL(metrics.paddingStart)
            ->PadR(metrics.paddingEnd);
        if (hasContent) {
            card->W(metrics.chipWidth);
        }
        if (!hasContent && !hasMedia) {
            card->PadL(metrics.paddingEnd);
        }
    } else if (flush) {
        // An image tile: a square the media fills edge to edge.
        card->W(metrics.height)->H(metrics.height);
    } else {
        // A preview card: media above the metadata, w(rems(7.5)).
        card->W(Rems(cx, 7.5f))
            ->FlexCol()
            ->ItemsStart()
            ->Gap(metrics.gap)
            ->Pad(metrics.cardPadding);
    }
    if (media) {
        card->Child(media->IntoEl());
    }
    if (content) {
        card->Child(content->IntoEl());
    }
    // A thin bar along the bottom edge tracks the upload, hugging the card's
    // rounded corners.
    if (progressBar) {
        card->Child(UploadBar(a, progress, radius, th.primary));
    }
    if (clickable) {
        // The click layer is added before the actions slot, so the actions'
        // hitboxes stay on top and their buttons keep working.
        card->Child(Div(a)
                        ->PathClick(id)
                        ->Absolute()
                        ->Left(0)
                        ->Top(0)
                        ->Right(0)
                        ->Bottom(0)
                        ->OnClick(onClick));
    }
    if (actions) {
        card->Child(actions->IntoEl());
    }
    if (styleSet) {
        card->Refine(style, styleSet);
    }
    if (hasId && hasTooltip) {
        // `.id((id, "card")).tooltip(..)`: the hover needs the card to be an
        // element of its own.
        card->PathClick(ControlId(a, id, "card"))->Tip(tooltip);
    }

    if (!hasId || !onRemove.IsValid()) {
        return card;
    }
    // The remove control rides outside the card, so the card gets a hover
    // group and room for the overhang.
    El* corner = Div(a)->Absolute()->Top(0)->Right(0);
    if (!IsMobile()) {
        corner->GroupHoverVisible();
    }
    return Div(a)
        ->FlexNone()
        ->MaxW(kFill)
        ->MinW(0)
        ->Group()
        ->PadT(kRemoveOverhang)
        ->PadR(kRemoveOverhang)
        ->Child(card)
        ->Child(corner->Child(RemoveButton(cx, id, onRemove)));
}

AttachmentGroup* AttachmentGroup::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    AttachmentGroup* s = ArenaNew<AttachmentGroup>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    return s;
}

AttachmentGroup* AttachmentGroup::Child(El* e) {
    if (e) {
        children.Append(a, e);
    }
    return this;
}

AttachmentGroup* AttachmentGroup::TrackScroll(float value, Listener fn) {
    scrollX = value;
    onScroll = fn;
    return this;
}

AttachmentGroup* AttachmentGroup::ScrollX(float value) {
    scrollX = value;
    return this;
}

AttachmentGroup* AttachmentGroup::OnScroll(Listener fn) {
    onScroll = fn;
    return this;
}

AttachmentGroup* AttachmentGroup::WithEdgeFade(Rgba color) {
    edgeFade = color;
    hasEdgeFade = true;
    return this;
}

AttachmentGroup* AttachmentGroup::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}

// AttachmentGroupScroll: the scroll state a group keeps for itself when the
// caller tracks none, keyed on the group's id.
struct AttachmentGroupScroll {
    float x = 0;
    // Whether the frame after the first layout has been asked for: the
    // scroll extent is unknown until then, so the fades need one more pass.
    bool primed = false;

    static void OnScroll(AttachmentGroupScroll* self, Ctx* cx,
                         const ScrollEvent* ev) {
        self->x = ev->offsetX;
        Notify(cx);
    }
};

El* AttachmentGroup::IntoEl() {
    Entity<AttachmentGroupScroll> scroll =
        ElementStateEntity<AttachmentGroupScroll>(
            cx, id, StrL("AttachmentGroupScroll"));
    AttachmentGroupScroll* own = scroll.Get(cx);
    float offset = scrollX;
    Listener report = onScroll;
    if (!report.IsValid() && own) {
        offset = own->x;
        report = ListenTo(scroll, &AttachmentGroupScroll::OnScroll);
    }
    // The row's scroll box, named so last frame's extent can be read back.
    int scrollId = (int)IdFoldName(cx->path, id);

    El* row = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->PathId(id)
                  ->W(kFill)
                  ->MinW(0)
                  ->Gap(Rems(cx, 0.75f))
                  ->PadY(Rems(cx, 0.25f))
                  ->ClipX()
                  ->ScrollX(offset)
                  ->ScrollId(scrollId)
                  // lock_scroll_axis: the row takes a horizontal-dominant
                  // wheel gesture and lets a vertical one reach the scroller
                  // around it.
                  ->ScrollMask(Axis::Horizontal);
    if (report.IsValid()) {
        row->OnScroll(report);
    }
    if (styleSet) {
        row->Refine(style, styleSet);
    }
    for (int i = 0; i < children.len; i++) {
        row->Child(children[i]);
    }
    if (!hasEdgeFade) {
        return row;
    }

    // The fades read the offset across frames, and the first layout must be
    // followed by one more render before the scroll extent is known.
    if (own && !own->primed) {
        own->primed = true;
        WindowRequestAnimationFrame(cx->win);
    }
    const ScrollRect* last = WindowLastScrollRect(cx->win, scrollId);
    float max = last ? last->contentW - last->bounds.w : 0.f;
    bool scrollable = max > 1.f;
    // The offset grows as the row scrolls right, where Rust's goes negative.
    bool hidesLeading = scrollable && offset > 1.f;
    bool hidesTrailing = scrollable && offset < max - 1.f;
    motion::Transition transition =
        motion::Transition::New(kEdgeFadeTransitionMs);
    float leading =
        motion::transition(cx, motion::TransitionId(id, StrL("leading-fade")),
                           hidesLeading ? 1.f : 0.f, transition);
    float trailing =
        motion::transition(cx, motion::TransitionId(id, StrL("trailing-fade")),
                           hidesTrailing ? 1.f : 0.f, transition);

    // Gradient angle: 0 points up and increases clockwise, so 90 runs from
    // the leading edge to the trailing edge.
    auto fade = [&](float opacity, bool isLeading) {
        Rgba clear = RgbaOpacity(edgeFade, 0.f);
        Background bg;
        bg.gradient = true;
        bg.angle = 90.f;
        bg.from = ColorStop{isLeading ? edgeFade : clear, 0.f};
        bg.to = ColorStop{isLeading ? clear : edgeFade, 1.f};
        El* e = Div(a)
                    ->Absolute()
                    ->Top(0)
                    ->Bottom(0)
                    ->W(Rems(cx, 1.5f))
                    ->Opacity(opacity)
                    ->Bg(bg);
        return isLeading ? e->Left(0) : e->Right(0);
    };
    El* frame = Div(a)->W(kFill)->MinW(0)->Child(row);
    if (leading > 0.f) {
        frame->Child(fade(leading, true));
    }
    if (trailing > 0.f) {
        frame->Child(fade(trailing, false));
    }
    return frame;
}

} // namespace component
} // namespace gpui
