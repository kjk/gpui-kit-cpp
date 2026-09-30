#include "component_shell/support.h"

#include "ui/theme.h"

#include <string.h>

namespace gpui::component_shell {

bool RecordEmpty(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<Empty>();
}

bool RecordCommonBehavior(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<CommonBehavior>();
}

UiSize SizeOfLiteral(Str literal) {
    if (StrEq(literal, "xsmall")) return UiSize::XSmall;
    if (StrEq(literal, "small")) return UiSize::Small;
    if (StrEq(literal, "large")) return UiSize::Large;
    return UiSize::Medium;
}

bool RejectStyle(MaterializeRequest* request, const char* name) {
    if (request->HasStyle()) {
        request->styleTaken = true;
        request
            ->Fail(fmt("%s carries data rather than a box, so it does not "
                       "implement Styled",
                       Str(name)));
        return false;
    }
    request->styleTaken = true;
    return true;
}

bool RequireChild(MaterializeRequest* request, const char* parent,
                  const char* actual, Slice<const char*> allowed) {
    for (const char* name : allowed) {
        if (actual && strcmp(actual, name) == 0) return true;
    }
    StrBuilder names;
    for (int i = 0; i < allowed.count; i++) {
        if (i) names.Append(StrL(" or "));
        names.Append(Str(allowed[i]));
    }
    Str joined = names.TakeStr();
    request->Fail(fmt("%s accepts only registered %s children; received %s",
                      Str(parent), joined,
                      Str(actual ? actual : "an ordinary element")));
    StrFree(joined);
    return false;
}

// What a carrier element holds. The magic tells a carrier from an ordinary
// element whose customUser means something else.
struct CarrierBox {
    uint32_t magic = 0;
    const void* tag = nullptr;
    void* value = nullptr;
    bool taken = false;
};

static const uint32_t kCarrierMagic = 0x43617272; // "Carr"

El* Carrier(Ctx* cx, const void* tag, void* value) {
    CarrierBox* box = ArenaNew<CarrierBox>(cx->a);
    box->magic = kCarrierMagic;
    box->tag = tag;
    box->value = value;
    El* element = Div(cx->a);
    element->customUser = box;
    return element;
}

void* TakeCarried(MaterializeRequest* request, El* element, const void* tag,
                  const char* name) {
    CarrierBox* box = element && !element->customPaint && !element->prePaint
                          ? (CarrierBox*)element->customUser
                          : nullptr;
    if (!box || box->magic != kCarrierMagic || box->tag != tag) {
        request->Fail(fmt("%s materialized an incompatible child", Str(name)));
        return nullptr;
    }
    if (box->taken) {
        request->Fail(fmt("%s child was already consumed", Str(name)));
        return nullptr;
    }
    box->taken = true;
    return box->value;
}

bool ParseColorArgument(PayloadBuild* build, const char* component, Str text,
                        Rgba* out) {
    if (ThemeParseColor(text, out)) return true;
    return build->Fail(
        fmt("invalid %s color: `%s` is not a color", Str(component), text));
}

} // namespace gpui::component_shell
