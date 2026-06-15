// AudioBufferSelfCheckMain.cpp — headless KAT for the portable PCM staging
// buffers (Linux port L5 audio foundation). Locks audio::RingBuffer (FIFO order,
// wrap-around, drop-oldest overflow, silence padding) and audio::JitterBuffer
// (prebuffer priming, in-order drain, underrun re-arm + counters). Pure logic, no
// audio device — exits 0 on success so CI can gate on it.
#include "AudioBuffer.h"

#include <cstdio>
#include <vector>

namespace {
int g_fail = 0;
void ck(const char* what, bool cond) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++g_fail; }
}
std::vector<int16_t> seq(int16_t start, size_t n) {
    std::vector<int16_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (int16_t)(start + (int16_t)i);
    return v;
}
} // namespace

int main() {
    using audio::RingBuffer;
    using audio::JitterBuffer;

    // 1. Basic FIFO write/read.
    {
        RingBuffer rb(8);
        auto in = seq(100, 5);
        ck("write no drop", rb.write(in.data(), 5) == 0);
        ck("size after write", rb.size() == 5);
        ck("space after write", rb.space() == 3);
        int16_t out[5] = {0};
        ck("read count", rb.read(out, 5) == 5);
        bool order = true;
        for (int i = 0; i < 5; ++i) order = order && out[i] == (int16_t)(100 + i);
        ck("FIFO order", order);
        ck("empty after drain", rb.empty());
    }

    // 2. Partial read returns only what's buffered.
    {
        RingBuffer rb(8);
        auto in = seq(1, 3);
        rb.write(in.data(), 3);
        int16_t out[8] = {0};
        ck("partial read count", rb.read(out, 8) == 3);
        ck("partial read drained", rb.empty());
    }

    // 3. Wrap-around: advance head, then a write that straddles the end.
    {
        RingBuffer rb(8);
        auto a = seq(1, 6); rb.write(a.data(), 6);
        int16_t tmp[4]; rb.read(tmp, 4);          // head now at 4, size 2
        auto b = seq(50, 5); rb.write(b.data(), 5); // writes across the wrap
        ck("wrap size", rb.size() == 7);
        int16_t out[7] = {0};
        rb.read(out, 7);
        // Remaining of a (5,6) then b (50..54).
        bool ok = out[0] == 5 && out[1] == 6 && out[2] == 50 && out[6] == 54;
        ck("wrap order", ok);
    }

    // 4. Overflow drops the OLDEST samples to fit the newest.
    {
        RingBuffer rb(4);
        auto a = seq(1, 4); rb.write(a.data(), 4);     // [1,2,3,4]
        auto b = seq(10, 2);
        ck("overflow drop count", rb.write(b.data(), 2) == 2);  // drop 1,2
        ck("overflow size", rb.size() == 4);
        int16_t out[4] = {0};
        rb.read(out, 4);
        bool ok = out[0] == 3 && out[1] == 4 && out[2] == 10 && out[3] == 11;
        ck("overflow keeps newest", ok);
    }

    // 5. Writing more than capacity keeps only the newest cap_ samples.
    {
        RingBuffer rb(4);
        auto big = seq(1, 10);   // 1..10
        size_t dropped = rb.write(big.data(), 10);
        ck("over-capacity drop count", dropped == 6);   // 10 in, keep 4
        ck("over-capacity size", rb.size() == 4);
        int16_t out[4] = {0};
        rb.read(out, 4);
        bool ok = out[0] == 7 && out[3] == 10;          // newest 4 (7,8,9,10)
        ck("over-capacity newest", ok);
    }

    // 6. readOrSilence zero-fills the shortfall.
    {
        RingBuffer rb(8);
        auto in = seq(20, 3); rb.write(in.data(), 3);
        int16_t out[6];
        for (int i = 0; i < 6; ++i) out[i] = -1;
        size_t real = rb.readOrSilence(out, 6);
        ck("silence real count", real == 3);
        ck("silence padded", out[3] == 0 && out[4] == 0 && out[5] == 0);
        ck("silence kept data", out[0] == 20 && out[2] == 22);
    }

    // 7. JitterBuffer primes: pulling before prefill yields silence, stays priming.
    {
        JitterBuffer jb(100, 10);
        auto a = seq(1, 5); jb.push(a.data(), 5);       // below prefill (10)
        ck("still priming", jb.priming());
        int16_t out[4];
        for (int i = 0; i < 4; ++i) out[i] = 7;
        ck("prime pull real=0", jb.pull(out, 4) == 0);
        ck("prime pull silent", out[0] == 0 && out[3] == 0);
        ck("prime underrun counted", jb.underrunSamples() == 4);
        ck("queue untouched while priming", jb.size() == 5);
    }

    // 8. Reaching prefill leaves priming; pull drains in order.
    {
        JitterBuffer jb(100, 8);
        auto a = seq(1, 8); jb.push(a.data(), 8);
        ck("primed at prefill", !jb.priming());
        int16_t out[8] = {0};
        ck("drain count", jb.pull(out, 8) == 8);
        bool ok = out[0] == 1 && out[7] == 8;
        ck("drain order", ok);
    }

    // 9. Underrun re-arms priming and counts the silence.
    {
        JitterBuffer jb(100, 4);
        auto a = seq(1, 6); jb.push(a.data(), 6);       // primed (>=4)
        ck("primed", !jb.priming());
        int16_t out[10] = {0};
        size_t real = jb.pull(out, 10);                 // only 6 available
        ck("underrun real", real == 6);
        ck("underrun re-arms priming", jb.priming());
        ck("underrun counted", jb.underrunSamples() == 4);
        // Next pull while re-priming with too little queued → silence again.
        auto b = seq(1, 2); jb.push(b.data(), 2);       // below prefill 4
        int16_t out2[3];
        ck("re-prime pull real=0", jb.pull(out2, 3) == 0);
        ck("re-prime still priming", jb.priming());
    }

    // 10. JitterBuffer overflow counts dropped samples.
    {
        JitterBuffer jb(4, 2);
        auto a = seq(1, 4); jb.push(a.data(), 4);
        auto b = seq(10, 3); jb.push(b.data(), 3);      // overflow ring(4) by 3
        ck("jitter dropped counted", jb.droppedSamples() == 3);
        ck("jitter size capped", jb.size() == 4);
    }

    if (g_fail == 0)
        std::printf("audio-buffer self-check: OK — ring FIFO/wrap/drop-oldest/"
                    "silence + jitter prime/drain/underrun/overflow KATs passed\n");
    else
        std::printf("audio-buffer self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
