#include "pch.h"
#include "VoiceEngine.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <chrono>

#pragma comment(lib, "ole32.lib")

namespace social {

// Our fixed transport format: 16 kHz mono 16-bit PCM.
static void FillFormat(WAVEFORMATEX& wf) {
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = VoiceEngine::Channels;
    wf.nSamplesPerSec  = VoiceEngine::SampleRate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = (wf.nChannels * wf.wBitsPerSample) / 8;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    wf.cbSize          = 0;
}

VoiceEngine::~VoiceEngine() {
    Stop();
}

bool VoiceEngine::Start(FrameCallback onCaptured) {
    if (m_running.exchange(true)) return true; // already running
    m_muted.store(false);
    { std::lock_guard<std::mutex> lk(m_playMtx); m_playBuf.clear(); }
    m_captureThread = std::thread(&VoiceEngine::CaptureLoop, this, std::move(onCaptured));
    m_renderThread  = std::thread(&VoiceEngine::RenderLoop, this);
    return true;
}

void VoiceEngine::Stop() {
    if (!m_running.exchange(false)) return;
    if (m_captureThread.joinable()) m_captureThread.join();
    if (m_renderThread.joinable())  m_renderThread.join();
    std::lock_guard<std::mutex> lk(m_playMtx);
    m_playBuf.clear();
}

void VoiceEngine::PushPlayback(const int16_t* samples, size_t count) {
    if (!m_running.load() || !samples || !count) return;
    std::lock_guard<std::mutex> lk(m_playMtx);
    // Drop oldest audio if the buffer is overrunning (peer faster than our clock).
    if (m_playBuf.size() + count > kMaxPlayBuf) {
        size_t drop = m_playBuf.size() + count - kMaxPlayBuf;
        m_playBuf.erase(m_playBuf.begin(), m_playBuf.begin() + std::min(drop, m_playBuf.size()));
    }
    m_playBuf.insert(m_playBuf.end(), samples, samples + count);
}

// ── Capture ──────────────────────────────────────────────────────────────────

void VoiceEngine::CaptureLoop(FrameCallback onCaptured) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInit = SUCCEEDED(hr);

    IMMDeviceEnumerator* enumr = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX wf{}; FillFormat(wf);

    auto cleanup = [&]() {
        if (capture) capture->Release();
        if (client)  client->Release();
        if (dev)     dev->Release();
        if (enumr)   enumr->Release();
        if (comInit) CoUninitialize();
    };

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               __uuidof(IMMDeviceEnumerator), (void**)&enumr))) { cleanup(); return; }
    if (FAILED(enumr->GetDefaultAudioEndpoint(eCapture, eCommunications, &dev))) { cleanup(); return; }
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) { cleanup(); return; }

    REFERENCE_TIME bufDur = 200000; // 20 ms in 100-ns units * 10 -> 200 ms buffer
    DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufDur, 0, &wf, nullptr))) { cleanup(); return; }
    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture))) { cleanup(); return; }
    if (FAILED(client->Start())) { cleanup(); return; }

    std::vector<int16_t> frame;
    frame.reserve(FrameSamples * 2);

    while (m_running.load()) {
        UINT32 packet = 0;
        if (FAILED(capture->GetNextPacketSize(&packet))) break;
        if (packet == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        while (packet > 0) {
            BYTE* data = nullptr; UINT32 frames = 0; DWORD bufFlags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &bufFlags, nullptr, nullptr))) break;
            const int16_t* s = reinterpret_cast<const int16_t*>(data);
            bool silent = (bufFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            for (UINT32 i = 0; i < frames; ++i)
                frame.push_back(silent ? 0 : s[i]);
            capture->ReleaseBuffer(frames);

            // Emit complete 20 ms frames.
            while (frame.size() >= (size_t)FrameSamples) {
                if (!m_muted.load() && onCaptured)
                    onCaptured(frame.data(), FrameSamples);
                frame.erase(frame.begin(), frame.begin() + FrameSamples);
            }
            if (FAILED(capture->GetNextPacketSize(&packet))) { packet = 0; break; }
        }
    }

    client->Stop();
    cleanup();
}

// ── Render ───────────────────────────────────────────────────────────────────

void VoiceEngine::RenderLoop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInit = SUCCEEDED(hr);

    IMMDeviceEnumerator* enumr = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX wf{}; FillFormat(wf);

    auto cleanup = [&]() {
        if (render) render->Release();
        if (client) client->Release();
        if (dev)    dev->Release();
        if (enumr)  enumr->Release();
        if (comInit) CoUninitialize();
    };

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               __uuidof(IMMDeviceEnumerator), (void**)&enumr))) { cleanup(); return; }
    if (FAILED(enumr->GetDefaultAudioEndpoint(eRender, eCommunications, &dev))) { cleanup(); return; }
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) { cleanup(); return; }

    REFERENCE_TIME bufDur = 1000000; // 100 ms render buffer
    DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufDur, 0, &wf, nullptr))) { cleanup(); return; }

    UINT32 bufFrames = 0;
    if (FAILED(client->GetBufferSize(&bufFrames))) { cleanup(); return; }
    if (FAILED(client->GetService(__uuidof(IAudioRenderClient), (void**)&render))) { cleanup(); return; }
    if (FAILED(client->Start())) { cleanup(); return; }

    while (m_running.load()) {
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) break;
        UINT32 avail = bufFrames - padding;
        if (avail == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        BYTE* out = nullptr;
        if (FAILED(render->GetBuffer(avail, &out))) break;
        int16_t* dst = reinterpret_cast<int16_t*>(out);
        UINT32 written = 0;
        {
            std::lock_guard<std::mutex> lk(m_playMtx);
            UINT32 n = (UINT32)std::min<size_t>(avail, m_playBuf.size());
            for (UINT32 i = 0; i < n; ++i) { dst[i] = m_playBuf.front(); m_playBuf.pop_front(); }
            written = n;
        }
        // Pad the remainder with silence on underrun.
        for (UINT32 i = written; i < avail; ++i) dst[i] = 0;
        render->ReleaseBuffer(avail, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    client->Stop();
    cleanup();
}

} // namespace social
