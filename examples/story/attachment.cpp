#include "Story.h"

// crates/story/src/stories/attachment_story.rs

// section(..).max_w(rems(42.5)) — 680 at the 16px root.
static const float kAttachmentSectionMaxW = 680;

struct AttachmentStory {
    // on_remove(|_, _, _| {}): the story shows the control, not a list to
    // take the card out of.
    static void OnRemove(AttachmentStory*, Ctx*, const ClickEvent*) {}
    static void OnRetryPhoto(AttachmentStory*, Ctx* cx, const ClickEvent*) {
        StoryPushNotification(cx, StrL("Retrying photo.png…"));
    }
    static void OnRetryStatement(AttachmentStory*, Ctx* cx, const ClickEvent*) {
        StoryPushNotification(cx, StrL("Retrying Q3 statement.pdf…"));
    }
    static void OnOpeningPreview(AttachmentStory*, Ctx* cx, const ClickEvent*) {
        StoryPushNotification(cx, StrL("Opening sdk-preview.svg…"));
    }
    static void OnOpenedPreview(AttachmentStory*, Ctx* cx, const ClickEvent*) {
        StoryPushNotification(cx, StrL("Opened sdk-preview.svg"));
    }

    static El* Render(AttachmentStory* self, Ctx* cx);
};

static El* AttachmentSection(Ctx* cx, const char* title, const char* desc,
                             float gap) {
    El* section = StorySection(cx, title, desc);
    StorySectionBody(section)->FlexCol()->Gap(gap)->MaxW(
        kAttachmentSectionMaxW);
    return section;
}

static component::AttachmentMedia* FileMedia(Ctx* cx) {
    return component::AttachmentMedia::New(cx)
        ->Child(component::Icon::New(cx, IconName::FileText)->IntoEl());
}

static component::AttachmentContent* TitleAndDescription(Ctx* cx,
                                                         const char* title,
                                                         const char* desc) {
    return component::AttachmentContent::New(cx)
        ->Title(component::AttachmentTitle::New(cx, Str(title)))
        ->Description(component::AttachmentDescription::New(cx, Str(desc)));
}

// The SVG preview the image tiles and the preview card load.
static Str AttachmentPreviewSrc() {
    return StrL("https://pub.lbkrs.com/files/202503/vEnnmgUM6bo362ya/sdk.svg");
}

// image_tile: a vertical attachment with only a preview, and a remove control.
static component::Attachment* ImageTile(Ctx* cx, const char* id,
                                        component::AttachmentStatus status) {
    return component::Attachment::New(cx)
        ->Id(Str(id))
        ->WithAxis(Axis::Vertical)
        ->Status(status)
        ->OnRemove(Listen(cx, &AttachmentStory::OnRemove))
        ->Media(component::AttachmentMedia::New(cx)
                    ->Src(AttachmentPreviewSrc()));
}

// file_chip: a file attachment with a title, a description and a remove
// control.
static component::Attachment* FileChip(Ctx* cx, const char* id,
                                       component::AttachmentStatus status,
                                       const char* name,
                                       const char* description) {
    return component::Attachment::New(cx)
        ->Id(Str(id))
        ->Status(status)
        ->OnRemove(Listen(cx, &AttachmentStory::OnRemove))
        ->Media(FileMedia(cx))
        ->Content(TitleAndDescription(cx, name, description));
}

