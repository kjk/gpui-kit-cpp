/* system/unsupported.rs: no platform recognizer.

   Linux has no system speech recognizer, as upstream's unsupported.rs says:
   an application recognizer is required there. */

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
