// AudioBuffer.cpp — portable PCM staging buffers (Linux port L5). See AudioBuffer.h.
#include "AudioBuffer.h"

#include <algorithm>
#include <cstring>

namespace audio {

// ── RingBuffer ───────────────────────────────────────────────────────────────
void RingBuffer::reset(size_t capacitySamples) {
    buf_.assign(capacitySamples, 0);
    cap_ = capacitySamples;
    head_ = 0;
    size_ = 0;
}

void RingBuffer::clear() {
    head_ = 0;
    size_ = 0;
}

size_t RingBuffer::write(const int16_t* src, size_t n) {
    if (cap_ == 0 || n == 0) return 0;

    // Writing more than capacity: keep only the newest cap_ samples.
    size_t dropped = 0;
    if (n >= cap_) {
        dropped = size_ + (n - cap_);   // everything buffered + the overflow tail
        src += (n - cap_);
        n = cap_;
        head_ = 0;
        size_ = 0;
    } else if (n > space()) {
        // Drop just enough oldest samples to fit the new ones.
        size_t over = n - space();
        head_ = (head_ + over) % cap_;
        size_ -= over;
        dropped = over;
    }

    size_t tail = (head_ + size_) % cap_;
    size_t first = std::min(n, cap_ - tail);
    std::memcpy(&buf_[tail], src, first * sizeof(int16_t));
    if (n > first)
        std::memcpy(&buf_[0], src + first, (n - first) * sizeof(int16_t));
    size_ += n;
    return dropped;
}

size_t RingBuffer::read(int16_t* dst, size_t n) {
    size_t take = std::min(n, size_);
    if (take == 0) return 0;
    size_t first = std::min(take, cap_ - head_);
    std::memcpy(dst, &buf_[head_], first * sizeof(int16_t));
    if (take > first)
        std::memcpy(dst + first, &buf_[0], (take - first) * sizeof(int16_t));
    head_ = (head_ + take) % cap_;
    size_ -= take;
    return take;
}

size_t RingBuffer::readOrSilence(int16_t* dst, size_t n) {
    size_t got = read(dst, n);
    if (got < n)
        std::memset(dst + got, 0, (n - got) * sizeof(int16_t));
    return got;
}

// ── JitterBuffer ─────────────────────────────────────────────────────────────
void JitterBuffer::reset(size_t capacitySamples, size_t prefillSamples) {
    ring_.reset(capacitySamples);
    prefill_  = std::min(prefillSamples, capacitySamples);
    priming_  = true;
    underrun_ = 0;
    dropped_  = 0;
}

void JitterBuffer::clear() {
    ring_.clear();
    priming_ = true;
}

void JitterBuffer::push(const int16_t* src, size_t n) {
    dropped_ += ring_.write(src, n);
    // Leave priming as soon as we've accumulated the target prebuffer.
    if (priming_ && ring_.size() >= prefill_) priming_ = false;
}

size_t JitterBuffer::pull(int16_t* dst, size_t n) {
    if (n == 0) return 0;

    // Still waiting for the prebuffer to fill: output silence, no drain.
    if (priming_) {
        std::memset(dst, 0, n * sizeof(int16_t));
        underrun_ += n;
        return 0;
    }

    size_t got = ring_.read(dst, n);
    if (got < n) {
        // Underrun: silence the shortfall and re-arm prebuffering so we wait for
        // the queue to refill before resuming (avoids choppy single-frame drains).
        std::memset(dst + got, 0, (n - got) * sizeof(int16_t));
        underrun_ += (n - got);
        priming_ = true;
    }
    return got;
}

} // namespace audio
