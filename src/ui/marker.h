#ifndef GPUI_SRC_UI_MARKER_H_
#define GPUI_SRC_UI_MARKER_H_
/* Themed conversation marker — crates/ui/src/marker.rs */

#include "ui/sizing.h"
#include "ui/shimmer.h"
#include "base/styled.h"

namespace gpui {

namespace component {

// The visual treatment used by a Marker.
enum class MarkerVariant : uint8_t {
    // An inline marker with no additional divider.
    Plain,
    // A centered marker with semantic divider lines on both sides.
    Separator,
    // A marker with a semantic bottom border.
    Border
};

// The visual treatment used while a Marker is loading.
enum class MarkerLoadingStyle : uint8_t {
    // A compact rotating spinner beside the marker content.
    Spinner,
    // A highlight swept across marker content, with no icon added.
    Shimmer
};

// Horizontal placement of a Marker's children inside its full-width row.
enum class MarkerAlignment : uint8_t {
    // Keep the children at the leading edge.
    Start,
    // Center the children, like a system notice in a transcript.
    Center,
    // Keep the children at the trailing edge.
    End
};

// A compact decorative icon slot inside a Marker.
struct MarkerIcon {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static MarkerIcon* New(Ctx* cx);
    MarkerIcon* Child(El* e);
    MarkerIcon* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// One entry of a MarkerContent: either text that can take the loading
// shimmer, or an arbitrary element.
struct MarkerContentChild {
    Str text = {};
    El* element = nullptr;
    bool isText = false;
};

// The independently styleable text or rich-content slot in a Marker.
struct MarkerContent {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<MarkerContentChild> children;
    // Filled in by the Marker that owns this slot.
    bool shimmer = false;
    ShimmerStyle shimmerStyle = {};
    bool separator = false;
    MarkerAlignment alignment = MarkerAlignment::Start;
    // The colour the shimmer composites over, which the row hands down.
    Rgba fg = {};
    bool hasFg = false;
    Style style = {};
    uint32_t styleSet = 0;

    static MarkerContent* New(Ctx* cx);
    // Text that can receive a continuous loading shimmer.
    MarkerContent* Text(Str text);
    MarkerContent* Child(El* e);
    MarkerContent* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

// One entry of a Marker: a configured icon slot, a configured content slot,
// or an arbitrary element, in the order the caller added them.
struct MarkerChild {
    MarkerIcon* icon = nullptr;
    MarkerContent* content = nullptr;
    El* element = nullptr;
};

// A compact, composable row for conversation status and system markers.
//
// Marker accepts arbitrary children rather than fixed icon and content slots.
// Loading effects only affect configured content slots, so icons and
// separators keep their appearance.
struct Marker {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    bool hasId = false;
    Style style = {};
    uint32_t styleSet = 0;
    Style separatorStyle = {};
    uint32_t separatorStyleSet = 0;
    MarkerVariant variant = MarkerVariant::Plain;
    // Option<MarkerAlignment>: unset until Alignment is called.
    MarkerAlignment alignment = MarkerAlignment::Start;
    bool hasAlignment = false;
    bool loading = false;
    MarkerLoadingStyle loadingStyle = MarkerLoadingStyle::Spinner;
    ShimmerStyle shimmerStyle = {};
    RoleOverride role = {};
    ArenaVec<MarkerChild> children;

    static Marker* New(Ctx* cx);
    // A stable identity, so the marker can appear in the accessibility tree.
    Marker* Id(Str value);
    // The role announced for this marker. A marker is presentational by
    // default; an accessibility node needs a stable identity, so the role
    // takes effect only together with Id.
    Marker* Role(RoleOverride value);
    Marker* WithVariant(MarkerVariant value);
    // Set where the row places its children.
    //
    // Unset, a Separator centers its label between the two lines and every
    // other variant starts at the leading edge. An explicit alignment applies
    // to any variant. A separator keeps only the line on the far side of its
    // label, so Start draws the trailing line and End the leading one.
    Marker* Alignment(MarkerAlignment value);
    MarkerAlignment ResolvedAlignment() const;
    Marker* Loading(bool value);
    Marker* WithLoadingStyle(MarkerLoadingStyle value);
    Marker* WithShimmerStyle(const ShimmerStyle& value);
    Marker* SeparatorStyle(const Style& s, uint32_t fields);
    Marker* Icon(MarkerIcon* value);
    Marker* Content(MarkerContent* value);
    Marker* Child(El* e);
    Marker* Refine(const Style& s, uint32_t fields);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_MARKER_H_
