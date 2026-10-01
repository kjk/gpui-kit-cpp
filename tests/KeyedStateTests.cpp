/* Runtime seam behind Window::use_keyed_state. The component WindowExt store
 * relies on its object and owned Vec/entity handles dying with the window. */

#include "Test.h"

namespace {
struct KeyedLifetime {
    static int made;
    static int dropped;
    int initial = 73;

    KeyedLifetime() { made++; }
    ~KeyedLifetime() { dropped++; }
};
int KeyedLifetime::made = 0;
int KeyedLifetime::dropped = 0;
} // namespace

static void TheFirstObjectIsKeptAndLaterCandidatesAreDropped() {
    KeyedLifetime::made = 0;
    KeyedLifetime::dropped = 0;
    Window window;
    Ctx cx = {};
    cx.win = &window;

    KeyedLifetime* first = KeyedState<KeyedLifetime>(&cx, 41);
    KeyedLifetime* again = KeyedState<KeyedLifetime>(&cx, 41);
    utassert(first != nullptr && again == first);
    utassert(first->initial == 73);
    utassert(KeyedLifetime::made == 2);
    utassert(KeyedLifetime::dropped == 1);

    WindowKeyedFree(&window);
    utassert(KeyedLifetime::dropped == 2);
    utassert(window.keyed.len == 0);
}

// UseLaidOutHeight: a virtual list's viewport is last frame's laid-out
// height, keyed like element state. The first frame builds with the
// fallback; a laid-out height that differs asks for exactly one more frame,
// and a height that follows the number it was built with stops asking.
static void ALaidOutHeightIsLastFramesAndAsksForOneMoreFrame() {
    LaidOutHeight slot;
    slot.built = 320;
    utassert(LaidOutHeightObserve(&slot, 540));
    utassertnear(slot.measured, 540.f);
    // Rebuilt with it, the same height settles.
    slot.built = 540;
    utassert(!LaidOutHeightObserve(&slot, 540));
    utassert(slot.chase == 0);
    // A fraction of a DIP is not another frame.
    utassert(!LaidOutHeightObserve(&slot, 540.3f));
    // A box that grows with what it was built with is chased once, not
    // forever.
    slot.built = 100;
    utassert(LaidOutHeightObserve(&slot, 132));
    slot.built = 132;
    utassert(!LaidOutHeightObserve(&slot, 164));
    // The inset comes off what is recorded.
    LaidOutHeight table;
    table.built = 200;
    table.inset = 34;
    utassert(!LaidOutHeightObserve(&table, 234));
    utassertnear(table.measured, 200.f);
    utassert(!LaidOutHeightObserve(nullptr, 10));
}

static void AnElementRecordsItsLaidOutHeightAtPrepaint() {
    App app;
    Window window;
    window.app = &app;
    Arena* a = ArenaNew();
    Ctx cx{&app, &window, a, {}};

    LaidOutHeight* first = UseLaidOutHeight(&cx, StrL("tree"), 320);
    utassert(first && first->built == 320);
    El* box = Div(a)->H(kFill)->Pad(4)->Border(1, Rgba8(0, 0, 0, 255));
    first->contentBox = true;
    utassert(TrackLaidOutHeight(&cx, box, first));
    // One prepaint per element: a box that already has one is left alone.
    utassert(!TrackLaidOutHeight(&cx, box, first));
    LayoutEl(nullptr, box, 0, 0, 200, 500, 14, Rgba{});
    utassertnear(box->h, 500.f);
    PaintCtx paint = {};
    box->prePaint(&paint, box, box->customUser);
    // 500 less 4 + 4 of padding and 1 + 1 of border.
    utassertnear(first->measured, 490.f);
    utassert(first->chase == 1);

    // The next frame builds with it, under the same name and scope; another
    // name is another slot.
    LaidOutHeight* next = UseLaidOutHeight(&cx, StrL("tree"), 320);
    utassert(next == first && next->built == 490.f);
    utassert(!next->contentBox);
    LaidOutHeight* other = UseLaidOutHeight(&cx, StrL("list"), 320);
    utassert(other != first && other->built == 320);
    // No window, no slot: the caller keeps its own number.
    Ctx bare{&app, nullptr, a, {}};
    utassert(UseLaidOutHeight(&bare, StrL("tree"), 320) == nullptr);

    WindowKeyedFree(&window);
    ArenaDelete(a);
}

void TestKeyedState() {
    TestSuite("keyed state");
    TheFirstObjectIsKeptAndLaterCandidatesAreDropped();
    ALaidOutHeightIsLastFramesAndAsksForOneMoreFrame();
    AnElementRecordsItsLaidOutHeightAtPrepaint();
}
