#ifndef GPUI_SRC_UI_ATTACHMENT_H_
#define GPUI_SRC_UI_ATTACHMENT_H_
/* Themed attachment card — crates/ui/src/attachment.rs */

#include "ui/sizing.h"
#include "ui/shimmer.h"

namespace gpui {

namespace component {

// The lifecycle status of an attachment.
enum class AttachmentStatus : uint8_t {
    // Selected and waiting to be uploaded.
    Pending,
    // Currently being uploaded.
    Uploading,
    // Uploaded and being processed.
    Processing,
    // Failed to upload or process.
    Failed,
    // Ready.
    Complete
};

inline bool AttachmentStatusIsPending(AttachmentStatus s) {
    return s == AttachmentStatus::Pending;
}
inline bool AttachmentStatusIsUploading(AttachmentStatus s) {
    return s == AttachmentStatus::Uploading;
}
inline bool AttachmentStatusIsProcessing(AttachmentStatus s) {
    return s == AttachmentStatus::Processing;
}
inline bool AttachmentStatusIsFailed(AttachmentStatus s) {
    return s == AttachmentStatus::Failed;
}
inline bool AttachmentStatusIsComplete(AttachmentStatus s) {
    return s == AttachmentStatus::Complete;
}
inline bool AttachmentStatusIsInProgress(AttachmentStatus s) {
    return s == AttachmentStatus::Uploading ||
           s == AttachmentStatus::Processing;
}

// SlotLayout: what the root hands its slots at layout time.
struct AttachmentSlotLayout {
    UiSize size = UiSize::Medium;
    AttachmentStatus status = AttachmentStatus::Complete;
    Axis axis = Axis::Horizontal;
    // Whether the media fills a vertical card flush with its border.
    bool flush = false;
    // The retry control, only while failed and identified.
    Listener retry;
    // Upload progress in percent, only while uploading; -1 is none.
    float progress = -1;
    // The attachment's identity, keying the controls' and the progress
    // ring's state.
    Str id = {};
    bool hasId = false;
};

// The media slot for an attachment. Add an icon or another element as a child
// for an icon-style preview; use Src when the attachment has an image.
struct AttachmentMedia {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    ArenaVec<El*> overlays;
    UiSize size = UiSize::Medium;
    bool hasSize = false;
    AttachmentStatus status = AttachmentStatus::Complete;
    Axis axis = Axis::Horizontal;
    bool flush = false;
    Listener retry;
    float progress = -1;
    Str id = {};
    bool hasId = false;
    Str source = {};
    bool hasSource = false;
    Style style = {};
    uint32_t styleSet = 0;

    static AttachmentMedia* New(Ctx* cx);
    AttachmentMedia* Src(Str source);
    // Centered content above the preview and above the status treatment.
    AttachmentMedia* Overlay(El* overlay);
    AttachmentMedia* Child(El* e);
    AttachmentMedia* WithSize(UiSize value);
    AttachmentMedia* Refine(const Style& s, uint32_t fields);
    // Fills the slot in from the attachment that owns it.
    AttachmentMedia* Layout(const AttachmentSlotLayout& layout);
    El* IntoEl();
};

// A single-line attachment title.
struct AttachmentTitle {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str text = {};
    AttachmentStatus status = AttachmentStatus::Complete;
    bool hasStatus = false;
    ShimmerStyle shimmerStyle = {};
    bool hasShimmerStyle = false;
    Style style = {};
    uint32_t styleSet = 0;

    static AttachmentTitle* New(Ctx* cx, Str text);
    AttachmentTitle* Status(AttachmentStatus value);
    AttachmentTitle* WithShimmerStyle(const ShimmerStyle& value);
    AttachmentTitle* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// A single-line attachment description or status message.
struct AttachmentDescription {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str text = {};
    AttachmentStatus status = AttachmentStatus::Complete;
    bool hasStatus = false;
    // The card size whose type scale the description follows.
    UiSize size = UiSize::Medium;
    bool hasSize = false;
    Style style = {};
    uint32_t styleSet = 0;

    static AttachmentDescription* New(Ctx* cx, Str text);
    AttachmentDescription* Status(AttachmentStatus value);
    AttachmentDescription* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// One entry of an AttachmentContent: a typed title or description, which
// inherits the card's lifecycle status, or an arbitrary element.
struct AttachmentContentChild {
    AttachmentTitle* title = nullptr;
    AttachmentDescription* description = nullptr;
    El* element = nullptr;
};

// The metadata slot for an attachment.
struct AttachmentContent {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<AttachmentContentChild> children;
    bool verticalLayout = false;
    AttachmentStatus status = AttachmentStatus::Complete;
    // The retry link that follows the first typed description while failed,
    // keyed on the attachment's id.
    Listener retry;
    Str retryId = {};
    // The percentage a typed description gains while uploading; -1 is none.
    float progress = -1;
    Style style = {};
    uint32_t styleSet = 0;

    static AttachmentContent* New(Ctx* cx);
    AttachmentContent* Title(AttachmentTitle* value);
    AttachmentContent* Description(AttachmentDescription* value);
    AttachmentContent* Child(El* e);
    AttachmentContent* Refine(const Style& s, uint32_t fields);
    AttachmentContent* Layout(const AttachmentSlotLayout& layout);
    El* IntoEl();
};

// A composition slot for attachment actions. Add existing Button or other
// controls as children; a separate attachment-specific action wrapper is
// intentionally unnecessary.
struct AttachmentActions {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    bool verticalLayout = false;
    Style style = {};
    uint32_t styleSet = 0;

