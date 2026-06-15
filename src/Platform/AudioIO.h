#pragma once
// platform/AudioIO.h — PCM capture/playback boundary for voice (Linux port L1).
//
// Wraps WASAPI on Windows (existing VoiceEngine) and miniaudio (PipeWire/ALSA/
// Pulse) on Linux (L5). Frames are interleaved 16-bit signed PCM. Callbacks run
// on the audio device thread — keep them allocation-free and fast.

#include <cstdint>
#include <functional>
#include <memory>

namespace platform {

struct AudioFormat {
    int sampleRate = 48000;   // Opus-friendly default (matches voice v2 plan)
    int channels   = 1;       // mono capture/playback for voice
};

// Pulls capture frames: implementation calls this with `frameCount` samples of
// mic input (interleaved int16) for the app to encode/send.
using CaptureCallback = std::function<void(const int16_t* in, int frameCount)>;

// Fills playback frames: the app writes up to `frameCount` decoded samples into
// `out`; unwritten samples must be left silent by the implementation.
using RenderCallback  = std::function<void(int16_t* out, int frameCount)>;

class IAudioIn {
public:
    virtual ~IAudioIn() = default;
    virtual bool start(const AudioFormat&, CaptureCallback) = 0;
    virtual void stop() = 0;
};

class IAudioOut {
public:
    virtual ~IAudioOut() = default;
    virtual bool start(const AudioFormat&, RenderCallback) = 0;
    virtual void stop() = 0;
};

// Factories, provided by the platform impl (Linux: miniaudio; Windows: WASAPI,
// once VoiceEngine is migrated onto the boundary). `useNullDevice` selects
// miniaudio's null backend — a device-free clock that still drives the callbacks
// at the configured rate, used by the headless self-check / CI where no real
// sound hardware exists. Returns nullptr if no backend could be opened.
std::unique_ptr<IAudioOut> makeAudioOut(bool useNullDevice = false);
std::unique_ptr<IAudioIn>  makeAudioIn(bool useNullDevice = false);

} // namespace platform
