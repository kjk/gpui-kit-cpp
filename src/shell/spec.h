#ifndef GPUI_SHELL_SPEC_H_
#define GPUI_SHELL_SPEC_H_

#include "shell/value.h"

namespace gpui::shell {

using SpecId = uint32_t;
using CallbackId = uint64_t;

// A hash of a description's *shape*, with the values left out of it.
//
// Two renders of one view that differ only in a price, a label or a handler's
// identity produce the same fingerprint. One that takes a different branch,
// grows a node, or calls a different style method does not. That distinction
// is the whole question a template cache turns on, and answering it is the
// reason this exists: a description that repeats its predecessor's shape is
// one a template could have filled instead of rebuilt.
//
// It is accumulated while the description is recorded rather than walked out
// of the arena afterwards, because a walk that costs the arena's length is the
// cost a template cache is trying to remove.
//
// Equality here is evidence, not proof. Two different shapes can collide, and
// the hash deliberately drops payloads a stricter definition might keep. That
// is the right trade for a counter; it would not be for a cache that skipped
// work on the strength of it.
struct StructureFingerprint {
    uint64_t value = 0;

    bool operator==(const StructureFingerprint& other) const {
        return value == other.value;
    }
    bool operator!=(const StructureFingerprint& other) const {
        return value != other.value;
    }
};

// One step of the fingerprint's mixer: SplitMix64's finalizer with the running
// state rotated in. Rust uses the same constants; a `DefaultHasher` would do
// the same job, but this sits on the recording path.
uint64_t StructureMix(uint64_t state, uint64_t value);

enum class BackgroundKind : uint8_t {
    Solid,
    LinearGradient,
    PatternSlash,
    Checkerboard,
};

struct BackgroundSpec {
    BackgroundKind kind = BackgroundKind::Solid;
    Str color;
    float opacity = 1;
    float angle = 0;
    Str fromColor;
    float fromPosition = 0;
    Str toColor;
    float toPosition = 1;
    Str colorSpace;
    float width = 0;
    float interval = 0;
    float size = 0;
};

enum class ComponentKind : uint8_t {
    Div,
    HFlex,
    VFlex,
    ChildView,
    Text,
    Button,
    Link,
    Checkbox,
    Switch,
    Scrollbar,
    Input,
    Textarea,
    NumberInput,
    OtpInput,
    Svg,
    // An accordion root: a group, and nothing else on screen.
    Accordion,
    // One item: it connects a header with a panel and passes its own `open`
    // down to both, which is the whole of what it does.
    AccordionItem,
    AccordionHeader,
    // The region an item reveals. Unmounted while shut unless
    // `keep_mounted(true)` says otherwise.
    AccordionPanel,
    AccordionTrigger,
    // A pagination root: a navigation landmark carrying the announced label.
    // The page buttons are the script's; the ellipsis layout is the free
    // function `pagination_items(...)` rather than a component.
    Pagination,
    // An avatar root: it renders its `image` slot, or its `fallback` slot
    // when there is no image, and nothing else.
    Avatar,
    // The image slot, a component of its own because base's `Avatar::image`
    // takes an AvatarImage rather than an element — the slot has to be
    // resolved back into that type, which needs the path.
    AvatarImage,
    AvatarFallback,
    Image,
    PathFill,
    PathStroke,
    Tabs,
    Tab,
    Progress,
    ProgressTrack,
    ProgressIndicator,
    FpsMonitor,
    Slider,
    SliderTrack,
    SliderIndicator,
    SliderThumb,
    Radio,
    Toggle,
    RadioGroup,
    ToggleGroup,
    Table,
    TableHeader,
    TableBody,
    TableRow,
    TableHead,
    TableCell,
    TableCaption,
    HResizable,
    VResizable,
    ResizablePanel,
    Collapsible,
    Popover,
    HoverCard,
    Popup,
    Select,
    Combobox,
    DatePicker,
    // A dockable layout, addressed by its entity handle for the reason Input
    // is: the layout is the state, it outlives every description, and the
    // user changes it without a script render. Nothing under it is
    // described — its panels are entities the script handed it, and its
    // chrome is drawn by the handlers this node carries.
    DockArea,
    // Where a dock's own content goes inside the chrome the script drew
    // around it. Base hands the content to the chrome as a finished element
    // and keeps whatever comes back, so a chrome that wants both has to place
    // the content itself; an element cannot cross into script, so this stands
    // in for it.
    DockContent,
    VVirtualList,
    HVirtualList,
    // GPUI's own lazy lists, driven the same way as the virtual lists: the
    // items come from a callback run during layout. `list` measures every
    // item it draws, so rows need not state a height; `uniform_list`
    // measures one and places the rest by it. Rust's `Component::List` holds
    // a `ListKind`; here the kind is the component kind, the way the two
    // virtual list axes are.
    List,
    UniformList,
};

struct VirtualListSpec {
    Str id;
    Axis axis = Axis::Vertical;
    Size* sizes = nullptr;
    int sizeCount = 0;
    CallbackId getKey = 0;
    CallbackId renderItems = 0;
};

// The parameters of a `list` or `uniform_list` call, held for the frame: the
// same shape as a VirtualListSpec without the size table, since GPUI measures
// the items itself and the script says only how many there are.
struct ListSpec {
    // The identity the script gave, also the name a `Scrollbar` pairs with.
    Str id;
    // How many items the collection has, visible or not.
    int itemCount = 0;
    // Resolves the stable domain key for one current item index.
    CallbackId getKey = 0;
    // The handler that describes one window of items.
    CallbackId renderItems = 0;
};

struct Component {
    ComponentKind kind = ComponentKind::Div;
    Str text;
    uint64_t handle = 0;
    uint32_t index = 0;
    BackgroundSpec background;
    float strokeWidth = 0;
    VirtualListSpec* virtualList = nullptr;
    ListSpec* list = nullptr;
};

const char* ComponentName(const Component& component);

enum class SpecOpKind : uint8_t {
    NullaryStyle,
    ParamStyle,
    Method,
    Callback,
    // A handler for one named action. Its own kind rather than a Callback
    // because the name it carries is the script's, discovered at run time,
    // while a Callback's name is one of a fixed set.
    ActionCallback,
    StateStyle,
    Slot,
};

struct SpecOp {
    SpecOpKind kind = SpecOpKind::Method;
    Str name;
    uint16_t styleIndex = 0;
    CallbackId callback = 0;
    SpecId node = 0;
    Bridged* args = nullptr;
    int argCount = 0;
};

struct SpecNode {
    Component component;
    ArenaVec<SpecOp> ops;
    ArenaVec<SpecId> children;
};

enum class SpecErrorKind : uint8_t {
    None,
    Claimed,
    Expired,
    AlreadyParented,
    SelfParent,
    DuplicateChildView,
};

struct SpecError {
    SpecErrorKind kind = SpecErrorKind::None;
    Str component;
};

Str SpecErrorMessage(Arena* arena, const SpecError& error);

// Where inside a node a template writes one of a call's arguments.
//
// The three positions a value can reach in a recorded description, and the
// only three a Template fills. A slot is addressed by the operation's index
// rather than by its name because a node may record the same method twice, and
// the second one is a different position.
enum class SlotSiteKind : uint8_t {
    // The string a Text node carries — what `.child(value)` records.
    Text,
    // One argument of a recorded ParamStyle operation.
    Argument,
    // The CallbackId of a recorded Callback operation. A handler is a slot
    // like any other, but the value written into it is minted per call rather
    // than carried — which is why a template does not make a handler free,
    // only the structure around it.
    Handler,
};

struct SlotSite {
    SlotSiteKind kind = SlotSiteKind::Text;
    uint16_t op = 0;
    uint8_t argument = 0;

