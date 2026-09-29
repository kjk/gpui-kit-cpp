/* Ported from the tests in crates/ui/src/attachment.rs — the builder tests,
 * plus the structure the `click_dispatch` module checks.
 *
 * That last one is a `#[gpui::test]` driving a window and simulating two
 * clicks. There is no TestAppContext here, so what it asserts is checked
 * structurally instead: the whole-card click layer is built *before* the
 * actions slot, so the actions' hitboxes are on top of it, and the actions
 * cluster stops the press from reaching the layer below. Those two facts are
 * exactly what makes the Rust assertions come out the way they do. */

#include "Test.h"

using namespace gpui::component;

static void TheBuilderCarriesStatusAxisSizeAndSlots() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Attachment* attachment =
        Attachment::New(&cx)
            ->Status(AttachmentStatus::Uploading)
            ->WithAxis(Axis::Vertical)
            ->WithSize(UiSize::Small)
            ->Media(AttachmentMedia::New(&cx)->Src(StrL("preview.png")))
            ->Content(AttachmentContent::New(&cx)
                          ->Title(AttachmentTitle::New(&cx, StrL("report.pdf")))
                          ->Description(AttachmentDescription::New(
                              &cx, StrL("Uploading"))))
            ->Actions(AttachmentActions::New(&cx)
                          ->Child(TextEl(a, StrL("Cancel"))));

    utassert(attachment->status == AttachmentStatus::Uploading);
    utassert(attachment->axis == Axis::Vertical);
    utassert(attachment->size == UiSize::Small);
    utassert(attachment->media != nullptr);
    utassert(attachment->content != nullptr);
    utassert(attachment->actions != nullptr);

    attachment->LayoutSlots();
    utassert(attachment->media->hasSize);
    utassert(attachment->media->size == UiSize::Small);
    utassert(attachment->media->status == AttachmentStatus::Uploading);
    utassert(attachment->content->verticalLayout);
    utassert(attachment->actions->verticalLayout);

    Listener onScroll;
    onScroll.SetFn(&TheBuilderCarriesStatusAxisSizeAndSlots);
    AttachmentGroup* group = AttachmentGroup::New(&cx, StrL("group"))
                                 ->TrackScroll(12, onScroll)
                                 ->WithEdgeFade(Rgb(0, 0, 0))
                                 ->Child(TextEl(a, StrL("first")))
                                 ->Child(TextEl(a, StrL("second")));
    utassert(group->onScroll.IsValid() && group->scrollX == 12.f);
    utassert(group->hasEdgeFade && RgbaEq(group->edgeFade, Rgb(0, 0, 0)));
    utassert(group->children.len == 2);
    AttachmentGroup* plain = AttachmentGroup::New(&cx, StrL("plain"));
    utassert(!plain->onScroll.IsValid() && !plain->hasEdgeFade);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void TheWholeCardClickNeedsBothAnIdAndAHandler() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    utassert(!Attachment::New(&cx)->hasId);
    utassert(!Attachment::New(&cx)->onClick.IsValid());

    Listener handler;
    handler.SetFn(&TheWholeCardClickNeedsBothAnIdAndAHandler);
    Attachment* clickable =
        Attachment::New(&cx)->Id(StrL("report-attachment"))->OnClick(handler);
    utassert(clickable->hasId &&
             base::StrEq(clickable->id, StrL("report-attachment")));
    utassert(clickable->onClick.IsValid());

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void TheDefaultsAndTheStatusHelpers() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    utassert(Attachment::New(&cx)->status == AttachmentStatus::Complete);
    utassert(AttachmentStatusIsPending(AttachmentStatus::Pending));
    utassert(AttachmentStatusIsInProgress(AttachmentStatus::Uploading));
    utassert(AttachmentStatusIsProcessing(AttachmentStatus::Processing));
    utassert(AttachmentStatusIsFailed(AttachmentStatus::Failed));
    utassert(AttachmentStatusIsComplete(AttachmentStatus::Complete));
    utassert(!AttachmentStatusIsInProgress(AttachmentStatus::Complete));

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void TheSlotsAreComposable() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    AttachmentMedia* media = AttachmentMedia::New(&cx)
                                 ->Child(TextEl(a, StrL("icon")));
    utassert(media->children.len == 1);

    AttachmentContent* content =
        AttachmentContent::New(&cx)
            ->Title(AttachmentTitle::New(&cx, StrL("name")))
            ->Description(AttachmentDescription::New(&cx, StrL("Details")))
            ->Child(TextEl(a, StrL("Custom progress")));
    utassert(content->children.len == 3);
    utassert(content->children[0].title != nullptr);
    utassert(content->children[1].description != nullptr);
    utassert(content->children[2].element != nullptr);

    // A title added through the plain child slot stays an ordinary element
    // and no longer inherits the card's status.
    AttachmentContent* legacy =
        AttachmentContent::New(&cx)
            ->Child(AttachmentTitle::New(&cx, StrL("legacy"))->IntoEl());
    utassert(legacy->children[0].element != nullptr);
    utassert(legacy->children[0].title == nullptr);

    AttachmentActions* actions = AttachmentActions::New(&cx)
                                     ->Child(TextEl(a, StrL("remove")));
    utassert(actions->children.len == 1);

    AttachmentGroup* group = AttachmentGroup::New(&cx, StrL("attachments"))
                                 ->Child(TextEl(a, StrL("First")))
                                 ->Child(TextEl(a, StrL("Second")));
    utassert(group->children.len == 2);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void TypedContentInheritsTheCardStatus() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Attachment* attachment =
        Attachment::New(&cx)
            ->Status(AttachmentStatus::Uploading)
            ->Content(AttachmentContent::New(&cx)
                          ->Title(AttachmentTitle::New(&cx, StrL("report.pdf")))
                          ->Description(AttachmentDescription::New(
                              &cx, StrL("Uploading"))));
    attachment->LayoutSlots();
    AttachmentContent* content = attachment->content;
    utassert(content->children[0].title->hasStatus);
    utassert(content->children[0].title->status == AttachmentStatus::Uploading);
    utassert(content->children[1].description->hasStatus);
    utassert(content->children[1]
                 .description->status == AttachmentStatus::Uploading);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void AnExplicitChildStatusOverridesTheCard() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Attachment* attachment =
        Attachment::New(&cx)
            ->Status(AttachmentStatus::Failed)
            ->Content(
                AttachmentContent::New(&cx)
                    ->Title(AttachmentTitle::New(&cx, StrL("report.pdf"))
                                ->Status(AttachmentStatus::Processing))
                    ->Description(AttachmentDescription::New(
                                      &cx, StrL("Previous upload completed"))
                                      ->Status(AttachmentStatus::Complete)));
    attachment->LayoutSlots();
    utassert(attachment->content->children[0]
                 .title->status == AttachmentStatus::Processing);
    utassert(attachment->content->children[1]
                 .description->status == AttachmentStatus::Complete);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void ATitleKeepsItsCustomShimmerStyle() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Attachment* attachment =
        Attachment::New(&cx)
            ->Status(AttachmentStatus::Processing)
            ->Content(AttachmentContent::New(&cx)->Title(
                AttachmentTitle::New(&cx, StrL("report.pdf"))
                    ->WithShimmerStyle(
                        ShimmerStyle::New().Spread(0.45f).Reverse(true))));
    attachment->LayoutSlots();
    AttachmentTitle* title = attachment->content->children[0].title;
    utassert(title->status == AttachmentStatus::Processing);
    utassert(title->hasShimmerStyle);
    utassert(title->shimmerStyle.spread == ShimmerSpread::Relative(0.45f));
    utassert(title->shimmerStyle.reverse);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void AMediaPreviewKeepsItsChildrenAndOverlays() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    AttachmentMedia* media = AttachmentMedia::New(&cx)
                                 ->Src(StrL("preview.png"))
                                 ->Child(TextEl(a, StrL("Existing overlay")))
                                 ->Overlay(TextEl(a, StrL("Centered overlay")));
    utassert(media->hasSource);
    utassert(media->children.len == 1);
    utassert(media->overlays.len == 1);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void MediaInheritsTheCardSizeUnlessItNamesOne() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    AttachmentSlotLayout tile;
    tile.size = UiSize::Small;
    tile.axis = Axis::Vertical;
    tile.flush = true;
    AttachmentMedia* inherited = AttachmentMedia::New(&cx)->Layout(tile);
    utassert(inherited->hasSize && inherited->size == UiSize::Small);
    utassert(inherited->axis == Axis::Vertical);
    utassert(inherited->flush);

    AttachmentSlotLayout failed;
    failed.size = UiSize::Large;
    failed.status = AttachmentStatus::Failed;
    AttachmentMedia* explicitSize =
        AttachmentMedia::New(&cx)->WithSize(UiSize::XSmall)->Layout(failed);
    utassert(explicitSize->size == UiSize::XSmall);
    utassert(explicitSize->status == AttachmentStatus::Failed);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// The stacking `click_dispatch::whole_card_click_stays_below_the_actions`
