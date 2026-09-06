#ifndef GPUI_BASE_NAV_STACK_H_
#define GPUI_BASE_NAV_STACK_H_
/* Unstyled navigation stack — crates/base/src/nav_stack.rs

   A stack of views, one visible at a time, with the pages popped off it kept
   for Forward. This is SwiftUI's NavigationStack, Qt's StackView and WinUI's
   Frame. Underneath it is a History of views — entries run from root to
   current, and a popped page waits on the forward side until a push discards
   it, which is what WinUI's BackStack and ForwardStack do.

   Rust's AnyView is an EntityId here: the runtime renders an entity into the
   frame arena with EntityRender, which is the whole of what NavStack needs a
   view for. */

#include "base/history.h"
#include "base/motion.h"

namespace gpui {

// What a running transition is doing, in Qt's terms. The operation decides
// paint order and lets a renderer move a pushed view differently from a popped
// one.
enum class NavOperation : uint8_t {
    Push,
    Pop,
    Replace
};

// Whether one change runs the NavStack's transition, as UIKit's `animated:`
// and Qt's StackView.Immediate decide per call. Immediate switches views on
// the spot even when the element has a transition, which is what restoring a
// stack at launch or jumping to a page from a command wants. A NavStack
// without a transition is always immediate, whatever is passed here.
enum class NavMotion : uint8_t {
    Animated,
    Immediate
};

// Emitted by NavStackState after the stack changed.
enum class NavStackEvent : uint8_t {
    Pushed,
    Popped,
    Forwarded,
    Replaced,
    Cleared
};

// A page pushed twice is two separate entries.
struct NavEntry {
    EntityId view = {};
};

// The view leaving the stack, kept mounted until its exit transition finishes.
// The element samples that transition as a presence keyed by the view, so an
// interrupted change reverses from where it is.
struct NavTransit {
    EntityId outgoing = {};
    // The position the outgoing view had on the stack.
    int index = 0;
    NavOperation operation = NavOperation::Push;
    NavMotion motion = NavMotion::Animated;
};

// The stack owns which view is current and the lifecycle of a change: after a
// push, pop or replace, the outgoing view stays mounted until the NavStack's
// transition finishes, so the application can animate it. The views
// themselves, and what a transition looks like, belong to the application.
//
// Pop keeps the root, as Qt's StackView and UIKit's navigation controller do;
// Clear is the way to empty the stack. A back button is shown when Depth() > 1,
// a forward button when ForwardCount() is not zero.
struct NavStackState {
    History<NavEntry> history;
    NavTransit transit = {};
    bool hasTransit = false;
    // cx.emit needs to know who is emitting, and Rust's Context<Self> does.
    // NavStackStateNew stamps it; the element stamps it again, so a state made
    // by hand still emits once it has been rendered.
    Entity<NavStackState> self = {};

    // The number of views on the stack.
    int Depth() const { return history.Entries().len; }
    bool IsEmpty() const { return history.Entries().len == 0; }

    // The view on top of the stack, which is the one shown once any transition
    // has finished. An invalid EntityId is Rust's None.
    EntityId Current() const {
        const NavEntry* entry = history.Current();
        return entry ? entry->view : EntityId{};
    }

    // Every view on the stack, root first.
    EntityId ViewAt(int index) const {
        const Vec<NavEntry>& undos = history.Entries();
        return index >= 0 && index < undos.len ? undos[index].view : EntityId{};
    }

