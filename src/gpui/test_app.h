#ifndef GPUI_GPUI_TEST_APP_H_
#define GPUI_GPUI_TEST_APP_H_
/* gpui's TestAppContext and VisualTestContext: an App and its windows with no
   OS window behind them, driven the way a test drives Rust's TestPlatform.

   What is real: the element tree, layout, paint (every element runs its
   prepaint and paint and writes back what it measured), hit-testing, focus,
   the keymap and action dispatch, entities and notify, and the platform's
   own text system, which shapes and measures without a window. What is
   simulated: the surface (a frame binds no target, so every drawing call
   draws nothing — WindowDrawFrameHeadless), the clock (frozen until
   TestAdvanceClock moves it), and the clipboard (in memory). A window is
   1920×1080 at scale 2 unless the test says otherwise, which is what Rust's
   TestDisplay and TestWindow answer.

   Rust draws every invalidated window when an update's effects are flushed,
   which in a test is the end of every `update`, every simulated event and
   every `run_until_parked`. Each entry point below that stands for one of
   those ends the same way; a test that changes state directly, the way a
   Rust test does inside `window.update(cx, ..)`, calls TestFlushEffects.

   One test app at a time: the clock and the clipboard it installs are the
   process's (TestPlatformNow / TestPlatformClipboard* in gpui.h). */

#include "gpui/gpui.h"

namespace gpui {

// TestAppContext::new: an App with the platform text system and the image
// store, no OS initialisation and no window. Installs the simulated clock and
// the in-memory clipboard until TestAppFree.
App* TestAppNew();
void TestAppFree(App* app);

// cx.open_window(size, |window, cx| root): a window rendering `root`, drawn
// once before this returns. `scale` is the window's scale factor, the 2.0 a
// Rust TestWindow reports.
Window* TestWindowOpen(App* app, EntityId root, float w = 1920, float h = 1080,
                       float scale = 2);
template <typename T>
Window* TestWindowOpen(App* app, Entity<T> root, float w = 1920, float h = 1080,
                       float scale = 2) {
    return TestWindowOpen(app, root.id, w, h, scale);
}

// window.draw(cx).clear(cx): one frame, whether or not anything asked for it.
void TestDraw(Window* win);
// The end of an update: every window something invalidated since its last
// frame is drawn again. A notify sent from inside a frame does not ask for
// another one, as in Rust.
void TestFlushEffects(App* app);
// cx.simulate_resize(size): the new size, and the frame it brings.
void TestSimulateResize(Window* win, float w, float h);

// cx.run_until_parked(): run the posted main-thread work, wait for the
// background work and run what it posts back, fire every timer that is due
// on the simulated clock, and draw what any of that invalidated — until
// nothing is left. The clock does not move.
void TestRunUntilParked(App* app);
// cx.executor().advance_clock(duration): move the simulated clock forward,
// firing each timer as the clock reaches it, then run until parked.
void TestAdvanceClock(App* app, double ms);
// What TimeNow answers while the test app is installed.
double TestClockNow();

// cx.simulate_keystrokes("cmd-a backspace"): each space-separated keystroke,
// parsed the way a binding is (gpui/keymap.h KeyChordParse: `cmd-` is the
// platform key, `secondary-` the platform's shortcut modifier), pressed in
// turn. A keystroke that types — a printable key with no ctrl, alt, cmd or fn
// — also delivers its character, the simulated IME of Rust's
// Keystroke::with_simulated_ime. Runs until parked afterwards.
void TestSimulateKeystrokes(Window* win, const char* keystrokes);
// cx.simulate_input("abc"): each character typed in turn.
void TestSimulateInput(Window* win, Str text);

// VisualTestContext::simulate_mouse_*: one event at `position`. A move with
// `pressed` carries `button` as its pressed button. Each runs until parked.
void TestSimulateMouseMove(Window* win, Point position, bool pressed = false,
                           MouseButton button = MouseButton::Left,
                           Modifiers modifiers = {});
void TestSimulateMouseDown(Window* win, Point position,
                           MouseButton button = MouseButton::Left,
                           Modifiers modifiers = {});
void TestSimulateMouseUp(Window* win, Point position,
                         MouseButton button = MouseButton::Left,
                         Modifiers modifiers = {});
// A left press and its release at the same point.
void TestSimulateClick(Window* win, Point position, Modifiers modifiers = {});

// cx.dispatch_action(action): to whatever has the focus, the way a key bound
// to it would — the focused field first, then the element chain out from the
// focus, then the application's handlers. True when something took it.
bool TestDispatchAction(Window* win, uint32_t action, int64_t arg = 0);

// cx.write_to_clipboard / cx.read_from_clipboard.
void TestWriteToClipboard(Str text);
// The text, from `a`; empty when nothing was written.
Str TestReadFromClipboard(Arena* a);

// window.focus(&handle): focus moves to it and the window redraws.
void TestFocus(Window* win, FocusHandle handle);

} // namespace gpui
#endif // GPUI_GPUI_TEST_APP_H_
