#ifndef GPUI_UI_GROUP_BOX_H_
#define GPUI_UI_GROUP_BOX_H_
/* Themed group box — crates/ui/src/group_box.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

enum class GroupBoxVariant : uint8_t {
    Normal,
    Fill,
    Outline
};

GroupBoxVariant GroupBoxVariantFromStr(Str text);
Str GroupBoxVariantAsStr(GroupBoxVariant variant);

struct GroupBox {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = StrL("group-box");
    Str title = {};
    // GroupBox::title takes `impl IntoElement`, not a string: the usage card
    // gives it a row with a label and a button in it.
    El* titleEl = nullptr;
    bool hasTitle = false;
    ArenaVec<El*> children;
    // GroupBox::footer: supporting content below, and outside, the surface.
    El* footer = nullptr;
    GroupBoxVariant variant = GroupBoxVariant::Normal;

    // StyleRefinement is a Style plus the fields it actually names in the
    // runtime POD seam. These are the root/title/content refinements Rust
    // stores independently.
    Style rootStyle = {};
    uint32_t rootStyleSet = 0;
    Style titleStyle = {};
    uint32_t titleStyleSet = 0;
    Style contentStyle = {};
    uint32_t contentStyleSet = 0;

    // The story's Custom Style section refines the box, its title and its
    // content container; these are the pieces it reaches for.
    bool titleSemibold = false;
    float titlePadX = 0;
    Background contentBg = {};
    bool hasContentBg = false;
    float contentRadius = -1;
    float contentPad = -1;
    float contentBorder = -1;

    static GroupBox* New(Ctx* cx);
    static GroupBox* New(Ctx* cx, Str title);
    GroupBox* Id(Str value);
    GroupBox* Title(El* e);
    GroupBox* Child(El* e);
    // Shares the title's leading edge, sits 8 px under the surface and reads
    // as small muted text, like a description.
    GroupBox* Footer(El* e);
    GroupBox* WithVariant(GroupBoxVariant value);
    GroupBox* Normal();
    GroupBox* Fill();
    GroupBox* Outline();
    // Compatibility spelling retained from the earlier port.
    GroupBox* Filled(bool v);
    GroupBox* Refine(const Style& style, uint32_t fields);
    GroupBox* TitleStyle(const Style& style, uint32_t fields);
    GroupBox* ContentStyle(const Style& style, uint32_t fields);
    GroupBox* TitleSemibold(bool v = true);
    GroupBox* TitlePadX(float px);
    GroupBox* ContentBg(Background c);
    GroupBox* ContentRadius(float px);
    GroupBox* ContentPad(float px);
    GroupBox* ContentBorder(float px);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_GROUP_BOX_H_
