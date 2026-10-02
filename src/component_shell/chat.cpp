// crates/component-shell/src/shell/chat.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/attachment.h"
#include "ui/bubble.h"
#include "ui/marker.h"
#include "ui/message.h"
#include "ui/message_scroller.h"
#include "ui/shimmer.h"

#include <float.h>
#include <limits.h>
#include <math.h>

namespace gpui::component_shell::chat {

using component::AttachmentStatus;
using component::BubbleVariant;
using component::MarkerLoadingStyle;
using component::MarkerVariant;
using component::MessageAlignment;

// Payload / Kind: one payload type for the six components, tagged by which
// one recorded it.
struct Payload {
    enum Kind : uint8_t {
        Attachment,
        Bubble,
        Marker,
        Message,
        ShimmerText,
        MessageScroller,
    } kind = Attachment;
    Str id;
    Str text;
    ComponentArgument state = {};
    ComponentArgument renderItem = {};
};

struct Op {
    enum Kind : uint8_t {
        Status,
        Axis,
        Size,
        Alignment,
        BubbleVariant,
        MarkerVariant,
        Loading,
        LoadingStyle,
        Id,
        Duration,
        Spread,
        Reverse,
        Once,
        Scrollbar,
        JumpButton,
        JumpButtonLabel,
    } kind = Status;
    // The enum literal's index in the method's vocabulary, which is the
    // native enum's order for every enum method below.
    int literal = 0;
    UiSize size = UiSize::Medium;
    bool flag = false;
    float number = 0;
    Str text;
};

// ─── Materializer ──────────────────────────────────────────────────────────

// What one message row renderer needs: BoundMessageScroller's closure.
struct RowRenderer {
    ShellRuntime* runtime = nullptr;
    shell::ComponentCallback callback = {};
};

static El* RenderRow(void* user, Ctx* cx, int index) {
    RowRenderer* renderer = (RowRenderer*)user;
    shell::ComponentDataValue argument =
        shell::ComponentDataValue::Number((double)index);
    Str error;
    // Interactive, so a row's own handlers (a button inside a message) stay
    // live the way they do in Rust, whose element callbacks always are.
    El* element = renderer->callback.BuildInteractiveWith(
        renderer->runtime, &argument, 1, cx, &error);
    if (element) return element;
    if (!len(error)) return Div(cx->a);
    return Div(cx->a)->Child(TextEl(
        cx->a, StrDup(cx->a, fmt("Failed to render message row: %s", error))));
}

static El* Materialize(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload)
        return request
            ->Fail(StrL("chat component received an incompatible payload"));
    Ctx* cx = request->cx;
    switch (payload->kind) {
        case Payload::Attachment: {
            component::Attachment* attachment = component::Attachment::New(cx)
                                                    ->Id(payload->id);
            EachMethod<Op>(request, [&](const Op& op) {
                switch (op.kind) {
                    case Op::Status:
                        attachment->Status((AttachmentStatus)op.literal);
                        break;
                    case Op::Axis:
                        attachment->WithAxis((gpui::Axis)op.literal);
                        break;
                    case Op::Size:
                        attachment->WithSize(op.size);
                        break;
                    default:
                        break;
                }
            });
            El** children = nullptr;
            int count = 0;
            if (!request->TakeChildren(&children, &count)) return nullptr;
            if (count > 0) {
                component::AttachmentContent* content =
                    component::AttachmentContent::New(cx);
                for (int i = 0; i < count; i++) content->Child(children[i]);
                attachment->Content(content);
            }
            return request->ApplyStyle(attachment->IntoEl());
        }
        case Payload::Bubble: {
            component::Bubble* bubble = component::Bubble::New(cx);
            EachMethod<Op>(request, [&](const Op& op) {
                if (op.kind == Op::Alignment)
                    bubble->Alignment((MessageAlignment)op.literal);
                else if (op.kind == Op::BubbleVariant)
                    bubble->WithVariant((BubbleVariant)op.literal);
            });
            // request.finish(bubble): the style, and the children extended
            // into the bubble the way its ParentElement takes them.
            El** children = nullptr;
            int count = 0;
            if (!request->TakeChildren(&children, &count)) return nullptr;
            for (int i = 0; i < count; i++) bubble->Child(children[i]);
            return request->ApplyStyle(bubble->IntoEl());
        }
        case Payload::Marker: {
            component::Marker* marker = component::Marker::New(cx)
                                            ->Id(payload->id);
            EachMethod<Op>(request, [&](const Op& op) {
                if (op.kind == Op::MarkerVariant)
                    marker->WithVariant((MarkerVariant)op.literal);
                else if (op.kind == Op::Loading)
                    marker->Loading(op.flag);
                else if (op.kind == Op::LoadingStyle)
                    marker->WithLoadingStyle((MarkerLoadingStyle)op.literal);
            });
            // request.finish(marker).
            El** children = nullptr;
            int count = 0;
            if (!request->TakeChildren(&children, &count)) return nullptr;
            for (int i = 0; i < count; i++) marker->Child(children[i]);
            return request->ApplyStyle(marker->IntoEl());
        }
        case Payload::Message: {
            component::Message* message = component::Message::New(cx);
            EachMethod<Op>(request, [&](const Op& op) {
                if (op.kind == Op::Alignment)
                    message->Alignment((MessageAlignment)op.literal);
            });
            El** children = nullptr;
            int count = 0;
            if (!request->TakeChildren(&children, &count)) return nullptr;
            if (count > 0) {
                component::MessageContent* content =
                    component::MessageContent::New(cx);
                for (int i = 0; i < count; i++) content->Child(children[i]);
                message->Content(content);
            }
            return request->ApplyStyle(message->IntoEl());
        }
        case Payload::ShimmerText: {
            if (request->ChildrenLen() != 0)
                return request
                    ->Fail(StrL("ShimmerText does not accept children"));
            component::ShimmerText* shimmer =
                component::ShimmerText::New(cx, payload->text);
            EachMethod<Op>(request, [&](const Op& op) {
                switch (op.kind) {
                    case Op::Id:
                        shimmer->Id(op.text);
                        break;
                    case Op::Duration:
                        shimmer->Duration(op.number);
                        break;
                    case Op::Spread:
                        shimmer->Spread(op.number);
                        break;
                    case Op::Reverse:
                        shimmer->Reverse(op.flag);
                        break;
                    case Op::Once:
                        shimmer->Once(op.flag);
                        break;
                    default:
                        break;
                }
            });
            return request->ApplyStyle(shimmer->IntoEl());
        }
        case Payload::MessageScroller: {
            if (request->ChildrenLen() != 0)
                return request
                    ->Fail(StrL("MessageScroller does not accept children"));
            EntityState<component::MessageScrollerState>* state =
                request->StateAs<EntityState<component::MessageScrollerState>>(
                    payload->state, "MessageScrollerState");
            if (!state) return nullptr;
            RowRenderer* renderer = ArenaNew<RowRenderer>(cx->a);
            renderer->runtime = request->runtime;
            renderer->callback = request->ResolveCallback(payload->renderItem);
            if (!renderer->callback.IsSet())
                return request
                    ->Fail(StrL("component argument is not a callback"));
            component::MessageScroller* scroller =
                component::MessageScroller::New(cx, payload->id, state->entity,
                                                &RenderRow, renderer);
            EachMethod<Op>(request, [&](const Op& op) {
                if (op.kind == Op::Scrollbar)
                    scroller->Scrollbar(op.flag);
                else if (op.kind == Op::JumpButton)
                    scroller->JumpButton(op.flag);
                else if (op.kind == Op::JumpButtonLabel)
                    scroller->WithJumpButtonLabel(op.text);
            });
            // The scroller fills its box and virtualizes against the bounds
            // it was laid out at, as GPUI's `list` does; its root is
            // `size_full()` and the script's style refines it, as in Rust.
            El* root = scroller->IntoEl();
            request->TakeStyle().Apply(root);
            return root;
        }
    }
    return request
        ->Fail(StrL("chat component received an incompatible payload"));
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool NonEmptyId(Str id) {
    return len(StrTrim(id)) > 0;
}

static bool ConstructAttachment(PayloadBuild* build,
                                const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        !NonEmptyId(args[0].string))
        return build->Fail(StrL("Attachment expects a non-empty stable id"));
    Payload* payload = build->New<Payload>();
    payload->kind = Payload::Attachment;
    payload->id = args[0].string;
    return true;
}

