/* system/unsupported.rs: no platform recognizer.

   The browser's SpeechRecognition is Chrome's alone, sends audio to its
   vendor and captures for itself; it is not what SystemRecognizer promises.
   A page supplies its own recognizer. */

#include "sys/speech_recognizer.h"

namespace gpui {

SysSpeechRecognizer* SysSpeechRecognizerNew(Str) {
    return nullptr;
}

void SysSpeechRecognizerFree(SysSpeechRecognizer*) {}

bool SysSpeechRecognizerAvailable(const SysSpeechRecognizer*) {
    return false;
}

SysSpeechSession* SysSpeechSessionStart(SysSpeechRecognizer*,
                                        const SysSpeechEvents&,
                                        SysSpeechError* error, char* message,
                                        int messageCap) {
    if (error) {
        *error = SysSpeechError::Unsupported;
    }
    if (message && messageCap > 0) {
        message[0] = 0;
    }
    return nullptr;
}

void SysSpeechSessionPushAudio(SysSpeechSession*, const int16_t*, int) {}
void SysSpeechSessionFinish(SysSpeechSession*) {}
void SysSpeechSessionDrop(SysSpeechSession*) {}

} // namespace gpui