// verifies with two simulated clicks.
static void TheClickLayerSitsBelowTheActions() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Listener handler;
    handler.SetFn(&TheClickLayerSitsBelowTheActions);
    El* card = Attachment::New(&cx)
                   ->Id(StrL("attachment"))
                   ->OnClick(handler)
                   ->Actions(AttachmentActions::New(&cx)
                                 ->Child(TextEl(a, StrL("Open"))))
                   ->IntoEl();

    // [click layer, actions]: the layer is added first, so the actions paint
    // over it and take the press.
    El* layer = card->first;
    utassert(layer != nullptr);
    utassert(layer->style.absolute);
    utassert(layer->clickFromPath);
    El* actions = layer->next;
    utassert(actions != nullptr);
    utassert(actions->stopMouseDown);
    utassert(actions->next == nullptr);

    // Without a handler there is no layer at all, and the card does not light
    // under the pointer.
    El* plain = Attachment::New(&cx)
                    ->Actions(AttachmentActions::New(&cx)
                                  ->Child(TextEl(a, StrL("Open"))))
                    ->IntoEl();
    utassert(plain->first && plain->first->stopMouseDown);
    utassert(!plain->style.hasHoverBg);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// test_attachment_corner_controls_builder,
// test_attachment_retry_control_reaches_slots_only_while_failed,
// test_attachment_progress_and_tooltip_builder and
// test_attachment_image_tile_media_is_flush.
static void TheCornerControlsProgressAndTooltip() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;
    Listener handler;
    handler.SetFn(&TheCornerControlsProgressAndTooltip);

    utassert(!Attachment::New(&cx)->onRemove.IsValid());
    utassert(!Attachment::New(&cx)->onRetry.IsValid());
    Attachment* controls = Attachment::New(&cx)
                               ->Id(StrL("upload"))
                               ->OnRemove(handler)
                               ->OnRetry(handler);
    utassert(controls->onRemove.IsValid() && controls->onRetry.IsValid());

    Attachment* failed =
        Attachment::New(&cx)
            ->Id(StrL("upload"))
            ->Status(AttachmentStatus::Failed)
            ->OnRetry(handler)
            ->Media(AttachmentMedia::New(&cx)->Src(StrL("preview.png")))
            ->Content(AttachmentContent::New(&cx)->Description(
                AttachmentDescription::New(&cx, StrL("Failed"))));
    failed->LayoutSlots();
    utassert(failed->media->retry.IsValid());
    utassert(failed->content->retry.IsValid());
    // A completed attachment offers no retry, and one without an identity
    // has nothing to key the control on.
    Attachment* complete =
        Attachment::New(&cx)
            ->Id(StrL("upload"))
            ->OnRetry(handler)
            ->Media(AttachmentMedia::New(&cx)->Src(StrL("preview.png")));
    complete->LayoutSlots();
    utassert(!complete->media->retry.IsValid());
    Attachment* anonymous =
        Attachment::New(&cx)
            ->Status(AttachmentStatus::Failed)
            ->OnRetry(handler)
            ->Media(AttachmentMedia::New(&cx)->Src(StrL("preview.png")));
    anonymous->LayoutSlots();
    utassert(!anonymous->media->retry.IsValid());

    utassert(Attachment::New(&cx)->progress < 0);
    utassert(!Attachment::New(&cx)->hasTooltip);
    utassertnear(Attachment::New(&cx)->Progress(130)->progress, 100.f);
    utassertnear(Attachment::New(&cx)->Progress(-5)->progress, 0.f);
    Attachment* tip = Attachment::New(&cx)->Tooltip(StrL("Network error"));
    utassert(tip->hasTooltip && StrEq(tip->tooltip, StrL("Network error")));
    // Progress reaches the slots only while uploading.
    Attachment* uploading =
        Attachment::New(&cx)
            ->Id(StrL("upload"))
            ->Status(AttachmentStatus::Uploading)
            ->Progress(62)
            ->Media(AttachmentMedia::New(&cx)->Src(StrL("preview.png")))
            ->Content(AttachmentContent::New(&cx)->Description(
                AttachmentDescription::New(&cx, StrL("Uploading"))));
    uploading->LayoutSlots();
    utassertnear(uploading->media->progress, 62.f);
    utassertnear(uploading->content->progress, 62.f);
    Attachment* processing =
        Attachment::New(&cx)
            ->Status(AttachmentStatus::Processing)
            ->Progress(62)
            ->Media(AttachmentMedia::New(&cx)->Src(StrL("preview.png")));
    processing->LayoutSlots();
    utassert(processing->media->progress < 0);

    // A vertical card without content is an image tile the media fills.
    Attachment* tileCard =
        Attachment::New(&cx)
            ->WithAxis(Axis::Vertical)
            ->Media(AttachmentMedia::New(&cx)->Src(StrL("preview.png")));
    tileCard->LayoutSlots();
    utassert(tileCard->media->flush);
    Attachment* previewCard =
        Attachment::New(&cx)
            ->WithAxis(Axis::Vertical)
            ->Media(AttachmentMedia::New(&cx)->Src(StrL("preview.png")))
            ->Content(AttachmentContent::New(&cx)->Title(
                AttachmentTitle::New(&cx, StrL("preview.png"))));
    previewCard->LayoutSlots();
    utassert(!previewCard->media->flush);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// upload_bar_path: the bar hugs the inner corner arcs and stops at the
