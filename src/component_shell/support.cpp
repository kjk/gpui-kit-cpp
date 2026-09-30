#include "component_shell/support.h"

#include "ui/theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gpui::component_shell {

bool RecordEmpty(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<Empty>();
}

bool RecordCommonBehavior(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<CommonBehavior>();
}

DeferredSlot* NewDeferredSlot(MaterializeRequest* request,
                              shell::ComponentElementFactory factory,
                              const char* failure) {
    DeferredSlot* slot = ArenaNew<DeferredSlot>(request->cx->a);
    slot->runtime = request->runtime;
    slot->specs = request->specs;
    slot->error = request->error;
    slot->factory = factory;
    slot->failure = failure;
    return slot;
}

El* BuildDeferredSlot(const DeferredSlot* slot, Ctx* cx) {
    MaterializeRequest request;
    request.cx = cx;
    request.runtime = slot->runtime;
    request.specs = slot->specs;
    request.error = slot->error;
    if (El* element = request.BuildFactory(slot->factory)) return element;
    return Div(cx->a)->Child(TextEl(
        cx->a, StrDup(cx->a, fmt("%s: no element", Str(slot->failure)))));
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

TempStr F64DisplayTemp(double value) {
    if (value != value) return fmt("NaN");
    if (value == INFINITY) return fmt("inf");
    if (value == -INFINITY) return fmt("-inf");
    bool negative = signbit(value) != 0;
    double magnitude = negative ? -value : value;
    if (magnitude == 0) return fmt(negative ? "-0" : "0");
    // The shortest `%.*e` that reads back as the same double: its digits and
    // its exponent are what Display lays out without the exponent.
    char buf[40];
    for (int precision = 0; precision < 17; precision++) {
        snprintf(buf, sizeof(buf), "%.*e", precision, magnitude);
        if (strtod(buf, nullptr) == magnitude) break;
    }
    char digits[24];
    int count = 0;
    const char* at = buf;
    for (; *at && *at != 'e'; at++) {
        if (*at >= '0' && *at <= '9' && count < 23) digits[count++] = *at;
    }
    int exponent = *at == 'e' ? atoi(at + 1) : 0;
    while (count > 1 && digits[count - 1] == '0') count--;
    StrBuilder out;
    if (negative) out.Append(StrL("-"));
    if (exponent < 0) {
        out.Append(StrL("0."));
        for (int i = 0; i < -exponent - 1; i++) out.Append(StrL("0"));
        out.Append(Str(digits, count));
    } else if (exponent >= count - 1) {
        out.Append(Str(digits, count));
        for (int i = 0; i < exponent - (count - 1); i++) out.Append(StrL("0"));
    } else {
        out.Append(Str(digits, exponent + 1));
        out.Append(StrL("."));
        out.Append(Str(digits + exponent + 1, count - exponent - 1));
    }
    Str owned = out.TakeStr();
    TempStr result = fmt("%s", owned);
    StrFree(owned);
    return result;
}

} // namespace gpui::component_shell
