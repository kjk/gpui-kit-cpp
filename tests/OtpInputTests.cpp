/* Ported from crates/base/src/otp_input.rs edit_value.
 *
 * Rust's own cases there drive a window; edit_value is the pure half and is
 * where the rules are — digits only, backspace pops, full-width digits fold
 * onto plain ones, and a full code refuses more. */

#include "Test.h"

struct OtpRecorder {
    OtpEventKind events[8] = {};
    int count = 0;

    static void OnEvent(OtpRecorder* self, Ctx*, const OtpEvent* ev) {
        if (self->count <
            (int)(sizeof(self->events) / sizeof(self->events[0]))) {
            self->events[self->count++] = ev->kind;
        }
    }
};

static void OnlyDigitsAreTaken() {
    OtpState s;
    utassert(OtpEditValue(&s, 0, '1'));
    utassert(OtpEditValue(&s, 0, '2'));
    utassert(base::StrEqI(OtpValue(&s), "12"));
    // A letter, a space and a symbol all leave the value where it was.
    utassert(!OtpEditValue(&s, 0, 'a'));
    utassert(!OtpEditValue(&s, 0, ' '));
    utassert(!OtpEditValue(&s, 0, '-'));
    utassert(base::StrEqI(OtpValue(&s), "12"));
}

static void FullWidthDigitsFoldOntoPlainOnes() {
    OtpState s;
    // U+FF13 and U+FF17, which an IME produces.
    utassert(OtpEditValue(&s, 0, 0xFF13));
    utassert(OtpEditValue(&s, 0, 0xFF17));
    utassert(base::StrEqI(OtpValue(&s), "37"));
    utassert(OtpDigitChar(0xFF10) == '0');
    utassert(OtpDigitChar(0xFF19) == '9');
    utassert(OtpDigitChar(0xFF1A) == 0);
}

static void BackspacePopsTheLastDigit() {
    OtpState s;
    OtpEditValue(&s, 0, '4');
    OtpEditValue(&s, 0, '5');
    utassert(OtpEditValue(&s, KeyBack, 0));
    utassert(base::StrEqI(OtpValue(&s), "4"));
    utassert(OtpEditValue(&s, KeyBack, 0));
    utassert(base::StrEqI(OtpValue(&s), ""));
    // Nothing left to pop.
    utassert(!OtpEditValue(&s, KeyBack, 0));
}

static void AFullCodeRefusesMore() {
    OtpState s;
    s.length = 4;
    utassert(OtpEditValue(&s, 0, '1'));
    utassert(OtpEditValue(&s, 0, '2'));
    utassert(OtpEditValue(&s, 0, '3'));
    utassert(OtpEditValue(&s, 0, '4'));
    utassert(base::StrEqI(OtpValue(&s), "1234"));
    // The run is not shifted along; the digit is simply dropped.
    utassert(!OtpEditValue(&s, 0, '5'));
    utassert(base::StrEqI(OtpValue(&s), "1234"));
    utassert(s.len == 4);
}

// OtpState::new takes any length and set_value any string: a code longer
// than 64 cells fills to its own length, and set_value keeps the whole of
// what it is handed.
static void ALongCodeFillsToItsOwnLength() {
    OtpState s;
    s.length = 100;
    for (int i = 0; i < 100; i++) {
        utassert(OtpEditValue(&s, 0, (uint32_t)('0' + i % 10)));
    }
    utassert(!OtpEditValue(&s, 0, '1'));
    utassert(s.len == 100 && len(OtpValue(&s)) == 100);
    utassert(OtpValue(&s).s[99] == '9' && OtpValue(&s).s[100] == 0);
    utassert(OtpEditValue(&s, KeyBack, 0));
    utassert(len(OtpValue(&s)) == 99);

    char big[200];
    for (int i = 0; i < 200; i++) {
        big[i] = (char)('0' + i % 10);
    }
    OtpSetValue(&s, Str(big, 200));
    utassert(s.len == 200 && OtpValue(&s).s[199] == '9');
    OtpSetValue(&s, Str{});
    utassert(s.len == 0 && base::StrEqI(OtpValue(&s), ""));
}

static void EditingEmitsChangeAndThenComplete() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<OtpState> otp = EntityNewState<OtpState>(&app);
    Entity<OtpRecorder> recorder = EntityNewState<OtpRecorder>(&app);
    SubscribeTo(&app, otp, recorder, &OtpRecorder::OnEvent);
    OtpState* state = otp.Get(&app);
    state->self = otp;
    state->length = 2;
    state->focused = true;
    Ctx cx = {&app, win, nullptr, otp.id};
    KeyEvent one = {};
    one.vk = '1';
    OtpKeyDown(state, &cx, &one);
    KeyEvent two = {};
    two.vk = '2';
    OtpKeyDown(state, &cx, &two);

    OtpRecorder* seen = recorder.Get(&app);
    utassert(seen->count == 3);
    utassert(seen->events[0] == OtpEventKind::Change);
    utassert(seen->events[1] == OtpEventKind::Change);
    utassert(seen->events[2] == OtpEventKind::Complete);

    delete win;
    EntityDropAll(&app);
}

void TestOtpInput() {
    TestSuite("otp_input");
    OnlyDigitsAreTaken();
    FullWidthDigitsFoldOntoPlainOnes();
    BackspacePopsTheLastDigit();
    AFullCodeRefusesMore();
    ALongCodeFillsToItsOwnLength();
    EditingEmitsChangeAndThenComplete();
}
