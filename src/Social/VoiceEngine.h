#pragma once
// VoiceEngine.h - WASAPI capture + render for voice chat (social.md §6 media
// stage). Captures the default communications microphone, hands fixed 20 ms
// frames of 16 kHz mono signed-16 PCM to a callback (SocialManager packetizes
// and relays them over the gateway), and renders incoming frames pushed via
// PushPlayback through the default communications speaker.
//
// The WASAPI shared-mode streams are opened with AUTOCONVERTPCM so the audio
// engine resamples to/from our fixed 16 kHz mono format regardless of the
// device's native mix format - no hand-rolled SRC needed. Capture and render
// each run on their own polling thread; a small mutex-guarded jitter buffer
// decouples network arrival from the render clock.
//
// Transport carries raw PCM (no codec) for a dependency-free first cut; the
// frame boundary (FrameSamples) is the natural seam to drop in Opus later.

#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
#include <cstdint>

namespace social {

class VoiceEngine {
public:
    static constexpr int SampleRate  = 16000;
    static constexpr int Channels    = 1;
    static constexpr int FrameSamples = 320;   // 20 ms @ 16 kHz mono

    using FrameCallback = std::function<void(const int16_t* samples, size_t count)>;

    VoiceEngine() = default;
    ~VoiceEngine();

    VoiceEngine(const VoiceEngine&) = delete;
    VoiceEngine& operator=(const VoiceEngine&) = delete;

    // Opens the mic + speaker and starts both threads. onCaptured fires on the
    // capture thread with each 20 ms frame. Returns false if devices are
    // unavailable. Idempotent: a second Start() is a no-op while running.
    bool Start(FrameCallback onCaptured);

    // Stops both threads and releases the devices. Idempotent.
    void Stop();

    bool IsRunning() const { return m_running.load(); }

    // Enqueue a decoded frame for playback (called from the network thread).
    void PushPlayback(const int16_t* samples, size_t count);

    // Microphone mute: when muted, capture still runs but frames are dropped so
    // the call stays "live" without transmitting audio.
    void SetMuted(bool m) { m_muted.store(m); }
    bool IsMuted() const { return m_muted.load(); }

private:
    void CaptureLoop(FrameCallback onCaptured);
    void RenderLoop();

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_muted{ false };
    std::thread m_captureThread;
    std::thread m_renderThread;

    std::mutex m_playMtx;
    std::deque<int16_t> m_playBuf;   // jitter buffer of pending playback samples
    static constexpr size_t kMaxPlayBuf = SampleRate;  // cap ~1 s to bound latency
};

} // namespace social
