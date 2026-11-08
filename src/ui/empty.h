#ifndef GPUI_UI_EMPTY_H_
#define GPUI_UI_EMPTY_H_
/* Composable empty states — crates/component/src/empty.rs */

#include "ui/sizing.h"

namespace gpui {
namespace component {

enum class EmptyMediaVariant : uint8_t {
    Default,
    Icon,
};

struct EmptyMedia : SlotBuilder<EmptyMedia> {
    EmptyMediaVariant variant = EmptyMediaVariant::Default;
    // A caller's whole StyleRefinement, applied with `style`.
    ElRefiner refiner = {};

    EmptyMedia* WithVariant(EmptyMediaVariant value);
    El* IntoEl();
};

struct EmptyTitle : SlotBuilder<EmptyTitle> {
    // A caller's whole StyleRefinement, applied with `style`.
    ElRefiner refiner = {};

    El* IntoEl();
};

struct EmptyDescription : SlotBuilder<EmptyDescription> {
    // A caller's whole StyleRefinement, applied with `style`.
    ElRefiner refiner = {};

    El* IntoEl();
};

struct EmptyContent : SlotBuilder<EmptyContent> {
    // A caller's whole StyleRefinement, applied with `style`.
    ElRefiner refiner = {};

    El* IntoEl();
};

struct EmptyHeader {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    EmptyMedia* media = nullptr;
    EmptyTitle* title = nullptr;
    EmptyDescription* description = nullptr;
    Style style = {};
    uint32_t styleSet = 0;
    // A caller's whole StyleRefinement, applied with `style`.
    ElRefiner refiner = {};

    static EmptyHeader* New(Ctx* cx);
    EmptyHeader* Media(EmptyMedia* value);
    EmptyHeader* Title(EmptyTitle* value);
    EmptyHeader* Description(EmptyDescription* value);
    EmptyHeader* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct Empty : SlotBuilder<Empty> {
    EmptyHeader* header = nullptr;
    EmptyContent* content = nullptr;
    // A caller's whole StyleRefinement, applied with `style`.
    ElRefiner refiner = {};

    Empty* Header(EmptyHeader* value);
    Empty* Content(EmptyContent* value);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_EMPTY_H_
