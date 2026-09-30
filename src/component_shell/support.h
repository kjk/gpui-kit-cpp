#ifndef GPUI_COMPONENT_SHELL_SUPPORT_H_
#define GPUI_COMPONENT_SHELL_SUPPORT_H_

// crates/component-shell/src/shell/support.rs and typed_child.rs: the
// descriptor helpers every family shares, written once so an error message
// and a documentation sentence do not drift between a dozen copies.
//
// The descriptors are `constexpr` tables, so a family's catalog is constant
// data with no initialization order to get wrong across translation units.

#include "component_shell/lib.h"
#include "ui/sizing.h"

namespace gpui::component_shell {

using shell::ArgumentDescriptor;
using shell::ComponentArgument;
using shell::ComponentDescriptor;
using shell::ComponentPayload;
using shell::ConstructorDescriptor;
using shell::MaterializeRequest;
using shell::MethodDescriptor;
using shell::PayloadBuild;
using shell::SchemaBoolean;
using shell::SchemaCallback;
using shell::SchemaElement;
using shell::SchemaEntity;
using shell::SchemaEnum;
using shell::SchemaNumber;
using shell::SchemaString;
using shell::Slice;

// The payload of a component whose constructor takes no arguments.
struct Empty {};

// The marker a common-behavior method records. The value itself is read by
// the shell, not by the adapter: declaring the method is what makes the
// runtime accept the call and resolve it into the request.
struct CommonBehavior {};

bool RecordEmpty(PayloadBuild* build, const ComponentArgument*, int);
bool RecordCommonBehavior(PayloadBuild* build, const ComponentArgument*, int);

inline constexpr ArgumentDescriptor kOnClickArguments[] = {
    {"callback", SchemaCallback("(event: ClickEvent, cx: Context) => void")}};
inline constexpr ArgumentDescriptor kDisabledArguments[] = {
    {"disabled", SchemaBoolean()}};
inline constexpr ArgumentDescriptor kSelectedArguments[] = {
    {"selected", SchemaBoolean()}};

// `on_click(callback)`, for a component whose materializer honours
// MaterializeRequest::onClick.
inline constexpr MethodDescriptor kOnClickMethod = {
    "on_click", kOnClickArguments,
    "Invokes the callback when this component is activated.",
    &RecordCommonBehavior};
// `disabled(value)`, for one that honours MaterializeRequest::disabled.
inline constexpr MethodDescriptor kDisabledMethod = {
    "disabled", kDisabledArguments,
    "Controls whether this component accepts interaction.",
    &RecordCommonBehavior};
// `selected(value)`, for one that honours MaterializeRequest::selected.
inline constexpr MethodDescriptor kSelectedMethod = {
    "selected", kSelectedArguments,
    "Marks this component as the selected one among its siblings.",
    &RecordCommonBehavior};

inline constexpr const char* kSizeLiterals[] = {"xsmall", "small", "medium",
                                                "large"};

// `xsmall`, `small`, `medium` or `large`. Medium for anything else, which the
// schema has already refused.
UiSize SizeOfLiteral(Str literal);

// Refuses style on a component that has nowhere to put it. A data-carrying
// part such as MenuItem renders no box of its own; silently dropping a
// script's `.bg(..)` would look like a shell bug.
bool RejectStyle(MaterializeRequest* request, const char* name);

// Requires a typed child to be one of the components `parent` accepts, and
// names both what it accepts and what it received when it is not.
bool RequireChild(MaterializeRequest* request, const char* parent,
                  const char* actual, Slice<const char*> allowed);

// ─── typed_child.rs ────────────────────────────────────────────────────────
//
// The shell materializes a child into an El, but a native parent such as
// Menu, Settings or Table needs the concrete value its builder takes. A
// carrier is the one element that carries such a value through that erasure,
// and TakeCarried is the only way back out.

El* Carrier(Ctx* cx, const void* tag, void* value);
// The value `element` carries when it is a carrier of `tag`, exactly once;
// null otherwise, with the reason recorded on `request`.
void* TakeCarried(MaterializeRequest* request, El* element, const void* tag,
                  const char* name);

template <class T>
El* CarrierOf(Ctx* cx, T* value) {
    return Carrier(cx, shell::PayloadTag<T>(), value);
}
template <class T>
T* TakeCarriedAs(MaterializeRequest* request, El* element, const char* name) {
    return (T*)TakeCarried(request, element, shell::PayloadTag<T>(), name);
}

// Visits every recorded method whose payload is a `T`, in script order.
template <class T, class F>
void EachMethod(const MaterializeRequest* request, F&& visit) {
    for (const shell::SpecOp& op : request->node->ops) {
        if (op.kind != shell::SpecOpKind::RegisteredMethod) continue;
        if (const T* value = shell::PayloadAs<T>(op.payload)) visit(*value);
    }
}

// Parses a theme colour string (`#rrggbb`, `blue-600`, ...) for a method's
// recorder, failing with "invalid <component> color: ..." as Rust does.
bool ParseColorArgument(PayloadBuild* build, const char* component, Str text,
                        Rgba* out);

} // namespace gpui::component_shell
#endif // GPUI_COMPONENT_SHELL_SUPPORT_H_
