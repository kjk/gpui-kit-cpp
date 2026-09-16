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

struct EmptyMedia {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    EmptyMediaVariant variant = EmptyMediaVariant::Default;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static EmptyMedia* New(Ctx* cx);
    EmptyMedia* WithVariant(EmptyMediaVariant value);
    EmptyMedia* Child(El* child);
    EmptyMedia* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct EmptyTitle {
    Arena* a = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static EmptyTitle* New(Ctx* cx);
    EmptyTitle* Child(El* child);
    EmptyTitle* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct EmptyDescription {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static EmptyDescription* New(Ctx* cx);
    EmptyDescription* Child(El* child);
    EmptyDescription* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct EmptyContent {
    Arena* a = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static EmptyContent* New(Ctx* cx);
    EmptyContent* Child(El* child);
    EmptyContent* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct EmptyHeader {
    Arena* a = nullptr;
    EmptyMedia* media = nullptr;
    EmptyTitle* title = nullptr;
    EmptyDescription* description = nullptr;
    Style style = {};
    uint32_t styleSet = 0;

    static EmptyHeader* New(Ctx* cx);
    EmptyHeader* Media(EmptyMedia* value);
    EmptyHeader* Title(EmptyTitle* value);
    EmptyHeader* Description(EmptyDescription* value);
    EmptyHeader* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct Empty {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    EmptyHeader* header = nullptr;
    EmptyContent* content = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static Empty* New(Ctx* cx);
    Empty* Header(EmptyHeader* value);
    Empty* Content(EmptyContent* value);
    Empty* Child(El* child);
    Empty* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_EMPTY_H_
