#ifndef GPUI_UI_ACCORDION_H_
#define GPUI_UI_ACCORDION_H_
/* Themed accordion — crates/ui/src/accordion.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// The slice of StyleRefinement an item refines onto its title row and its
// content box. Rust hands `AccordionItem::title_style` / `content_style` a
// whole StyleRefinement; these are the fields anything actually sets.
// A negative padding is "leave it", and a zero alpha leaves the color alone.
struct AccordionStyle {
    float padT = -1;
    float padB = -1;
    float padL = -1;
    float padR = -1;
    Rgba fg = {};
};

// crates/ui's AccordionItem: a title element, an icon before it, and any
// element as its content.
struct AccordionItem {
    Ctx* cx = nullptr;
    El* title = nullptr;
    El* content = nullptr;
    bool open = false;
    bool disabled = false;
    IconName icon = IconName::None;
    AccordionStyle titleStyle = {};
    AccordionStyle contentStyle = {};

    static AccordionItem* New(Ctx* cx);
    AccordionItem* Title(El* t);
    AccordionItem* Title(Str s);
    AccordionItem* Icon(IconName i);
    AccordionItem* Open(bool v);
    AccordionItem* Disabled(bool v);
    AccordionItem* Child(El* c);
    AccordionItem* Child(Str s);
    AccordionItem* TitleStyle(const AccordionStyle& s);
    AccordionItem* ContentStyle(const AccordionStyle& s);
};

struct Accordion {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    bool multiple = false;
    bool bordered = true;
    bool disabled = false;
    UiSize size = UiSize::Medium;
    ArenaVec<AccordionItem*> items;
    Listener onToggle;

    static Accordion* New(Ctx* cx, Str id);
    Accordion* Multiple(bool v);
    Accordion* Bordered(bool v);
    Accordion* Disabled(bool v);
    Accordion* WithSize(UiSize s);
    Accordion* Item(AccordionItem* it);
    Accordion* OnToggle(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_ACCORDION_H_