// no_arg(name, kind).
template <Payload::Kind K>
static bool ConstructNoArg(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<Payload>()->kind = K;
    return true;
}

static bool ConstructMarker(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        !NonEmptyId(args[0].string))
        return build->Fail(StrL("Marker expects a non-empty stable id"));
    Payload* payload = build->New<Payload>();
    payload->kind = Payload::Marker;
    payload->id = args[0].string;
    return true;
}

static bool ConstructShimmerText(PayloadBuild* build,
                                 const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("ShimmerText expects text"));
    Payload* payload = build->New<Payload>();
    payload->kind = Payload::ShimmerText;
    payload->text = args[0].string;
    return true;
}

static bool ConstructMessageScroller(PayloadBuild* build,
                                     const ComponentArgument* args, int count) {
    if (count != 3 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::Entity ||
        args[2].kind != shell::ComponentArgumentKind::Callback ||
        !NonEmptyId(args[0].string)) {
        return build
            ->Fail(StrL("MessageScroller expects a non-empty id, "
                        "MessageScrollerState, and row renderer"));
    }
    Payload* payload = build->New<Payload>();
    payload->kind = Payload::MessageScroller;
    payload->id = args[0].string;
    payload->state = args[1];
    payload->renderItem = args[2];
    return true;
}

// enum_method(component, name, values, ..): which method it is decides the
// op, and the literal's index is the value. `name` is also the argument's.
template <Op::Kind K>
static bool RecordEnum(PayloadBuild* build, const ComponentArgument* args,
                       int count, const char* component, const char* name,
                       Slice<const char*> values) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(
            fmt("%s.%s expects one enum literal", Str(component), Str(name)));
    for (int i = 0; i < values.count; i++) {
        if (StrEq(args[0].string, values[i])) {
            Op* op = build->New<Op>();
            op->kind = K;
            op->literal = i;
            op->size = SizeOfLiteral(args[0].string);
            return true;
        }
    }
    return build->Fail(fmt("unsupported %s.%s value `%s`", Str(component),
                           Str(name), args[0].string));
}

