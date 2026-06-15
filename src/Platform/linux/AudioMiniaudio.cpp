// AudioMiniaudio.cpp — miniaudio implementation of platform::IAudioIn /
// IAudioOut (Linux port L5-b). miniaudio wraps PipeWire/ALSA/Pulse behind one
// API; this drives interleaved int16 PCM through the device callbacks, calling
// the app's Render/Capture callbacks on the audio thread. The pure sample
// plumbing those callbacks feed (ring/jitter buffers) lives in audio:: (L5-a) so
// it's shared and KAT-locked; this file is just the device glue.
//
// `useNullDevice` selects miniaudio's null backend — a device-free clock that
// still fires the data callback at the configured rate — so the headless
// self-check and CI exercise the full start→callback→stop path with no sound
// hardware. Only compiled on non-Windows builds (see CMakeLists.txt); the
// Windows build keeps WASAPI until VoiceEngine is migrated onto this boundary.

#include "Platform/AudioIO.h"

#define MINIAUDIO_IMPLEMENTATION
// Voice only needs PCM playback/capture — drop the decoders/encoders/resamplers
// we don't use to keep this TU small and warning-free.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

#include <cstring>
#include <memory>

namespace platform {
namespace {

ma_format kFormat = ma_format_s16;

// Shared device wrapper: a ma_device plus (for the null backend) its own context
// pinned to ma_backend_null. RAII-uninit on destruction.
struct Device {
    ma_device  device{};
    ma_context context{};
    bool       deviceInited  = false;
    bool       contextInited = false;
    bool       useNull = false;
    int        channels = 1;

    ~Device() { teardown(); }

    void teardown() {
        if (deviceInited)  { ma_device_uninit(&device); deviceInited = false; }
        if (contextInited) { ma_context_uninit(&context); contextInited = false; }
    }

    // Init the device with the given config. For the null backend we first stand
    // up a context restricted to ma_backend_null and init the device against it;
    // otherwise we pass NULL for miniaudio's default backend selection.
    bool init(ma_device_config& cfg) {
        ma_context* pctx = nullptr;
        if (useNull) {
            ma_backend backends[] = { ma_backend_null };
            ma_context_config cc = ma_context_config_init();
            if (ma_context_init(backends, 1, &cc, &context) != MA_SUCCESS)
                return false;
            contextInited = true;
            pctx = &context;
        }
        if (ma_device_init(pctx, &cfg, &device) != MA_SUCCESS) {
            teardown();
            return false;
        }
        deviceInited = true;
        return true;
    }

    bool startDevice() { return ma_device_start(&device) == MA_SUCCESS; }
    void stopDevice()  { if (deviceInited) ma_device_stop(&device); }
};

// ── Playback ─────────────────────────────────────────────────────────────────
class AudioOut : public IAudioOut {
public:
    explicit AudioOut(bool useNull) { dev_.useNull = useNull; }
    ~AudioOut() override { stop(); }

    bool start(const AudioFormat& fmt, RenderCallback cb) override {
        if (running_) return false;
        render_ = std::move(cb);
        dev_.channels = fmt.channels;
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = kFormat;
        cfg.playback.channels = (ma_uint32)fmt.channels;
        cfg.sampleRate        = (ma_uint32)fmt.sampleRate;
        cfg.dataCallback      = &AudioOut::dataThunk;
        cfg.pUserData         = this;
        if (!dev_.init(cfg) || !dev_.startDevice()) { dev_.teardown(); return false; }
        running_ = true;
        return true;
    }

    void stop() override {
        if (!running_) return;
        dev_.stopDevice();
        dev_.teardown();
        running_ = false;
    }

private:
    static void dataThunk(ma_device* d, void* out, const void*, ma_uint32 frames) {
        auto* self = static_cast<AudioOut*>(d->pUserData);
        const int samples = (int)frames * self->dev_.channels;
        auto* dst = static_cast<int16_t*>(out);
        // The RenderCallback contract leaves unwritten samples silent, so zero the
        // device buffer first (miniaudio hands us uninitialized memory).
        std::memset(dst, 0, (size_t)samples * sizeof(int16_t));
        if (self->render_) self->render_(dst, samples);
    }

    Device dev_;
    RenderCallback render_;
    bool running_ = false;
};

// ── Capture ──────────────────────────────────────────────────────────────────
class AudioIn : public IAudioIn {
public:
    explicit AudioIn(bool useNull) { dev_.useNull = useNull; }
    ~AudioIn() override { stop(); }

    bool start(const AudioFormat& fmt, CaptureCallback cb) override {
        if (running_) return false;
        capture_ = std::move(cb);
        dev_.channels = fmt.channels;
        ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
        cfg.capture.format   = kFormat;
        cfg.capture.channels = (ma_uint32)fmt.channels;
        cfg.sampleRate       = (ma_uint32)fmt.sampleRate;
        cfg.dataCallback     = &AudioIn::dataThunk;
        cfg.pUserData        = this;
        if (!dev_.init(cfg) || !dev_.startDevice()) { dev_.teardown(); return false; }
        running_ = true;
        return true;
    }

    void stop() override {
        if (!running_) return;
        dev_.stopDevice();
        dev_.teardown();
        running_ = false;
    }

private:
    static void dataThunk(ma_device* d, void*, const void* in, ma_uint32 frames) {
        auto* self = static_cast<AudioIn*>(d->pUserData);
        const int samples = (int)frames * self->dev_.channels;
        if (self->capture_)
            self->capture_(static_cast<const int16_t*>(in), samples);
    }

    Device dev_;
    CaptureCallback capture_;
    bool running_ = false;
};

} // namespace

std::unique_ptr<IAudioOut> makeAudioOut(bool useNullDevice) {
    return std::make_unique<AudioOut>(useNullDevice);
}
std::unique_ptr<IAudioIn> makeAudioIn(bool useNullDevice) {
    return std::make_unique<AudioIn>(useNullDevice);
}

} // namespace platform
