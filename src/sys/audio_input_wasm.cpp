/* The browser's microphone: not implemented.

   getUserMedia answers through a promise, behind a permission prompt the
   browser only shows inside a user gesture, and delivers audio on an
   AudioWorklet thread a single-threaded module has no way to hear from
   synchronously. A page that wants speech feeds a SpeechState an AudioInput
   of its own. `SysAudioInputAvailable` answers false. */

#include "sys/audio_input.h"

namespace gpui {

bool SysAudioInputAvailable() {
    return false;
}

bool SysAudioInputDeviceName(char* out, int cap) {
    if (out && cap > 0) {
        out[0] = 0;
    }
    return false;
}

AudioInputStream* SysAudioInputStart(const AudioInputCallbacks&, uint32_t*,
                                     AudioInputError* error, char* message,
                                     int messageCap) {
    if (error) {
        *error = AudioInputError::Unsupported;
    }
    if (message && messageCap > 0) {
        message[0] = 0;
    }
    return nullptr;
}

void SysAudioInputStop(AudioInputStream*) {}

} // namespace gpui
