#ifndef GPUI_COMPONENT_SHELL_TYPED_COMPOUND_MOD_H_
#define GPUI_COMPONENT_SHELL_TYPED_COMPOUND_MOD_H_

// crates/component-shell/src/shell/typed_compound/mod.rs: the typed part
// element the compound families share (take_element, TypedChildElement,
// finish_part, finish_typed_children).
//
// A part such as a Radio, a Tab or an EmptyTitle is a component value its
// parent's builder takes, and also an element that renders on its own when
// an ordinary parent holds it. Rust's TypedChildElement<T> is both: it lays
// out as the value it wraps, and a typed parent downcasts it back to the
// value. Here the part's materializer renders the value, and the element it
// returns carries the value beside that rendering; a typed parent takes the
// value back and renders it again itself, so the first rendering is only
// what an ordinary parent shows.
//
// The part's style goes onto the value, not the rendering (Rust refines the
// part's own StyleRefinement): each part component holds an ElRefiner and
// applies it where its render refines its style, so the style follows the
// value into whichever parent renders it.

#include "component_shell/support.h"

namespace gpui::component_shell::typed_compound {

// TypedChildElement::new(value): `rendered` carrying `value` of type `tag`.
El* TypedChildElement(Ctx* cx, const void* tag, void* value, El* rendered);

// take_element: the value `element` carries when it is a typed part of
// `tag`, exactly once; null otherwise, with Rust's reason on `request`:
// "registered <name> materialized an incompatible element" or "registered
// <name> child was already consumed".
void* TakeElement(MaterializeRequest* request, El* element, const void* tag,
                  const char* name);

// `element.downcast_mut::<TypedChildElement<T>>().is_some()`: whether
// `element` is a typed part of `tag` still holding its value, without taking
// it or failing the request.
bool IsTypedElement(El* element, const void* tag);

template <class T>
bool IsTypedElementOf(El* element) {
    return IsTypedElement(element, shell::PayloadTag<T>());
}

template <class T>
El* TypedChildElementOf(Ctx* cx, T* value) {
    return TypedChildElement(cx, shell::PayloadTag<T>(), value,
                             value->IntoEl());
}

template <class T>
T* TakeElementAs(MaterializeRequest* request, El* element, const char* name) {
    return (T*)TakeElement(request, element, shell::PayloadTag<T>(), name);
}

// finish_part: the node's style onto the part, its ordinary children into
// it, and the part as a typed element. `T` has `refiner`, `Child(El*)` and
// `IntoEl()`.
template <class T>
El* FinishPart(MaterializeRequest* request, T* element) {
    element->refiner = request->TakeStyle();
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (children[i]) element->Child(children[i]);
    }
    return TypedChildElementOf(request->cx, element);
}

// finish_typed_children: every child must be one of `allowed`, and each is
// materialized into the part in order before its style is taken.
template <class T>
El* FinishTypedChildren(MaterializeRequest* request, T* element,
                        const char* parent, Slice<const char*> allowed) {
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, parent, children[i].componentName, allowed))
            return nullptr;
    }
    for (int i = 0; i < count; i++) {
        El* child = request->MaterializeChild(&children[i]);
        if (!child) return nullptr;
        element->Child(child);
    }
    element->refiner = request->TakeStyle();
    return TypedChildElementOf(request->cx, element);
}

// index_callback_payload for RadioGroup: one callback recorded as the
// group's on_change op, or "<component>.on_change(callback) expects a
// callback". Exposed for the tests, as Rust's is to its own.
bool IndexCallbackPayload(PayloadBuild* build, const ComponentArgument* args,
                          int count, const char* component);
// The callback a RadioGroup on_change op carries, when `payload` is one.
bool RadioGroupOnChangeCallback(const ComponentPayload& payload,
                                shell::CallbackId* callback);

} // namespace gpui::component_shell::typed_compound
#endif // GPUI_COMPONENT_SHELL_TYPED_COMPOUND_MOD_H_
