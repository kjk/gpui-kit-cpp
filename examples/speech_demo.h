#ifndef GPUI_EXAMPLES_SPEECH_DEMO_H_
#define GPUI_EXAMPLES_SPEECH_DEMO_H_
/* examples/speech/src/demo.rs
 *
 * A recognizer that needs no service, permission or network: it types a
 * scripted passage as you speak, so the whole flow can be tried anywhere,
 * including on Linux.
 *
 * It does not recognize words, but it follows your voice: tokens appear only
 * while the input is loud enough to be speech, and a pause ends the sentence.
 *
 * A header rather than part of speech.cpp so tests/SpeechDemoTests.cpp can
 * reach it, the way Rust's tests sit in the module. */

#include "gpui.h"

#include <math.h>

// SAMPLES_PER_TOKEN: speech per recognized token, 0.25 s at 16 kHz.
const int kDemoSamplesPerToken = 4000;
// WINDOW_SAMPLES: audio is judged speech or silence in windows of 20 ms.
const int kDemoWindowSamples = 320;
// PAUSE_SAMPLES: a pause this long ends the sentence, 0.8 s.
const int kDemoPauseSamples = 12800;
// VOICE_RMS: a window counts as speech above about -40 dBFS, well over a
// quiet room's noise floor and well under normal speech.
const double kDemoVoiceRms = 330.;

struct DemoSentence {
    const char* const* tokens;
    int count;
};

struct DemoScript {
    const char* language;
    // Put between tokens and between sentences.
    const char* separator;
    const DemoSentence* sentences;
    int count;
};

// SCRIPTS.
inline const DemoScript* DemoScripts(int* count) {
    static const char* const en0[] = {"Speech", "input", "turns", "what",
                                      "you",    "say",   "into",  "text."};
    static const char* const en1[] = {"Stop", "whenever", "you",   "like,",
                                      "and",  "the",      "words", "land",
                                      "in",   "the",      "note."};
    static const char* const en2[] = {"Any", "speech",  "service", "plugs",
                                      "in",  "through", "one",     "trait."};
    static const DemoSentence en[] = {{en0, 8}, {en1, 11}, {en2, 8}};

    static const char* const zh0[] = {"语音", "输入", "会把",  "你说",
                                      "的话", "转成", "文字。"};
    static const char* const zh1[] = {"随时", "停止，", "文字",
                                      "就会", "写进",   "笔记。"};
    static const char* const zh2[] = {"任何", "识别", "服务", "都能",
                                      "通过", "一个", "接口", "接进来。"};
    static const DemoSentence zh[] = {{zh0, 7}, {zh1, 6}, {zh2, 8}};

    static const char* const ja0[] = {"話した", "言葉が", "そのまま", "文字に",
                                      "なります。"};
    static const char* const ja1[] = {"止めると", "ノートに",
                                      "書き込まれます。"};
    static const DemoSentence ja[] = {{ja0, 5}, {ja1, 3}};

    static const DemoScript scripts[] = {
        {"en", " ", en, 3},
        {"zh", "", zh, 3},
        {"ja", "", ja, 2},
    };
    *count = 3;
    return scripts;
}

// is_speech: whether `window` is loud enough to be speech.
inline bool DemoIsSpeech(const int16_t* window, int count) {
    if (count <= 0) {
        return false;
    }
    double energy = 0;
    for (int ix = 0; ix < count; ix++) {
        energy += (double)window[ix] * (double)window[ix];
    }
    return sqrt(energy / (double)count) > kDemoVoiceRms;
}

// DemoRecognizer and the one DemoSession it has open: Rust boxes a session
// per start, and a state runs one session at a time, so the recognizer holds
// it.
struct DemoRecognizer {
    const DemoScript* script = nullptr;

    gpui::component::SpeechSink sink = {};
    int sentenceIx = 0;
    // Tokens of the current sentence heard so far.
    int tokens = 0;
    // Speech received since the last token, in samples.
    int samples = 0;
    // Silence since the last speech, in samples.
    int silence = 0;

    // A recognizer typing the passage for `language`, a BCP 47 tag.
    void Language(gpui::Str language) {
        int count = 0;
        const DemoScript* scripts = DemoScripts(&count);
        script = &scripts[0];
        for (int ix = 0; ix < count; ix++) {
            if (gpui::StrStartsWith(language,
                                    gpui::Str(scripts[ix].language))) {
                script = &scripts[ix];
                break;
            }
        }
    }

    gpui::component::SpeechRecognizer AsRecognizer() {
        if (!script) {
            Language(gpui::Str{});
        }
        gpui::component::SpeechRecognizer r;
        r.data = this;
        r.start = &DemoRecognizer::Start;
        return r;
    }

    const DemoSentence* Sentence() const {
        return &script->sentences[sentenceIx % script->count];
    }

    // The heard part of the current sentence, with the separator that joins
    // it to the sentences before; empty when none of it was heard.
    gpui::TempStr HeardTemp() const {
        if (tokens == 0) {
            return {};
        }
        const DemoSentence* sentence = Sentence();
        gpui::Str separator = gpui::Str(script->separator);
        gpui::TempStr heard =
            gpui::StrDupTemp(sentenceIx > 0 ? separator : gpui::Str(""));
        for (int ix = 0; ix < tokens && ix < sentence->count; ix++) {
            heard =
                gpui::fmt("%s%s%s", heard, ix > 0 ? separator : gpui::Str(""),
                          gpui::Str(sentence->tokens[ix]));
        }
        return heard;
    }

    // End the current sentence, if any of it was heard.
    void Commit(gpui::App* app) {
        gpui::TempStr heard = HeardTemp();
        if (gpui::len(heard) == 0) {
            return;
        }
        sink.Phrase(heard, app);
        sentenceIx++;
        tokens = 0;
    }

    void NextToken(gpui::App* app) {
        tokens++;
        if (tokens < Sentence()->count) {
            sink.Hypothesis(HeardTemp(), app);
        } else {
            Commit(app);
        }
    }

    static bool Start(void* data, gpui::component::SpeechSink sink,
                      gpui::App* app, gpui::component::RecognitionSession* out,
                      gpui::component::SpeechError*) {
        DemoRecognizer* self = (DemoRecognizer*)data;
        // There is nothing to connect to, so audio is consumed at once.
        sink.Ready(app);
        self->sink = sink;
        self->sentenceIx = 0;
        self->tokens = 0;
        self->samples = 0;
        self->silence = 0;
        out->data = self;
        out->pushAudio = &DemoRecognizer::PushAudio;
        out->finish = &DemoRecognizer::Finish;
        return true;
    }

    static void PushAudio(void* data, const int16_t* samples, int count,
                          gpui::App* app) {
        DemoRecognizer* self = (DemoRecognizer*)data;
        for (int at = 0; at < count; at += kDemoWindowSamples) {
            int n = count - at < kDemoWindowSamples ? count - at
                                                    : kDemoWindowSamples;
            if (DemoIsSpeech(samples + at, n)) {
                self->silence = 0;
                self->samples += n;
                while (self->samples >= kDemoSamplesPerToken) {
                    self->samples -= kDemoSamplesPerToken;
                    self->NextToken(app);
                }
            } else {
                self->silence += n;
                if (self->silence >= kDemoPauseSamples && self->tokens > 0) {
                    self->samples = 0;
                    self->Commit(app);
                }
            }
        }
    }

    static void Finish(void* data, gpui::App* app) {
        DemoRecognizer* self = (DemoRecognizer*)data;
        self->Commit(app);
        self->sink.Finish(app);
    }
};

#endif // GPUI_EXAMPLES_SPEECH_DEMO_H_