// bool_method(component, name, ..).
template <Op::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count, const char* component, const char* name) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build
            ->Fail(fmt("%s.%s expects one boolean", Str(component), Str(name)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static constexpr const char* kStatusLiterals[] = {
    "pending", "uploading", "processing", "failed", "complete"};
static constexpr const char* kAxisLiterals[] = {"horizontal", "vertical"};
static constexpr const char* kAlignmentLiterals[] = {"start", "end"};
static constexpr const char* kBubbleVariantLiterals[] = {
    "filled",  "secondary", "muted",      "tinted",
    "outline", "ghost",     "destructive"};
static constexpr const char* kMarkerVariantLiterals[] = {"plain", "separator",
                                                         "border"};
static constexpr const char* kLoadingStyleLiterals[] = {"spinner", "shimmer"};

static bool RecordAttachmentStatus(PayloadBuild* b,
                                   const ComponentArgument* args, int count) {
    return RecordEnum<Op::Status>(b, args, count, "Attachment", "status",
                                  kStatusLiterals);
}
static bool RecordAttachmentAxis(PayloadBuild* b, const ComponentArgument* args,
                                 int count) {
    return RecordEnum<Op::Axis>(b, args, count, "Attachment", "axis",
                                kAxisLiterals);
}
static bool RecordAttachmentSize(PayloadBuild* b, const ComponentArgument* args,
                                 int count) {
    return RecordEnum<Op::Size>(b, args, count, "Attachment", "size",
                                kSizeLiterals);
}
static bool RecordBubbleAlignment(PayloadBuild* b,
                                  const ComponentArgument* args, int count) {
    return RecordEnum<Op::Alignment>(b, args, count, "Bubble", "alignment",
                                     kAlignmentLiterals);
}
static bool RecordBubbleVariant(PayloadBuild* b, const ComponentArgument* args,
                                int count) {
    return RecordEnum<Op::BubbleVariant>(b, args, count, "Bubble", "variant",
                                         kBubbleVariantLiterals);
}
static bool RecordMarkerVariant(PayloadBuild* b, const ComponentArgument* args,
                                int count) {
    return RecordEnum<Op::MarkerVariant>(b, args, count, "Marker", "variant",
                                         kMarkerVariantLiterals);
}
static bool RecordMarkerLoading(PayloadBuild* b, const ComponentArgument* args,
                                int count) {
    return RecordBool<Op::Loading>(b, args, count, "Marker", "loading");
}
static bool RecordMarkerLoadingStyle(PayloadBuild* b,
                                     const ComponentArgument* args, int count) {
    return RecordEnum<Op::LoadingStyle>(b, args, count, "Marker",
                                        "loading_style", kLoadingStyleLiterals);
}
static bool RecordMessageAlignment(PayloadBuild* b,
                                   const ComponentArgument* args, int count) {
    return RecordEnum<Op::Alignment>(b, args, count, "Message", "alignment",
                                     kAlignmentLiterals);
}

static bool RecordShimmerId(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        !NonEmptyId(args[0].string))
        return build
            ->Fail(StrL("ShimmerText.id expects a non-empty stable id"));
    Op* op = build->New<Op>();
    op->kind = Op::Id;
    op->text = args[0].string;
    return true;
}

static bool RecordShimmerDuration(PayloadBuild* build,
                                  const ComponentArgument* args, int count) {
    // `*value <= u64::MAX as f64`: 2^64.
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number ||
        !isfinite(args[0].number) || args[0].number < 0.0 ||
        args[0].number > 18446744073709551616.0) {
        return build->Fail(StrL(
            "ShimmerText.duration_ms expects a finite non-negative duration"));
    }
    Op* op = build->New<Op>();
    op->kind = Op::Duration;
    // Duration::from_millis(value as u64): whole milliseconds.
    op->number = (float)floor(args[0].number);
    return true;
}

