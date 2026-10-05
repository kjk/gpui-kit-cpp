/* Core Audio capture of the default input device, through an AudioQueue.

   cpal opens the device's own AUHAL unit and takes the format it runs at.
   An input AudioQueue is the same device with the conversion done by the
   system: it is asked for 48 kHz mono float and delivers that, whatever the
   hardware is set to, on a thread of its own.

   Capturing without access yields silence rather than an error on macOS, so
   the start asks AVFoundation first (is_microphone_denied in macos.rs). The
   application's Info.plist must carry NSMicrophoneUsageDescription, or the
   system refuses without asking. */

#include "sys/audio_input.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

namespace gpui {

static const int kQueueBuffers = 3;
static const Float64 kQueueRate = 48000;
// 20 ms a buffer.
static const UInt32 kQueueFrames = 960;

struct AudioInputStream {
    AudioInputCallbacks callbacks = {};
    AudioQueueRef queue = nullptr;
    AudioQueueBufferRef buffers[kQueueBuffers] = {};
    volatile int stop = 0;
};

static void OnQueueInput(void* user, AudioQueueRef queue,
                         AudioQueueBufferRef buffer, const AudioTimeStamp*,
                         UInt32, const AudioStreamPacketDescription*) {
    AudioInputStream* s = (AudioInputStream*)user;
    if (__atomic_load_n(&s->stop, __ATOMIC_RELAXED)) {
        return;
    }
    int frames = (int)(buffer->mAudioDataByteSize / sizeof(Float32));
    if (frames > 0) {
        s->callbacks.samples(s->callbacks.user,
                             (const float*)buffer->mAudioData, frames);
    }
    OSStatus status = AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    if (status != noErr && s->callbacks.error &&
        !__atomic_load_n(&s->stop, __ATOMIC_RELAXED)) {
        s->callbacks
            .error(s->callbacks.user,
                   fmt("the audio input queue failed (%d)", (int)status));
    }
}

static void SetMessage(char* message, int cap, Str text) {
    if (!message || cap <= 0) {
        return;
    }
    int n = len(text) < cap - 1 ? len(text) : cap - 1;
    if (n > 0) {
        memcpy(message, text.s, (size_t)n);
    }
    message[n] = 0;
}

bool SysAudioInputAvailable() {
    return true;
}

bool SysAudioInputDeviceName(char* out, int cap) {
    if (!out || cap <= 0) {
        return false;
    }
    out[0] = 0;
    AVCaptureDevice* device =
        [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
    if (!device) {
        return false;
    }
    const char* name = [[device localizedName] UTF8String];
    SetMessage(out, cap, Str(name && name[0] ? name : "Unnamed device"));
    return true;
}

AudioInputStream* SysAudioInputStart(const AudioInputCallbacks& callbacks,
                                     uint32_t* sampleRate,
                                     AudioInputError* error, char* message,
                                     int messageCap) {
    SetMessage(message, messageCap, Str{});
    AVAuthorizationStatus access =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (access == AVAuthorizationStatusDenied ||
        access == AVAuthorizationStatusRestricted) {
        *error = AudioInputError::PermissionDenied;
        return nullptr;
    }

    AudioStreamBasicDescription format = {};
    format.mSampleRate = kQueueRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = sizeof(Float32);
    format.mBytesPerPacket = sizeof(Float32);
    format.mFramesPerPacket = 1;

    AudioInputStream* s = new AudioInputStream();
    s->callbacks = callbacks;
    // A null run loop: the queue calls back on its own thread.
    OSStatus status = AudioQueueNewInput(&format, &OnQueueInput, s, nullptr,
                                         nullptr, 0, &s->queue);
    for (int i = 0; status == noErr && i < kQueueBuffers; i++) {
        status = AudioQueueAllocateBuffer(
            s->queue, kQueueFrames * sizeof(Float32), &s->buffers[i]);
        if (status == noErr) {
            status =
                AudioQueueEnqueueBuffer(s->queue, s->buffers[i], 0, nullptr);
        }
    }
    if (status == noErr) {
        status = AudioQueueStart(s->queue, nullptr);
    }
    if (status != noErr) {
        // kAudioQueueErr_InvalidDevice and its like: nothing to record from.
        *error =
            s->queue ? AudioInputError::Failed : AudioInputError::NoInputDevice;
        SetMessage(message, messageCap,
                   fmt("cannot start the audio input queue (%d)", (int)status));
        if (s->queue) {
            AudioQueueDispose(s->queue, true);
        }
        delete s;
        return nullptr;
    }
    if (sampleRate) {
        *sampleRate = (uint32_t)kQueueRate;
    }
    return s;
}

void SysAudioInputStop(AudioInputStream* s) {
    if (!s) {
        return;
    }
    __atomic_store_n(&s->stop, 1, __ATOMIC_RELAXED);
    // Synchronous both: the callback has returned for good once these do.
    AudioQueueStop(s->queue, true);
    AudioQueueDispose(s->queue, true);
    delete s;
}

} // namespace gpui