El* AttachmentStory::Render(AttachmentStory*, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    using component::AttachmentStatus;
    El* page = Div(a)->FlexCol()->W(kFill)->Gap(16);

    El* composer = AttachmentSection(
        cx, "Composer",
        "Image tiles and file chips in a scrolling row, with the built-in "
        "remove, retry, progress and tooltip controls. Hover a card for its "
        "remove control.",
        0);
    StorySectionAdd(
        composer,
        component::AttachmentGroup::New(cx, StrL("attachment-story-composer"))
            ->WithEdgeFade(th.background)
            ->Child(ImageTile(cx, "composer-image", AttachmentStatus::Complete)
                        ->IntoEl())
            ->Child(ImageTile(cx, "composer-image-uploading",
                              AttachmentStatus::Uploading)
                        ->Progress(62)
                        ->IntoEl())
            ->Child(
                ImageTile(cx, "composer-image-failed", AttachmentStatus::Failed)
                    ->Tooltip(StrL("Network error · Click to retry"))
                    ->OnRetry(Listen(cx, &AttachmentStory::OnRetryPhoto))
                    ->IntoEl())
            ->Child(ImageTile(cx, "composer-image-rejected",
                              AttachmentStatus::Failed)
                        ->Tooltip(StrL("Image exceeds 20 MB limit · Remove to "
                                       "send"))
                        ->IntoEl())
            ->Child(FileChip(cx, "composer-file", AttachmentStatus::Complete,
                             "Q3 statement.pdf", "PDF · 1.2 MB")
                        ->IntoEl())
            ->Child(FileChip(cx, "composer-file-uploading",
                             AttachmentStatus::Uploading, "Q3 statement.pdf",
                             "Uploading")
                        ->Progress(62)
                        ->IntoEl())
            ->Child(
                FileChip(cx, "composer-file-failed", AttachmentStatus::Failed,
                         "Q3 statement.pdf", "Upload failed")
                    ->Tooltip(StrL("Network error · Click to retry"))
                    ->OnRetry(Listen(cx, &AttachmentStory::OnRetryStatement))
                    ->IntoEl())
            ->Child(FileChip(cx, "composer-file-rejected",
                             AttachmentStatus::Failed,
                             "accessibility-review-and-keyboard-navigation-"
                             "findings.xlsx",
                             "Exceeds 20 MB limit")
                        ->Tooltip(StrL("Max file size is 20 MB · Remove to "
                                       "send"))
                        ->IntoEl())
            ->IntoEl());
    page->Child(composer);

    El* lifecycle = AttachmentSection(
        cx, "Lifecycle",
        "The states the composer row does not show: pending, an upload "
        "without a known percentage, processing with a custom shimmer, and a "
        "completed file.",
        12);
    StorySectionAdd(lifecycle,
                    FileChip(cx, "lifecycle-pending", AttachmentStatus::Pending,
                             "meeting-notes.pdf", "Ready to upload")
                        ->IntoEl());
    StorySectionAdd(lifecycle, FileChip(cx, "lifecycle-uploading",
                                        AttachmentStatus::Uploading,
                                        "design-assets.zip", "Uploading")
                                   ->IntoEl());
    StorySectionAdd(
        lifecycle,
        component::Attachment::New(cx)
            ->Id(StrL("lifecycle-processing"))
            ->Status(AttachmentStatus::Processing)
            ->Media(FileMedia(cx))
            ->Content(component::AttachmentContent::New(cx)
                          ->Title(component::AttachmentTitle::New(
                                      cx, StrL("transcript.pdf"))
                                      ->WithShimmerStyle(
                                          component::ShimmerStyle::New()
                                              .HighlightColor(th.primary)
                                              .Spread(0.45f)
                                              .Reverse(true)))
                          ->Description(component::AttachmentDescription::New(
                              cx, StrL("Processing document"))))
            ->IntoEl());
    Style successMedia = {};
    successMedia.color = th.success;
    StorySectionAdd(
        lifecycle,
        component::Attachment::New(cx)
            ->Id(StrL("lifecycle-complete"))
            ->Media(component::AttachmentMedia::New(cx)
                        ->Refine(successMedia, StyleFieldColor)
                        ->Child(component::Icon::New(cx, IconName::CircleCheck)
                                    ->IntoEl()))
            ->Content(TitleAndDescription(cx, "published-report.pdf",
                                          "Uploaded · 1.8 MB"))
            ->IntoEl());
    page->Child(lifecycle);

    El* preview = AttachmentSection(
        cx, "Preview card",
        "A vertical card puts the preview above the metadata; actions sit over "
        "its upper trailing corner and stay clickable above the whole-card "
        "click.",
        0);
    StorySectionAdd(
        preview,
        component::Attachment::New(cx)
            ->Id(StrL("preview-card"))
            ->WithAxis(Axis::Vertical)
            ->OnClick(Listen(cx, &AttachmentStory::OnOpeningPreview))
            ->Media(component::AttachmentMedia::New(cx)
                        ->Src(AttachmentPreviewSrc()))
            ->Content(
                TitleAndDescription(cx, "sdk-preview.svg", "SVG · 1280 × 720"))
            ->Actions(component::AttachmentActions::New(cx)->Child(
                component::Button::New(cx, StrL("preview-card-open"))
                    ->Ghost()
                    ->WithSize(UiSize::XSmall)
                    ->Label(StrL("Open"))
                    ->OnClick(Listen(cx, &AttachmentStory::OnOpenedPreview))
                    ->IntoEl()))
            ->IntoEl());
    page->Child(preview);

    El* sizes = AttachmentSection(
        cx, "Sizes",
        "One geometry scale per named size: card, media box and type move "
        "together.",
        12);
    StorySectionAdd(sizes,
                    FileChip(cx, "size-large", AttachmentStatus::Complete,
                             "large.pdf", "Large · PDF · 3.1 MB")
                        ->WithSize(UiSize::Large)
                        ->IntoEl());
    StorySectionAdd(sizes,
                    FileChip(cx, "size-medium", AttachmentStatus::Complete,
                             "medium.pdf", "Medium · PDF · 2.4 MB")
                        ->IntoEl());
    StorySectionAdd(sizes,
                    FileChip(cx, "size-small", AttachmentStatus::Complete,
                             "small.csv", "Small · CSV · 840 KB")
                        ->WithSize(UiSize::Small)
                        ->IntoEl());
    StorySectionAdd(sizes,
                    FileChip(cx, "size-xsmall", AttachmentStatus::Complete,
                             "xsmall.txt", "XSmall · TXT · 4 KB")
                        ->WithSize(UiSize::XSmall)
                        ->IntoEl());
    page->Child(sizes);

    El* custom = AttachmentSection(
        cx, "Custom style",
        "Every public part takes style refinements, and an overlay is painted "
        "above the preview and any status scrim.",
        12);
    Style customMedia = {};
    customMedia.radius = th.radius;
    customMedia.bg = Background(RgbaOpacity(th.primary, 0.12f));
    customMedia.color = th.primary;
    Style customTitle = {};
    customTitle.color = th.primary;
    StorySectionAdd(
        custom,
        component::Attachment::New(cx)
            ->Media(component::AttachmentMedia::New(cx)
                        ->Refine(customMedia, StyleFieldRadius | StyleFieldBg |
                                                  StyleFieldColor)
                        ->Child(component::Icon::New(cx, IconName::FileText)
                                    ->IntoEl()))
            ->Content(component::AttachmentContent::New(cx)
                          ->Title(component::AttachmentTitle::New(
                                      cx, StrL("custom-theme.json"))
                                      ->Refine(customTitle, StyleFieldColor))
                          ->Description(component::AttachmentDescription::New(
                              cx, StrL("JSON · 16 KB"))))
            ->IntoEl()
            ->W(kFill)
            ->Radius(th.radius)
            ->Bg(th.tokens.accent)
            ->Border(1, RgbaOpacity(th.accent, 0.5f)));
    StorySectionAdd(
        custom,
        component::Attachment::New(cx)
            ->WithAxis(Axis::Vertical)
            ->Media(component::AttachmentMedia::New(cx)
                        ->Src(AttachmentPreviewSrc())
                        ->Overlay(component::Icon::New(cx, IconName::Play)
                                      ->IntoEl()
                                      ->Fg(th.foreground)))
            ->IntoEl());
    page->Child(custom);
    return page;
}

STORY_PAGE(StoryAttachment, AttachmentStory);