    bool operator==(const SlotSite& other) const {
        return kind == other.kind && op == other.op &&
               argument == other.argument;
    }
};

// One position a template fills, and which of the call's arguments fills it.
struct Slot {
    SpecId node = 0;
    SlotSite site;
    // The index of the template parameter whose sentinel came to rest here.
    uint16_t argument = 0;
};

enum class SlotValueKind : uint8_t {
    Text,
    Value,
    Handler
};

// What a call writes into one slot.
struct SlotValue {
    SlotValueKind kind = SlotValueKind::Text;
    Str text;
    Bridged value;
    CallbackId handler = 0;
};

class SpecArena;

// A description recorded once, with the positions its values occupy left open.
//
// Built by running a script's template body a single time with a sentinel in
// each parameter position, and used afterwards by grafting it into the live
// arena and writing that call's arguments into its slots — which is the whole
// of an instantiation, and runs no script at all.
//
// It holds no CallbackId of its own: a handler is a slot, minted per call,
// because a closure recorded at discovery would capture that first call's
// values for as long as the template lived.
struct Template {
    SpecArena* arena = nullptr;
    SpecId root = 0;
    Vec<Slot> slots;
    int arity = 0;
    // The application whose script defined it. A template outlives every
    // render, which is the point of it, so nothing else would ever free one:
    // the store would grow by one entry per `template(...)` call site per hot
    // reload, forever. Holding the owner lets the same release that retires an
    // application's callbacks and tasks drop its templates too. Null only for
    // a runtime that has no application generation at all, which is a test.
    void* application = nullptr;

