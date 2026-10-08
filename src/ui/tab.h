#ifndef GPUI_SRC_UI_TAB_H_
#define GPUI_SRC_UI_TAB_H_
/* Themed tabs — crates/ui/src/tab

   Rust splits this in two: `Tab` is one tab and `TabBar` is the strip that
   holds them and hands each one the variant, the size and its index.
   `component::Tabs` remains a compatibility alias for the source-named
   `TabBar`. */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// TabVariant. Six looks over the same behaviour: a plain tab, an outlined
// one, a pill, a segmented control, an underline, and a browser-style folder
// tab whose selected item joins the content below.
enum class TabVariant : uint8_t {
    Tab,
    Outline,
    Pill,
    Segmented,
    Underline,
    Folder
};

// The tables from `impl TabVariant`, which are what the look actually is.
// They are functions rather than constants because every one of them is a
// match on the variant and the size.
float TabHeight(TabVariant v, UiSize size);
float TabInnerHeight(TabVariant v, UiSize size);
// inner_paddings: the padding either side of the label. Underline has none —
// its gap comes from the bar instead.
float TabPadX(TabVariant v, UiSize size);
// inner_margins: only Underline has any, and only top and bottom.
float TabMarginTop(TabVariant v, UiSize size);
float TabMarginBottom(TabVariant v, UiSize size);
// The gap between two tabs, which the bar owns.
float TabBarGap(TabVariant v, UiSize size);
// The bar's own padding, which only Segmented has.
float TabBarPadX(TabVariant v, UiSize size);
// A rounded bar: Segmented only.
float TabBarRadius(TabVariant v, UiSize size, float radius, float radiusLg);
float TabRadius(TabVariant v, UiSize size, float radius, float radiusLg);
float TabInnerRadius(TabVariant v, UiSize size, float radius, float radiusLg);
// Folder tab geometry. The radius is the theme's, capped at a third of the
// tab height so a curve cannot reach under the neighbor's hover fill.
struct FolderTabMetrics {
    float topPadding = 0;
    float radius = 0;
    float paddingX = 0;
    float separatorHeight = 0;
};
FolderTabMetrics FolderTabMetricsFor(UiSize size, float radius, float radiusLg);
// How far a tab paints past its box. Only Folder has any.
float TabOverhang(TabVariant v, UiSize size, float radius, float radiusLg);

// A source-shaped Tab value. It is arena-built like every other component,
// then copied as POD into TabBar's list; its element children remain owned by
// the frame arena.
struct Tab {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str label = {};
    Str ariaLabel = {};
    IconName icon = IconName::None;
    El* prefix = nullptr;
    El* suffix = nullptr;
    ArenaVec<El*> children;
    TabVariant variant = TabVariant::Tab;
    UiSize size = UiSize::Medium;
    bool disabled = false;
    bool selected = false;
    bool tabBarPrefix = true;
    // Tab::flex_1(): the tab shares the bar with its siblings instead of
    // taking the width of its own label. Two of them split a segmented bar
    // evenly, which is what the colour picker's Palette/HSLA pair does.
    bool flex1 = false;
    float maxWidth = 0;
    Listener onClick;
    Style style = {};
    uint32_t styleSet = 0;
    // A caller's whole StyleRefinement, applied with `style`.
    ElRefiner refiner = {};

    static Tab* New(Ctx* cx);
    static Tab* New(Ctx* cx, Str label);
    Tab* Label(Str value);
    Tab* AriaLabel(Str value);
    Tab* Icon(IconName value);
    Tab* Prefix(El* value);
    Tab* Suffix(El* value);
    Tab* Child(El* value);
    Tab* Disabled(bool value = true);
    Tab* Selected(bool value = true);
    Tab* OnClick(Listener value);
    Tab* WithVariant(TabVariant value);
    Tab* Outline();
    Tab* Pill();
    Tab* Segmented();
    Tab* Underline();
    Tab* Folder();
    Tab* WithSize(UiSize value);
    Tab* Flex1();
    Tab* MaxWidth(float value);
    Tab* TabBarPrefix(bool value);
    Tab* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct TabBar {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    // As many tabs as the caller adds; the builder is on the frame arena, so
    // the items grow into it.
    ArenaVec<component::Tab> items;
    // Rust's Option<usize>: -1 means no controlled selection.
    int selected = -1;
    TabVariant variant = TabVariant::Tab;
    UiSize size = UiSize::Medium;
    // TabBar::max_width: the width a label gives way at, ellipsizing inside
    // it. 0 is Rust's `None` — the label decides the width and never shrinks.
    float maxWidth = 0;
    // The bar's own width. kAuto is Rust's default — the tabs decide.
    float width = kAuto;
    El* prefix = nullptr;
    El* suffix = nullptr;
    El* lastEmptySpace = nullptr;
    // TabBar::menu: the overflow button after the strip. Its dropdown lists
    // every tab by name, checked on the selected one and disabled where the
    // tab is, so a bar too narrow to show them all can still reach them.
    bool menu = false;
    Listener onChange;
    bool trackScroll = false;
    int scrollId = 0;
    float scrollX = 0;
    Listener onScroll;
    Style style = {};
    uint32_t styleSet = 0;

    static TabBar* New(Ctx* cx);
    static TabBar* New(Ctx* cx, Str id);
    TabBar* Child(component::Tab* child);
    TabBar* Child(Str label);
    TabBar* Tab(Str label);
    TabBar* Tab(Str label, IconName icon, bool disabled = false);
    // `Tab::new().flex_1()`, applied to the tab just added.
    TabBar* Flex1();
    // `Tab::aria_label`, applied to the tab most recently added.
    TabBar* AriaLabel(Str label);
    TabBar* Disabled(int ix, bool v = true);
    TabBar* Selected(int i);
    TabBar* OnChange(Listener fn);
    TabBar* OnClick(Listener fn);
    TabBar* Variant(TabVariant v);
    TabBar* WithVariant(TabVariant v);
    TabBar* Outline();
    TabBar* Pill();
    TabBar* Segmented();
    TabBar* Underline();
    TabBar* Folder();
    TabBar* Size(UiSize v);
    TabBar* WithSize(UiSize v);
    TabBar* MaxWidth(float v);
    // TabBar has no width of its own in Rust: it is as wide as its tabs
    // unless the caller says `.w_full()` or `.w_64()`. A bar that fills is
    // also what makes `Tab::flex_1()` mean anything, since there is no free
    // space to share otherwise.
    TabBar* W(float v);
    TabBar* WFill();
    TabBar* Prefix(El* e);
    TabBar* Suffix(El* e);
    TabBar* LastEmptySpace(El* e);
    TabBar* Menu(bool v = true);
    // ScrollHandle projection: the owning view supplies the retained offset
    // and listener exactly as it does for every other scroll element here.
    // Clicking a tab scrolls it into view with part of the next one showing.
    // Changing the selection, the tabs or the layout does not.
    TabBar* TrackScroll(int scrollKey, float offset, Listener fn);
    TabBar* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

using Tabs = TabBar;

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_TAB_H_