static bool RecordShimmerSpread(PayloadBuild* build,
                                const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number ||
        !isfinite(args[0].number) || args[0].number < -(double)FLT_MAX ||
        args[0].number > (double)FLT_MAX) {
        return build
            ->Fail(StrL("ShimmerText.spread expects a finite f32 fraction"));
    }
    Op* op = build->New<Op>();
    op->kind = Op::Spread;
    op->number = (float)args[0].number;
    return true;
}

static bool RecordShimmerReverse(PayloadBuild* b, const ComponentArgument* args,
                                 int count) {
    return RecordBool<Op::Reverse>(b, args, count, "ShimmerText", "reverse");
}
static bool RecordShimmerOnce(PayloadBuild* b, const ComponentArgument* args,
                              int count) {
    return RecordBool<Op::Once>(b, args, count, "ShimmerText", "once");
}
static bool RecordScrollerScrollbar(PayloadBuild* b,
                                    const ComponentArgument* args, int count) {
    return RecordBool<Op::Scrollbar>(b, args, count, "MessageScroller",
                                     "scrollbar");
}
static bool RecordScrollerJumpButton(PayloadBuild* b,
                                     const ComponentArgument* args, int count) {
    return RecordBool<Op::JumpButton>(b, args, count, "MessageScroller",
                                      "jump_button");
}

static bool RecordJumpButtonLabel(PayloadBuild* build,
                                  const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrim(args[0].string)) == 0) {
        return build->Fail(
            StrL("MessageScroller.jump_button_label expects non-empty text"));
    }
    Op* op = build->New<Op>();
    op->kind = Op::JumpButtonLabel;
    op->text = args[0].string;
    return true;
}

static bool NewMessageScrollerState(shell::StateBuild* build,
                                    const ComponentArgument* args, int count) {
    // `*value <= usize::MAX as f64`, and then an item count the state holds
    // one row height for: the rows here are indexed by int, so a count past
    // INT_MAX is refused where Rust would try to allocate it.
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number ||
        !isfinite(args[0].number) || args[0].number != floor(args[0].number) ||
        args[0].number < 0.0 || args[0].number > (double)INT_MAX) {
        return build->Fail(StrL(
            "MessageScrollerState expects a non-negative integer item_count"));
    }
    EntityState<component::MessageScrollerState>* state =
        build->New<EntityState<component::MessageScrollerState>>();
    state->app = build->app;
    state->entity = EntityNewState<component::MessageScrollerState>(build->app);
    component::MessageScrollerState::Init(state->entity.Get(build->app),
                                          (int)args[0].number);
    return true;
}