    // The views popped since the last push, nearest first: the one Forward
    // would bring back is the first.
    int ForwardCount() const { return history.ForwardEntries().len; }
    EntityId ForwardViewAt(int index) const {
        auto entries = history.ForwardEntries();
        return index >= 0 && index < entries.len ? entries[index].view
                                                 : EntityId{};
    }
};

// cx.new(|_| NavStackState::new()), with the handle stamped on the state so it
// can emit before anything has rendered it.
Entity<NavStackState> NavStackStateNew(App* app);

// Pushes `view` on top of the stack and discards the forward views.
//
// Into an empty stack this is immediate, like Qt's `initialItem`. Over an
// existing top it starts a NavOperation::Push transition, unless `motion` is
// NavMotion::Immediate.
void NavStackPush(NavStackState* s, Ctx* cx, EntityId view, NavMotion motion);

// Pops the top view and returns it, starting a NavOperation::Pop transition to
// the view below. The view waits in the forward views. The root is never
// popped: this answers an invalid EntityId at a depth of one or less.
EntityId NavStackPop(NavStackState* s, Ctx* cx, NavMotion motion);

// Pops every view above the root in one NavOperation::Pop transition from the
// previous top, and returns them root-side first.
Vec<EntityId> NavStackPopToRoot(NavStackState* s, Ctx* cx, NavMotion motion);

// Brings back the most recently popped view, starting a NavOperation::Push
// transition over the current top, and returns it. Invalid when nothing has
// been popped since the last push.
EntityId NavStackForward(NavStackState* s, Ctx* cx, NavMotion motion);

// Swaps the top view for `view` and returns the one replaced, starting a
// NavOperation::Replace transition. The forward views are kept. On an empty
// stack this is a push, and the answer is invalid.
EntityId NavStackReplace(NavStackState* s, Ctx* cx, EntityId view,
                         NavMotion motion);

// Empties the stack and the forward views immediately, abandoning any running
// transition.
void NavStackClear(NavStackState* s, Ctx* cx);

// One mounted view of a NavStack, handed to the item renderer.
//
// The item fills its container. Its readers describe where the view is in the
// change that is running, so the renderer can move it: `phase` says whether it
// is arriving, settled, or leaving; `operation` says which change, and
// `hasOperation` is Rust's Option discriminator; `progress` runs from 0 to 1
// over the transition, already eased, and is shared by both views of one
// change.
//
// `el` is the element the stack already built for the view — absolute and
// filling the container, with the view as its child. Rust refines a
// StyleRefinement onto the NavPage element; here the renderer refines that El
// and hands it back.
struct NavPage {
    EntityId view = {};
    int index = 0;
    PresencePhase phase = PresencePhase::Present;
    NavOperation operation = NavOperation::Push;
    bool hasOperation = false;
    float progress = 1.f;
    El* el = nullptr;

    // The view's position on the stack, root first. A view on its way out
    // keeps the position it had.
    int Index() const { return index; }
    PresencePhase Phase() const { return phase; }
    bool HasOperation() const { return hasOperation; }
    NavOperation Operation() const { return operation; }
    float Progress() const { return progress; }
};

// Rust's `item(impl Fn(NavPage, ..) -> AnyElement)`. An element in this tree
// holds no closures, so the renderer is a function plus the user pointer it
// would have captured.
using NavItemFn = El* (*)(void* user, Ctx* cx, const NavPage& page);

// An unstyled host for a NavStackState.
//
// The container is positioned so that the two views of a transition can
// overlap; each mounted view is handed to the `item` renderer as a NavPage that
// already fills the container. Everything else — size, clipping, background,
// and how a transition moves — is the application's, chained onto the El that
// IntoEl answers.
//
// Without a transition the stack switches views immediately, as it also does
// under reduced motion.
struct NavStack {
    Ctx* cx = nullptr;
    Entity<NavStackState> state = {};
    motion::Transition transition = {};
    bool hasTransition = false;
    NavItemFn item = nullptr;
    void* user = nullptr;

    static NavStack* New(Ctx* cx, Entity<NavStackState> state);
    // The timing every push, pop and replace runs under.
    NavStack* Transition(const motion::Transition& value);
    // Renders each mounted view. The item is already positioned to fill the
    // container; refine it to move or fade the view by its phase and progress,
    // then return it.
    NavStack* Item(NavItemFn fn, void* user = nullptr);
    El* IntoEl();
};

template <>
struct EventEmitter<NavStackState, NavStackEvent> {};

} // namespace gpui
#endif // GPUI_BASE_NAV_STACK_H_
