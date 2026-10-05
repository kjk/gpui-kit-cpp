#ifndef GPUI_SYS_SPEECH_RECOGNIZER_H_
#define GPUI_SYS_SPEECH_RECOGNIZER_H_
/* The operating system's speech recognizer — the platform half of speech's
   SystemRecognizer (crates/ui/src/speech/system/).

   - macOS: SFSpeechRecognizer, recognizing on the device only. A language
     the device cannot recognize offline is not available, so audio never
     leaves the machine. The application's Info.plist must carry
     NSSpeechRecognitionUsageDescription; without it the recognizer reports
     itself unavailable rather than let the system terminate the process.
   - Windows: Windows.Media.SpeechRecognition, continuous dictation.
     Dictation needs the language's speech pack and the "Online speech
     recognition" privacy setting, and runs through Microsoft's online
     service. The recognizer captures from the default microphone itself, so
     pushed audio is ignored; the Microphone keeps capturing alongside it
     only to drive the waveform, which WASAPI's shared mode allows.
   - Linux, wasm: none. SysSpeechRecognizerNew answers null.
   - iOS, Android: the host adapter.

   A session reports through SysSpeechEvents, on the main thread, and never
   after SysSpeechSessionDrop: the framework's own threads only forward. */

#include "base.h"

namespace gpui {

enum class SysSpeechError : uint8_t {
    PermissionDenied,
    NoInputDevice,
    Unsupported,
    // The recognizer failed; the message says how.
    Recognizer
};

// Where a session reports. Every call is on the main thread.
struct SysSpeechEvents {
    void* user = nullptr;
    // The recognizer is consuming audio.
    void (*ready)(void* user) = nullptr;
    // The hypothesis for the phrase being spoken, separator included.
    void (*hypothesis)(void* user, Str text) = nullptr;
    // A recognized phrase, separator included.
    void (*phrase)(void* user, Str text) = nullptr;
    // The session is complete.
    void (*finish)(void* user) = nullptr;
    // The session failed. `message` is empty except for Recognizer.
    void (*error)(void* user, SysSpeechError kind, Str message) = nullptr;
};

struct SysSpeechRecognizer;
struct SysSpeechSession;

// A recognizer for `locale`, a BCP 47 language tag such as `en-US`, or for
// the system's language when it is empty. Null when the platform has none.
SysSpeechRecognizer* SysSpeechRecognizerNew(Str locale);
void SysSpeechRecognizerFree(SysSpeechRecognizer* recognizer);
// Whether a session can start now. Cheap: it is asked on every render.
bool SysSpeechRecognizerAvailable(const SysSpeechRecognizer* recognizer);

// Open a session. Null, with `error` and `message` filled, when it cannot.
SysSpeechSession* SysSpeechSessionStart(SysSpeechRecognizer* recognizer,
                                        const SysSpeechEvents& events,
                                        SysSpeechError* error, char* message,
                                        int messageCap);
// 16 kHz mono PCM.
void SysSpeechSessionPushAudio(SysSpeechSession* session,
                               const int16_t* samples, int count);
// No more audio will arrive; `finish` follows once the last result is in.
void SysSpeechSessionFinish(SysSpeechSession* session);
// Cancel and free. Nothing is reported after this returns.
void SysSpeechSessionDrop(SysSpeechSession* session);

// ─── what the backends share ──────────────────────────────────────────────

// system/winrt.rs phrase_separator: what goes between two phrases, which
// dictation returns without surrounding whitespace: a space, except in
// languages written without spaces between words (Chinese, Japanese, Thai,
// Lao, Khmer, Burmese). `tag` is a BCP 47 language tag.
Str SpeechPhraseSeparator(Str tag);

// system/macos.rs starts_over: whether `text`, the result after the one that
// ended `utterance`, starts a new utterance instead of carrying `utterance`
// on.
//
// A result that carries it on may still revise its words, punctuation or
// case ("Hello word" becomes "Hello world, how"), so an exact prefix is too
// strict; a result that starts over shares almost nothing with it. Less than
// half of `utterance` in common means it started over.
bool SpeechStartsOver(Str utterance, Str text);

// system/macos.rs needs_space: whether two phrases ending and starting with
// these characters need a space between them.
bool SpeechNeedsSpace(uint32_t before, uint32_t after);

} // namespace gpui
#endif // GPUI_SYS_SPEECH_RECOGNIZER_H_