    static AttachmentActions* New(Ctx* cx);
    AttachmentActions* Child(El* e);
    AttachmentActions* Refine(const Style& s, uint32_t fields);
    AttachmentActions* LayoutForAxis(Axis axis);
    El* IntoEl();
};

// A file or image attachment composed from media, content and action slots.
struct Attachment {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    bool hasId = false;
    Style style = {};
    uint32_t styleSet = 0;
    AttachmentStatus status = AttachmentStatus::Complete;
    UiSize size = UiSize::Medium;
    Axis axis = Axis::Horizontal;
    AttachmentMedia* media = nullptr;
    AttachmentContent* content = nullptr;
    AttachmentActions* actions = nullptr;
    Listener onClick;
    Listener onRemove;
    Listener onRetry;
    // progress(..), clamped to 0..100; -1 is none.
    float progress = -1;
    Str tooltip = {};
    bool hasTooltip = false;

    static Attachment* New(Ctx* cx);
    // A stable identity for the built-in controls: the whole-card click
    // layer, the remove control and the retry control.
    Attachment* Id(Str value);
    // Make the whole card clickable. The click layer is painted below the
    // actions slot, so action buttons stay independently clickable; click
    // state needs a stable identity, so this takes effect only with Id.
    Attachment* OnClick(Listener handler);
    // A remove control riding the card's upper trailing corner: on hover on
    // desktop, always on touch. Takes effect only together with Id.
    Attachment* OnRemove(Listener handler);
    // A retry control while Failed: a round button over an image preview,
    // or a link after the typed description. Only together with Id.
    Attachment* OnRetry(Listener handler);
    // Upload progress in percent. While Uploading the media shows a progress
    // ring instead of the spinner, a horizontal card a thin bar along its
    // bottom edge, and a typed description the percentage.
    Attachment* Progress(float percent);
    // A tooltip while the card is hovered, e.g. why an upload failed. Only
    // together with Id.
    Attachment* Tooltip(Str text);
    Attachment* Status(AttachmentStatus value);
    Attachment* WithAxis(Axis value);
    Attachment* Media(AttachmentMedia* value);
    Attachment* Content(AttachmentContent* value);
    Attachment* Actions(AttachmentActions* value);
    Attachment* WithSize(UiSize value);
    Attachment* Refine(const Style& s, uint32_t fields);
    // retry_control: the retry control the slots may render — only while
    // failed, and only with an identity to key its state on. Invalid
    // otherwise.
    Listener RetryControl() const;
    // The pass that hands the card's size, axis and status to its slots.
    void LayoutSlots();
    El* IntoEl();
};

// A horizontally scrollable row of attachments.
//
// The group keeps its own scroll offset, keyed on its id, unless the caller
// tracks it: Rust's `track_scroll(&ScrollHandle)` is the offset and the
// OnScroll that reports it back, since an offset a caller moves is
// view-owned here.
struct AttachmentGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    ArenaVec<El*> children;
    float scrollX = 0;
    Listener onScroll;
    Rgba edgeFade = {};
    bool hasEdgeFade = false;
    Style style = {};
    uint32_t styleSet = 0;

    static AttachmentGroup* New(Ctx* cx, Str id);
    AttachmentGroup* Child(El* e);
    // track_scroll: scroll the row through the caller's offset, which
    // `onScroll` reports back.
    AttachmentGroup* TrackScroll(float value, Listener onScroll);
    AttachmentGroup* ScrollX(float value);
    AttachmentGroup* OnScroll(Listener fn);
    // with_edge_fade: fade an edge into `color` — the surface behind the row
    // — while attachments continue past it.
    AttachmentGroup* WithEdgeFade(Rgba color);
    AttachmentGroup* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// CardMetrics: the geometry a named size resolves to. Rust keeps these in
// rems so they follow the root font size; here they are the pixels those rems
// come to at the 16 px rem this tree lays out with.
struct AttachmentCardMetrics {
    // The height of a horizontal card and the side of a square image tile.
    float height = 0;
    // The fixed width of a horizontal card that carries content.
    float chipWidth = 0;
    // The side of the square media slot of a horizontal card.
    float media = 0;
    // The glyph size inside the media slot: an icon child without its own
    // size, and the status glyphs.
    float mediaGlyph = 0;
    float paddingStart = 0;
    float paddingEnd = 0;
    // The padding of a vertical card that carries content.
    float cardPadding = 0;
    float gap = 0;
    float text = 0;
    float description = 0;
};

AttachmentCardMetrics AttachmentMetrics(UiSize size);

// upload_bar_path: the upload bar as the intersection of its rectangle with
// the card's inner rounded rectangle, both ends sampled along the corner arcs.
// `w` x `h` is the card's padding box. Writes the polygon's points (at most
// kAttachmentUploadBarPoints) and answers how many, 0 for no bar.
const int kAttachmentUploadBarPoints = 15;
int AttachmentUploadBarPath(float w, float h, float percent, float radius,
                            Point* out);

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_ATTACHMENT_H_
