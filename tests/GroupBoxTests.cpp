/* crates/ui/src/group_box.rs: variants and container structure. */

#include "Test.h"

using namespace gpui::component;

static bool GroupBoxColorEq(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void VariantsRoundTripPinnedNames() {
    utassert(GroupBoxVariantFromStr(StrL("normal")) == GroupBoxVariant::Normal);
    utassert(GroupBoxVariantFromStr(StrL("fill")) == GroupBoxVariant::Fill);
    utassert(GroupBoxVariantFromStr(StrL("outline")) ==
             GroupBoxVariant::Outline);
    utassert(GroupBoxVariantFromStr(StrL("other")) == GroupBoxVariant::Normal);
    utassert(GroupBoxVariantFromStr(StrL("FILL")) == GroupBoxVariant::Fill);
    utassert(GroupBoxVariantFromStr(StrL("OutLine")) ==
             GroupBoxVariant::Outline);
    utassert(base::StrEq(GroupBoxVariantAsStr(GroupBoxVariant::Normal),
                         StrL("normal")));
    utassert(
        base::StrEq(GroupBoxVariantAsStr(GroupBoxVariant::Fill), StrL("fill")));
    utassert(base::StrEq(GroupBoxVariantAsStr(GroupBoxVariant::Outline),
                         StrL("outline")));
}

static void VariantBuildersAndChildrenMatchTheSourceTree() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;
    const Theme& th = ThemeNow(&app);

    GroupBox* group = GroupBox::New(&cx, StrL("Title"))->Fill();
    for (int i = 0; i < 40; i++) {
        group->Child(Div(a));
    }
    utassert(group->children.len == 40);
    El* root = group->IntoEl();
    El* title = root->first;
    El* content = title ? title->next : nullptr;
    utassert(base::StrEq(root->id, StrL("group-box")));
    utassert(title != nullptr);
    utassert(title && title->first &&
             base::StrEq(title->first->text, StrL("Title")));
    utassertnear(title ? title->style.lineHeight : 0, 1.25f);
    utassert(title && title->style.hasColor);
    utassert(title && GroupBoxColorEq(title->style.color, th.mutedFg));
    utassert(content != nullptr);
    utassert(content && content->style.hasBg);
    utassert(content && content->style.pad.left == 16);
    utassert(content && content->style.gapX == 16);
    int children = 0;
    for (El* child = content ? content->first : nullptr; child;
         child = child->next) {
        children++;
    }
    utassert(children == 40);

    GroupBox* outlined = GroupBox::New(&cx)
                             ->Id(StrL("outlined"))
                             ->WithVariant(GroupBoxVariant::Outline);
    El* outlinedRoot = outlined->IntoEl();
    El* outlinedContent = outlinedRoot->first;
    utassert(base::StrEq(outlinedRoot->id, StrL("outlined")));
    utassert(outlinedContent && outlinedContent->style.border == 1);
    utassert(!outlinedContent || !outlinedContent->style.hasBg);

    outlined->Normal();
    utassert(outlined->variant == GroupBoxVariant::Normal);
    outlined->Filled(true);
    utassert(outlined->variant == GroupBoxVariant::Fill);
    outlined->Filled(false);
    utassert(outlined->variant == GroupBoxVariant::Normal);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void ElementTitlesAndThreeRefinementsAreRetained() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Style rootStyle = {};
    rootStyle.opacity = 0.5f;
    Style titleStyle = {};
    titleStyle.pad = EdgesAll(7);
    Style contentStyle = {};
    contentStyle.radius = 11;
    El* customTitle = Div(a)->Id(StrL("custom-title"));
    El* root = GroupBox::New(&cx)
                   ->Title(customTitle)
                   ->Refine(rootStyle, StyleFieldOpacity)
                   ->TitleStyle(titleStyle, StyleFieldPad)
                   ->ContentStyle(contentStyle, StyleFieldRadius)
                   ->Child(Div(a))
                   ->IntoEl();

    El* title = root->first;
    El* content = title ? title->next : nullptr;
    utassert(root->StyleStates()->refineSet == StyleFieldOpacity);
    utassertnear(root->StyleStates()->refine.opacity, 0.5f);
    utassert(title && title->first == customTitle);
    utassert(title && title->StyleStates()->refineSet == StyleFieldPad);
    utassert(title && title->StyleStates()->refine.pad.left == 7);
    utassert(content && content->StyleStates()->refineSet == StyleFieldRadius);
    utassert(content && content->StyleStates()->refine.radius == 11);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

void TestGroupBox() {
    TestSuite("group_box");
    VariantsRoundTripPinnedNames();
    VariantBuildersAndChildrenMatchTheSourceTree();
    ElementTitlesAndThreeRefinementsAreRetained();
}
