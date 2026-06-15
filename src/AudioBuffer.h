#pragma once
// AudioBuffer.h — portable (UTF-8, Win32-free) PCM staging buffers for voice
// (Linux port L5, audio foundation). These are the device-free core that BOTH
// platform audio backends build on: miniaudio (PipeWire/ALSA/Pulse) on Linux and
// WASAPI on Windows. The platform layer owns the device callbacks; this owns the
// sample plumbing those callbacks drain/fill, so the timing-sensitive logic is
// shared and KAT-locked instead of reimplemented per backend.
//
// Two pieces, mirroring the two halves of platform::AudioIO:
//   • RingBuffer  — a fixed-capacity SPSC ring of interleaved int16 samples. The
//                   capture path writes mic frames in; the app reads them out to
//                   encode/send. Overflow drops the OLDEST samples (a live voice
//                   stream must stay current, not stall).
//   • JitterBuffer — a playback-side ring with a prebuffer (target latency): it
//                   holds incoming decoded frames until `prefill` samples have
//                   arrived, then feeds the render callback. On underrun it
//                   outputs silence (and re-arms prebuffering) so a network hiccup
//                   is a gap, never a buffer of stale audio played late.
//
// All counts are in SAMPLES (not bytes, not frames) — for mono voice a sample is
// a frame; for N channels the caller passes interleaved samples and sizes the
// buffer as frames*channels. Single-producer/single-consumer by contract: one
// thread writes, one reads (the audio device thread is the producer for capture
// and the consumer for playback).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {

// Fixed-capacity ring of interleaved int16 PCM samples. Overflow drops oldest.
class RingBuffer {
public:
    explicit RingBuffer(size_t capacitySamples = 0) { reset(capacitySamples); }

    // Resize to hold `capacitySamples` and clear all contents.
    void reset(size_t capacitySamples);
    // Drop all buffered samples (keep capacity).
    void clear();

    size_t capacity() const { return cap_; }
    size_t size()  const { return size_; }            // samples currently buffered
    size_t space() const { return cap_ - size_; }     // free samples
    bool   empty() const { return size_ == 0; }
    bool   full()  const { return size_ == cap_; }

    // Write `n` samples. If they don't fit, the OLDEST buffered samples are
    // dropped to make room (a live stream stays current). Returns the number of
    // samples DROPPED to fit (0 when there was room). Writing more than capacity
    // keeps only the newest `cap_` samples.
    size_t write(const int16_t* src, size_t n);

    // Read up to `n` samples into `dst`. Returns the count actually read (<= n,
    // limited by what's buffered). Does NOT pad — the caller decides about the
    // remainder. Use for the capture/encode path.
    size_t read(int16_t* dst, size_t n);

    // Read exactly `n` samples, zero-filling any shortfall. Returns the number of
    // REAL samples written (n - underrun). Use for a render callback that must
    // fully fill `out` and leave the rest silent.
    size_t readOrSilence(int16_t* dst, size_t n);

private:
    std::vector<int16_t> buf_;
    size_t cap_  = 0;   // usable capacity in samples
    size_t head_ = 0;   // read index
    size_t size_ = 0;   // samples buffered
};

// Playback-side jitter buffer: prebuffers `prefillSamples` before output begins,
// then drains to the render callback. Underrun outputs silence and re-arms the
// prebuffer so playback resumes cleanly once enough samples are queued again.
class JitterBuffer {
public:
    JitterBuffer() = default;
    JitterBuffer(size_t capacitySamples, size_t prefillSamples) {
        reset(capacitySamples, prefillSamples);
    }

    void reset(size_t capacitySamples, size_t prefillSamples);
    void clear();   // drop queued audio and re-arm prebuffering

    size_t size()  const { return ring_.size(); }
    size_t capacity() const { return ring_.capacity(); }
    bool   priming() const { return priming_; }   // true while waiting for prefill

    // Counters (monotonic since construction/reset) for diagnostics/tests.
    uint64_t underrunSamples() const { return underrun_; }
    uint64_t droppedSamples()  const { return dropped_; }

    // Push decoded samples from the network/decoder. Overflow drops oldest.
    void push(const int16_t* src, size_t n);

    // Pull exactly `n` samples for the device. While priming (not enough queued
    // yet) it outputs silence. Once primed it drains the ring, zero-filling any
    // shortfall and re-entering priming on underrun. Returns REAL samples output.
    size_t pull(int16_t* dst, size_t n);

private:
    RingBuffer ring_;
    size_t   prefill_  = 0;
    bool     priming_  = true;
    uint64_t underrun_ = 0;
    uint64_t dropped_  = 0;
};

} // namespace audio
