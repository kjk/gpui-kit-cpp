#ifndef GPUI_SYS_AUDIO_INPUT_H_
#define GPUI_SYS_AUDIO_INPUT_H_
/* The default audio input device — the platform half of speech's Microphone.

   crates/ui/src/speech/microphone.rs captures through cpal, which is Core
   Audio on macOS, WASAPI on Windows and ALSA on Linux. This is the same three
   written against the OS directly, and only what the microphone asks of
   cpal: open the default input device in whatever format it runs at, and
   hand each callback's frames over mixed down to mono.

   - Windows: WASAPI, shared mode, event driven, on a thread of its own. The
     device's mix format is taken as it is.
   - macOS: an AudioQueue input, which converts the device's format to the
     48 kHz mono float this asks for. The application's Info.plist must carry
     NSMicrophoneUsageDescription, or the system refuses access without
     asking.
   - Linux: ALSA's `default` PCM, when libasound2-dev was there at build
     time (GPUI_HAVE_ALSA). Without it there is no input, which is what a
     machine with no device answers too.
   - wasm: none. getUserMedia is asynchronous and wants a user gesture; a
     page feeds an AudioInput of its own.
   - iOS, Android: the host adapter.

   The callbacks run on the audio thread: they copy and return. Nothing of
   the UI's is touched from them. */

#include "base.h"

namespace gpui {

enum class AudioInputError : uint8_t {
    None,
    // The user or the system denied access to the microphone.
    PermissionDenied,
    // No audio input device is available.
    NoInputDevice,
    // This build or platform has no audio input.
    Unsupported,
    // The device could not be opened; `message` says why.
    Failed
};

struct AudioInputCallbacks {
    void* user = nullptr;
    // `frames` mono samples in -1..1, at the rate SysAudioInputStart
    // answered. Audio thread.
    void (*samples)(void* user, const float* mono, int frames) = nullptr;
    // The stream failed and delivers nothing more. Audio thread.
    void (*error)(void* user, Str message) = nullptr;
};

struct AudioInputStream;

// Whether this build can capture at all. False is not an error.
bool SysAudioInputAvailable();

// Open the default input device and start capturing. Answers null and fills
// `error` (and `message`, NUL-terminated, for Failed) when it cannot.
// `sampleRate` receives the rate the samples arrive at.
AudioInputStream* SysAudioInputStart(const AudioInputCallbacks& callbacks,
                                     uint32_t* sampleRate,
                                     AudioInputError* error, char* message,
                                     int messageCap);

// Stop and free the stream. No callback runs after this returns.
void SysAudioInputStop(AudioInputStream* stream);

} // namespace gpui
#endif // GPUI_SYS_AUDIO_INPUT_H_