    ~Template();
};

class SpecArena {
  public:
    SpecArena();
    // A description whose strings live in an arena someone else owns — the
    // frame arena, for an item batch whose elements outlive the batch. Reset
    // leaves that arena alone.
    explicit SpecArena(Arena* borrowed);
    SpecArena(const SpecArena&) = delete;
    SpecArena& operator=(const SpecArena&) = delete;
    ~SpecArena();

    void Reset();
    int Len() const { return nodes.len; }
    bool IsEmpty() const { return nodes.len == 0; }
    SpecId Push(const Component& component);
    bool PushChildView(const Component& component, SpecId* out,
                       SpecError* error = nullptr);
    // The same rule and the same table as a child view's, because it is the
    // same rule: one entity cannot be mounted at two positions in a tree, and
    // a dock area is an entity.
    bool PushDockArea(uint64_t handle, SpecId* out, SpecError* error = nullptr);
    const SpecNode* Node(SpecId id) const;
    bool PushOp(SpecId id, const SpecOp& op, SpecError* error = nullptr);
    bool Claim(SpecId id, SpecError* error = nullptr);
    bool Attach(SpecId parent, SpecId child, SpecError* error = nullptr);
    bool ClaimVirtualItems(uint64_t count, uint64_t limit);
    Str DebugTree(Arena* into, SpecId root) const;

    // The shape of what has been recorded, values excluded. Read from a
    // published snapshot rather than from the scratch arena: the scratch one
    // is reset by the next render, and the question is what *this* description
    // looked like beside the one before it.
    StructureFingerprint Structure() const { return {structure}; }

    // Copies a template's nodes into this arena and answers where its root
    // landed. This is an instantiation's whole structural half: no script
    // runs, no value crosses the bridge, and no builder method is interpreted.
    // A template arena's ids are dense and start at zero, so remapping is one
    // addition.
    //
    // The grafted nodes arrive carrying the `parented` and `claimed` flags
    // they were recorded with, which is what makes a grafted subtree obey the
    // same single-use rule as a described one: its interior is already spoken
    // for, and only its root is free to be attached.
    SpecId Graft(const Template& tmpl);

    // Writes one call's value into a grafted slot. `base` is what Graft
    // returned less the template's own root, so that a slot recorded against
    // the template's ids reaches the copy.
    bool WriteSlot(SpecId base, const Slot& slot, const SlotValue& value,
                   SpecError* error = nullptr);

    // Whether anything in this arena mounts a retained entity. A template is
    // grafted many times and GPUI cannot mount one entity at two positions in
    // a tree, so a body that describes one is refused at definition rather
    // than at the second call.
    bool MountsAnEntity() const { return mountedViews.len > 0; }

  private:
    Arena* arena = nullptr;
    bool ownsArena = true;
    Vec<SpecNode*> nodes;
    Vec<uint8_t> parented;
    Vec<uint8_t> claimed;
    Vec<uint64_t> mountedViews;
    uint64_t virtualItems = 0;
    // The shape of everything recorded so far. See StructureFingerprint.
    uint64_t structure = 0;

    bool CheckLive(SpecId id, SpecError* error) const;
    Component CopyComponent(const Component& component);
    SpecOp CopyOp(const SpecOp& op);
    void WriteTree(StrBuilder* out, SpecId id, int depth) const;
};

} // namespace gpui::shell
#endif // GPUI_SHELL_SPEC_H_
