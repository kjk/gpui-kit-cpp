#include "Story.h"

// ColorName::all() minus Gray, and the Tailwind scales Tag::color
// picks in a light theme: 50 for the fill, 200 for the border, 600 for
// the label (default-colors.json).
struct TagColor {
    const char* name;
    Rgba bg;
    Rgba border;
    Rgba fg;
};
static const TagColor kTagColors[] = {
    {"Neutral", Rgb(0xfa, 0xfa, 0xfa), Rgb(0xe5, 0xe5, 0xe5),
     Rgb(0x52, 0x52, 0x52)},
    {"Red", Rgb(0xfe, 0xf2, 0xf2), Rgb(0xfe, 0xca, 0xca),
     Rgb(0xdc, 0x26, 0x26)},
    {"Orange", Rgb(0xff, 0xf7, 0xed), Rgb(0xfe, 0xd7, 0xaa),
     Rgb(0xea, 0x58, 0x0c)},
    {"Amber", Rgb(0xff, 0xfb, 0xeb), Rgb(0xfd, 0xe6, 0x8a),
     Rgb(0xd9, 0x77, 0x06)},
    {"Yellow", Rgb(0xfe, 0xfc, 0xe8), Rgb(0xfe, 0xf0, 0x8a),
     Rgb(0xca, 0x8a, 0x04)},
    {"Lime", Rgb(0xf7, 0xfe, 0xe7), Rgb(0xd9, 0xf9, 0x9d),
     Rgb(0x65, 0xa3, 0x0d)},
    {"Green", Rgb(0xf0, 0xfd, 0xf4), Rgb(0xbb, 0xf7, 0xd0),
     Rgb(0x16, 0xa3, 0x4a)},
    {"Emerald", Rgb(0xec, 0xfd, 0xf5), Rgb(0xa7, 0xf3, 0xd0),
     Rgb(0x05, 0x96, 0x69)},
    {"Teal", Rgb(0xf0, 0xfd, 0xfa), Rgb(0x99, 0xf6, 0xe4),
     Rgb(0x0d, 0x94, 0x88)},
    {"Cyan", Rgb(0xec, 0xfe, 0xff), Rgb(0xa5, 0xf3, 0xfc),
     Rgb(0x08, 0x91, 0xb2)},
    {"Sky", Rgb(0xf0, 0xf9, 0xff), Rgb(0xba, 0xe6, 0xfd),
     Rgb(0x02, 0x84, 0xc7)},
    {"Blue", Rgb(0xef, 0xf6, 0xff), Rgb(0xbf, 0xdb, 0xfe),
     Rgb(0x25, 0x63, 0xeb)},
    {"Indigo", Rgb(0xee, 0xf2, 0xff), Rgb(0xc7, 0xd2, 0xfe),
     Rgb(0x4f, 0x46, 0xe5)},
    {"Violet", Rgb(0xf5, 0xf3, 0xff), Rgb(0xdd, 0xd6, 0xfe),
     Rgb(0x7c, 0x3a, 0xed)},
    {"Purple", Rgb(0xfa, 0xf5, 0xff), Rgb(0xe9, 0xd5, 0xff),
     Rgb(0x93, 0x33, 0xea)},
    {"Fuchsia", Rgb(0xfd, 0xf4, 0xff), Rgb(0xf5, 0xd0, 0xfe),
     Rgb(0xc0, 0x26, 0xd3)},
    {"Pink", Rgb(0xfd, 0xf2, 0xf8), Rgb(0xfb, 0xcf, 0xe8),
     Rgb(0xdb, 0x27, 0x77)},
    {"Rose", Rgb(0xff, 0xf1, 0xf2), Rgb(0xfe, 0xcd, 0xd3),
     Rgb(0xe1, 0x1d, 0x48)},
};
static const int kNTagColors = 18;

struct TagStory {
    StoryToolbarState toolbar;

    static El* Render(TagStory* self, Ctx* cx);
};

static El* TagRow(Ctx* cx, TagStory* self, bool outline, float radius) {
    Arena* a = cx->a;
    // tag_story.rs uses the indigo_500()/indigo_50() palette helpers here,
    // not theme tokens, so these stay literal.
    // Tag::custom(indigo_500, indigo_50, indigo_500) filled, and
    // Tag::custom(indigo_500, indigo_500, indigo_500) outlined.
    Rgba indigo = Rgb(0x63, 0x66, 0xf1);
    Rgba indigo50 = Rgb(0xee, 0xf2, 0xff);
    const char* labels[] = {"Tag",     "Secondary", "Danger",
                            "Success", "Warning",   "Info"};
    using Fn = component::Tag* (*)(component::Tag*);
    Fn fns[] = {
        [](component::Tag* t) { return t->Primary(); },
        [](component::Tag* t) { return t->Secondary(); },
        [](component::Tag* t) { return t->Danger(); },
        [](component::Tag* t) { return t->Success(); },
        [](component::Tag* t) { return t->Warning(); },
        [](component::Tag* t) { return t->Info(); },
    };
    El* row = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->Wrap();
    for (int i = 0; i < 6; i++) {
        component::Tag* t = component::Tag::New(cx, Str(labels[i]))
                                ->WithSize(self->toolbar.size);
        fns[i](t);
        if (outline) {
            t->Outline();
        }
        if (radius >= 0) {
            t->Radius(radius);
        }
        row->Child(t->IntoEl());
    }
    if (radius < 0) {
        component::Tag* c =
            component::Tag::New(cx, StrL("Custom"))
                ->Custom(indigo, outline ? indigo : indigo50, indigo)
                ->WithSize(self->toolbar.size);
        if (outline) {
            c->Outline();
        }
        row->Child(c->IntoEl());
    }
    return row;
}

El* TagStory::Render(TagStory* self, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* def = StorySection(cx, "Default", nullptr);
    StorySectionAdd(def, TagRow(cx, self, false, -1));
    page->Child(def);

    El* out = StorySection(cx, "Outline", nullptr);
    StorySectionAdd(out, TagRow(cx, self, true, -1));
    page->Child(out);

    El* rnd = StorySection(cx, "Rounded", nullptr);
    StorySectionAdd(rnd, TagRow(cx, self, false, 99));
    page->Child(rnd);

    El* sq = StorySection(cx, "Square", nullptr);
    StorySectionAdd(sq, TagRow(cx, self, false, 0));
    page->Child(sq);

    // ColorName::all() minus Gray, as Tag::color paints each of them.
    El* colors = StorySection(cx, "Colors", nullptr);
    StorySectionBody(colors)->MaxW(640);
    // v_flex().w_full().gap_4() around one h_flex().w_full().gap_2()
    // .flex_wrap() of every colour.
    El* ccol = Div(a)->FlexCol()->W(kFill)->Gap(16);
    El* crow = Div(a)->FlexRow()->W(kFill)->Gap(8)->FlexWrap();
    ccol->Child(crow);
    for (int i = 0; i < kNTagColors; i++) {
        crow->Child(component::Tag::New(cx, Str(kTagColors[i].name))
                        ->Custom(kTagColors[i].bg, kTagColors[i].fg,
                                 kTagColors[i].border)
                        ->WithSize(self->toolbar.size)
                        ->IntoEl());
    }
    StorySectionAdd(colors, ccol);
    page->Child(colors);
    return page;
}

STORY_PAGE(StoryTag, TagStory);
