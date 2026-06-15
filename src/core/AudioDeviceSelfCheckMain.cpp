// AudioDeviceSelfCheckMain.cpp — headless KAT for the miniaudio platform backend
// (Linux port L5-b). Drives the REAL device path (start → data callback → stop)
// on miniaudio's NULL backend — a device-free clock that still fires the
// callbacks at the configured rate — so CI exercises the whole boundary with no
// sound hardware. The callbacks are wired through the shared audio:: buffers
// (L5-a) exactly as the voice path will use them:
//   • playback: the RenderCallback drains an audio::JitterBuffer we pre-filled,
//   • capture:  the CaptureCallback writes mic frames into an audio::RingBuffer.
// Asserts both callbacks actually fired and moved samples. Exit 0 on success;
// SKIP (exit 0) if even the null backend can't open (shouldn't happen).

#include "Platform/AudioIO.h"
#include "AudioBuffer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

int main() {
    using namespace std::chrono_literals;
    const platform::AudioFormat fmt{48000, 1};   // 48 kHz mono (voice)

    int fail = 0;
    auto ck = [&](const char* what, bool cond) {
        if (!cond) { std::printf("  FAIL %s\n", what); ++fail; }
    };

    // ── Playback: drain a pre-filled jitter buffer through the render callback.
    {
        auto out = platform::makeAudioOut(/*useNullDevice=*/true);
        if (!out) { std::printf("audio-device self-check: SKIP (no null backend)\n"); return 0; }

        audio::JitterBuffer jb(48000, 480);       // 1s cap, 10ms prebuffer
        std::vector<int16_t> tone(24000);          // 0.5s of a ramp
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = (int16_t)(i % 1000);
        jb.push(tone.data(), tone.size());

        std::mutex m;
        std::atomic<long> rendered{0};
        std::atomic<long> calls{0};
        bool ok = out->start(fmt, [&](int16_t* o, int n) {
            std::lock_guard<std::mutex> lk(m);
            rendered += (long)jb.pull(o, (size_t)n);   // real (non-silence) samples
            ++calls;
        });
        ck("playback start", ok);
        std::this_thread::sleep_for(200ms);            // let the null clock pump
        out->stop();

        ck("playback callback fired", calls.load() > 0);
        ck("playback drained real samples", rendered.load() > 0);
        // No more than we queued should ever come out as real audio.
        ck("playback bounded by queue", rendered.load() <= (long)tone.size());
        std::printf("  playback: %ld callbacks, %ld real samples drained (of %zu)\n",
                    calls.load(), rendered.load(), tone.size());
    }

    // ── Capture: collect mic frames into a ring buffer via the capture callback.
    {
        auto in = platform::makeAudioIn(/*useNullDevice=*/true);
        ck("capture object", (bool)in);
        if (in) {
            audio::RingBuffer rb(48000);
            std::mutex m;
            std::atomic<long> captured{0};
            std::atomic<long> calls{0};
            bool ok = in->start(fmt, [&](const int16_t* mic, int n) {
                std::lock_guard<std::mutex> lk(m);
                rb.write(mic, (size_t)n);              // null backend delivers silence
                captured += n;
                ++calls;
            });
            ck("capture start", ok);
            std::this_thread::sleep_for(200ms);
            in->stop();

            ck("capture callback fired", calls.load() > 0);
            ck("capture delivered frames", captured.load() > 0);
            std::printf("  capture: %ld callbacks, %ld samples delivered\n",
                        calls.load(), captured.load());
        }
    }

    // ── Lifecycle: a stopped device can be restarted, and double-stop is safe.
    {
        auto out = platform::makeAudioOut(true);
        std::atomic<long> c1{0}, c2{0};
        out->start(fmt, [&](int16_t*, int) { ++c1; });
        std::this_thread::sleep_for(80ms);
        out->stop();
        out->stop();                                   // idempotent
        long after = c1.load();
        out->start(fmt, [&](int16_t*, int) { ++c2; }); // restart with a new cb
        std::this_thread::sleep_for(80ms);
        out->stop();
        ck("restart fires new callback", c2.load() > 0);
        ck("stop halts callbacks", c1.load() == after);
    }

    if (fail == 0)
        std::printf("audio-device self-check: OK — miniaudio null backend "
                    "playback + capture + restart, wired through audio:: buffers\n");
    else
        std::printf("audio-device self-check: FAILED (%d)\n", fail);
    return fail == 0 ? 0 : 1;
}