// percentage.
static void TheUploadBarHugsTheCorners() {
    Point points[kAttachmentUploadBarPoints];
    utassert(AttachmentUploadBarPath(200, 54, 0, 8, points) == 0);
    int n = AttachmentUploadBarPath(200, 54, 50, 8, points);
    utassert(n == kAttachmentUploadBarPoints);
    // Starts on the left arc, a little in from the edge at the top of the
    // bar, and reaches the edge only at the far end of the arc.
    utassertnear(points[0].y, 52.f);
    utassert(points[0].x > 0.f && points[0].x < 7.f);
    // Half the width, well before the right arc.
    utassertnear(points[1].x, 100.f);
    for (int i = 0; i < n; i++) {
        utassert(points[i].y >= 52.f - 0.001f && points[i].y <= 54.001f);
        utassert(points[i].x >= 0.f && points[i].x <= 100.001f);
    }
}

// card_metrics, and the geometry of the two axes.
static void TheSizeScaleAndTheTwoAxes() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    El* medium =
        Attachment::New(&cx)
            ->Content(AttachmentContent::New(&cx)
                          ->Title(AttachmentTitle::New(&cx, StrL("a"))))
            ->IntoEl();
    // A chip: 56 tall, 232 wide once it carries content, a 13 px title.
    utassertnear(medium->style.gapX, 10.f);
    utassertnear(medium->style.fontSize, 13.f);
    utassertnear(medium->style.height, 56.f);
    utassertnear(medium->style.width, 232.f);
    utassertnear(medium->style.pad.left, 8.f);
    utassertnear(medium->style.pad.right, 12.f);

    El* withMedia =
        Attachment::New(&cx)
            ->WithSize(UiSize::Large)
            ->Media(AttachmentMedia::New(&cx))
            ->Content(AttachmentContent::New(&cx)
                          ->Title(AttachmentTitle::New(&cx, StrL("a"))))
            ->IntoEl();
    utassertnear(withMedia->style.gapX, 12.f);
    utassertnear(withMedia->style.fontSize, 14.f);
    utassertnear(withMedia->style.height, 64.f);
    utassertnear(withMedia->style.width, 272.f);
    utassertnear(withMedia->style.pad.left, 10.f);
    utassertnear(withMedia->style.pad.right, 16.f);

    // Vertical with metadata is rems(7.5) wide; without it, a square tile
    // as tall as the chip.
    El* vertical =
        Attachment::New(&cx)
            ->WithAxis(Axis::Vertical)
            ->Content(AttachmentContent::New(&cx)
                          ->Title(AttachmentTitle::New(&cx, StrL("a"))))
            ->IntoEl();
    utassertnear(vertical->style.width, 120.f);
    utassert(vertical->style.dir == FlexDir::Col);
    El* bare = Attachment::New(&cx)
                   ->WithAxis(Axis::Vertical)
                   ->Media(AttachmentMedia::New(&cx))
                   ->IntoEl();
    utassertnear(bare->style.width, 56.f);
    utassertnear(bare->style.height, 56.f);

    // A pending card is dashed; a failed one borders in destructive.
    utassert(Attachment::New(&cx)
                 ->Status(AttachmentStatus::Pending)
                 ->IntoEl()
                 ->style.borderDashed);
    const Theme& th = ThemeNow(&app);
    El* failed =
        Attachment::New(&cx)->Status(AttachmentStatus::Failed)->IntoEl();
    utassert(RgbaEq(failed->style.borderColor, th.danger));

    AppGlobalClear(&app);
    ArenaDelete(a);
}

void TestAttachment() {
    TestSuite("attachment");
    TheBuilderCarriesStatusAxisSizeAndSlots();
    TheWholeCardClickNeedsBothAnIdAndAHandler();
    TheDefaultsAndTheStatusHelpers();
    TheSlotsAreComposable();
    TypedContentInheritsTheCardStatus();
    AnExplicitChildStatusOverridesTheCard();
    ATitleKeepsItsCustomShimmerStyle();
    AMediaPreviewKeepsItsChildrenAndOverlays();
    MediaInheritsTheCardSizeUnlessItNamesOne();
    TheClickLayerSitsBelowTheActions();
    TheCornerControlsProgressAndTooltip();
    TheUploadBarHugsTheCorners();
    TheSizeScaleAndTheTwoAxes();
}