// ─── Descriptors ───────────────────────────────────────────────────────────

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kTextArgs[] = {{"text", SchemaString()}};

static constexpr ArgumentDescriptor kStatusArgs[] = {
    {"status", SchemaEnum(kStatusLiterals)}};
static constexpr ArgumentDescriptor kAxisArgs[] = {
    {"axis", SchemaEnum(kAxisLiterals)}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kAlignmentArgs[] = {
    {"alignment", SchemaEnum(kAlignmentLiterals)}};
static constexpr ArgumentDescriptor kBubbleVariantArgs[] = {
    {"variant", SchemaEnum(kBubbleVariantLiterals)}};
static constexpr ArgumentDescriptor kMarkerVariantArgs[] = {
    {"variant", SchemaEnum(kMarkerVariantLiterals)}};
static constexpr ArgumentDescriptor kLoadingArgs[] = {
    {"loading", SchemaBoolean()}};
static constexpr ArgumentDescriptor kLoadingStyleArgs[] = {
    {"loading_style", SchemaEnum(kLoadingStyleLiterals)}};
static constexpr ArgumentDescriptor kDurationArgs[] = {
    {"duration_ms", SchemaNumber()}};
static constexpr ArgumentDescriptor kSpreadArgs[] = {
    {"spread", SchemaNumber()}};
static constexpr ArgumentDescriptor kReverseArgs[] = {
    {"reverse", SchemaBoolean()}};
static constexpr ArgumentDescriptor kOnceArgs[] = {{"once", SchemaBoolean()}};
static constexpr ArgumentDescriptor kScrollbarArgs[] = {
    {"scrollbar", SchemaBoolean()}};
static constexpr ArgumentDescriptor kJumpButtonArgs[] = {
    {"jump_button", SchemaBoolean()}};
static constexpr ArgumentDescriptor kJumpButtonLabelArgs[] = {
    {"jump_button_label", SchemaString()}};

// Attachment
static constexpr ConstructorDescriptor kAttachmentConstructors[] = {
    {"Attachment", kIdArgs, &ConstructAttachment}};
static constexpr MethodDescriptor kAttachmentMethods[] = {
    {"status", kStatusArgs, "Sets the attachment lifecycle status.",
     &RecordAttachmentStatus},
    {"axis", kAxisArgs, "Sets the attachment layout axis.",
     &RecordAttachmentAxis},
    {"size", kSizeArgs, "Sets the semantic attachment size.",
     &RecordAttachmentSize},
};
static constexpr ComponentDescriptor kAttachment = {
    "Attachment", kAttachmentConstructors, kAttachmentMethods,
    "A file or image attachment. Ordinary children are composed into its "
    "metadata content slot.",
    &Materialize};

// Bubble
static constexpr ConstructorDescriptor kBubbleConstructors[] = {
    {"Bubble", {}, &ConstructNoArg<Payload::Bubble>}};
static constexpr MethodDescriptor kBubbleMethods[] = {
    {"alignment", kAlignmentArgs, "Sets the message-edge alignment.",
     &RecordBubbleAlignment},
    {"variant", kBubbleVariantArgs, "Sets the semantic bubble treatment.",
     &RecordBubbleVariant},
};
static constexpr ComponentDescriptor kBubble = {
    "Bubble", kBubbleConstructors, kBubbleMethods,
    "A message bubble whose ordinary children form its visible content.",
    &Materialize};

// Marker
static constexpr ConstructorDescriptor kMarkerConstructors[] = {
    {"Marker", kIdArgs, &ConstructMarker}};
static constexpr MethodDescriptor kMarkerMethods[] = {
    {"variant", kMarkerVariantArgs, "Sets the marker treatment.",
     &RecordMarkerVariant},
    {"loading", kLoadingArgs,
     "Sets whether the marker displays a loading treatment.",
     &RecordMarkerLoading},
    {"loading_style", kLoadingStyleArgs, "Sets the loading treatment.",
     &RecordMarkerLoadingStyle},
};
static constexpr ComponentDescriptor kMarker = {
    "Marker", kMarkerConstructors, kMarkerMethods,
    "A compact conversation status marker with composable children.",
    &Materialize};

// Message
static constexpr ConstructorDescriptor kMessageConstructors[] = {
    {"Message", {}, &ConstructNoArg<Payload::Message>}};
static constexpr MethodDescriptor kMessageMethods[] = {
    {"alignment", kAlignmentArgs, "Sets the sender-edge alignment.",
     &RecordMessageAlignment},
};
static constexpr ComponentDescriptor kMessage = {
    "Message", kMessageConstructors, kMessageMethods,
    "A message row. Ordinary children are composed into its content slot.",
    &Materialize};

// ShimmerText
static constexpr ConstructorDescriptor kShimmerConstructors[] = {
    {"ShimmerText", kTextArgs, &ConstructShimmerText}};
static constexpr MethodDescriptor kShimmerMethods[] = {
    {"id", kIdArgs, "Sets an explicit stable animation identity.",
     &RecordShimmerId},
    {"duration_ms", kDurationArgs,
     "Sets one shimmer sweep duration in milliseconds.",
     &RecordShimmerDuration},
    {"spread", kSpreadArgs, "Sets the relative highlight half-width.",
     &RecordShimmerSpread},
    {"reverse", kReverseArgs, "Reverses the shimmer direction.",
     &RecordShimmerReverse},
    {"once", kOnceArgs, "Runs one sweep instead of looping.",
     &RecordShimmerOnce},
};
static constexpr ComponentDescriptor kShimmerText = {
    "ShimmerText", kShimmerConstructors, kShimmerMethods,
    "Theme-aware animated loading text.", &Materialize};

// MessageScrollerState
static constexpr ArgumentDescriptor kStateArgs[] = {
    {"item_count", SchemaNumber()}};
static constexpr shell::StateDescriptor kMessageScrollerState = {
    "MessageScrollerState",
    "MessageScrollerState",
    kStateArgs,
    "Retained virtual-list and tail-following state for a message transcript.",
    &NewMessageScrollerState,
    {}};

// MessageScroller
static constexpr ArgumentDescriptor kScrollerArgs[] = {
    {"id", SchemaString()},
    {"state", SchemaEntity("MessageScrollerState")},
    {"render_item", SchemaCallback("(index: number) => Element | null")}};
static constexpr ConstructorDescriptor kScrollerConstructors[] = {
    {"MessageScroller", kScrollerArgs, &ConstructMessageScroller}};
static constexpr MethodDescriptor kScrollerMethods[] = {
    {"scrollbar", kScrollbarArgs, "Enables its virtual-list scrollbar.",
     &RecordScrollerScrollbar},
    {"jump_button", kJumpButtonArgs, "Enables the jump-to-latest button.",
     &RecordScrollerJumpButton},
    {"jump_button_label", kJumpButtonLabelArgs,
     "Sets the jump-to-latest button label.", &RecordJumpButtonLabel},
};
static constexpr ComponentDescriptor kMessageScroller = {
    "MessageScroller", kScrollerConstructors, kScrollerMethods,
    "A virtualized message transcript with retained scroll and "
    "tail-following state.",
    &Materialize};

} // namespace gpui::component_shell::chat

namespace gpui::component_shell {

bool RegisterChat(shell::ComponentRegistry* registry,
                  shell::RegistryError* error) {
    return registry->Register(&chat::kAttachment, error) &&
           registry->Register(&chat::kBubble, error) &&
           registry->Register(&chat::kMarker, error) &&
           registry->Register(&chat::kMessage, error) &&
           registry->Register(&chat::kShimmerText, error) &&
           registry->RegisterState(&chat::kMessageScrollerState, error) &&
           registry->Register(&chat::kMessageScroller, error);
}

} // namespace gpui::component_shell
