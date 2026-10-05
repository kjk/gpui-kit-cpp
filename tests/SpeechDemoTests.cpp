/* Ported from the tests in examples/speech/src/demo.rs: the demo
 * recognizer's voice activity check. The two after them are ours — upstream
 * has none for the session — and pin what the example's flow rests on: a
 * token per quarter second of voice, and a pause that ends the sentence. */

#include "Test.h"

#include "../examples/speech_demo.h"

using namespace gpui::component;

// silence_and_room_noise_are_not_speech
static void SilenceAndRoomNoiseAreNotSpeech() {
    int16_t window[kDemoWindowSamples] = {};
    utassert(!DemoIsSpeech(window, 0));
    utassert(!DemoIsSpeech(window, kDemoWindowSamples));
    for (int ix = 0; ix < kDemoWindowSamples; ix++) {
        window[ix] = 60;
    }
    utassert(!DemoIsSpeech(window, kDemoWindowSamples));
}

static void FillVoice(int16_t* out, int count) {
    for (int ix = 0; ix < count; ix++) {
        out[ix] = (int16_t)(ix % 2 == 0 ? 3300 : -3300);
    }
}

// a_voice_is_speech
static void AVoiceIsSpeech() {
    // About -20 dBFS, ordinary speech into a laptop microphone.
    int16_t voice[kDemoWindowSamples];
    FillVoice(voice, kDemoWindowSamples);
    utassert(DemoIsSpeech(voice, kDemoWindowSamples));
}

static void TheScriptFollowsTheLanguage() {
    DemoRecognizer demo;
    demo.Language(StrL("zh-HK"));
    utassert(StrEq(Str(demo.script->language), StrL("zh")));
    demo.Language(StrL("ja-JP"));
    utassert(StrEq(Str(demo.script->language), StrL("ja")));
    demo.Language(StrL("fr-FR"));
    utassert(StrEq(Str(demo.script->language), StrL("en")));
}

// The sink of a default DemoRecognizer names no state, so its calls go
// nowhere; what is checked is the session's own bookkeeping.
static void VoiceTypesTokensAndAPauseEndsTheSentence() {
    DemoRecognizer demo;
    demo.Language(StrL("en-US"));
    App* app = TestAppNew();

    static int16_t voice[kDemoSamplesPerToken * 3];
    FillVoice(voice, kDemoSamplesPerToken * 3);
    DemoRecognizer::PushAudio(&demo, voice, kDemoSamplesPerToken * 3, app);
    utassert(demo.tokens == 3);
    utassert(demo.sentenceIx == 0);
    utassert(StrEq(demo.HeardTemp(), StrL("Speech input turns")));

    // Short of a pause, nothing ends.
    static int16_t quiet[kDemoPauseSamples] = {};
    DemoRecognizer::PushAudio(&demo, quiet, kDemoPauseSamples - 320, app);
    utassert(demo.tokens == 3);
    DemoRecognizer::PushAudio(&demo, quiet, 320, app);
    utassert(demo.tokens == 0);
    utassert(demo.sentenceIx == 1);

    // The next sentence is joined to the first by the script's separator.
    DemoRecognizer::PushAudio(&demo, voice, kDemoSamplesPerToken, app);
    utassert(StrEq(demo.HeardTemp(), StrL(" Stop")));
    TestAppFree(app);
}

void TestSpeechDemo() {
    SilenceAndRoomNoiseAreNotSpeech();
    AVoiceIsSpeech();
    TheScriptFollowsTheLanguage();
    VoiceTypesTokensAndAPauseEndsTheSentence();
}
