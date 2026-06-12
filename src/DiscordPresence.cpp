#include "pch.h"
#include "DiscordPresence.h"
#include <chrono>

namespace {

// Minimal UTF-16 -> UTF-8 (the project hand-rolls these conversions elsewhere;
// kept local so this unit has no cross-file dependency).
std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Escape a UTF-8 string for embedding in a JSON string literal.
std::string JsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    return out;
}

int64_t NowEpoch() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

DiscordPresence::~DiscordPresence() {
    Stop();
}

void DiscordPresence::Start(const std::wstring& clientId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_clientId = Utf8(clientId);
    // Connection is established lazily on the first activity push.
}

void DiscordPresence::Stop() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_pipe != INVALID_HANDLE_VALUE && m_handshaked) {
        // Clear the activity so we don't leave a stale "Playing" behind.
        std::string nonce = std::to_string(++m_nonce);
        WriteFrame(1, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
                          std::to_string(GetCurrentProcessId()) +
                          ",\"activity\":null},\"nonce\":\"" + nonce + "\"}");
    }
    Disconnect();
    m_clientId.clear();
}

void DiscordPresence::Disconnect() {
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    m_handshaked = false;
}

bool DiscordPresence::EnsureConnected() {
    if (m_clientId.empty()) return false;
    if (m_pipe != INVALID_HANDLE_VALUE && m_handshaked) return true;

    // Discord exposes pipes discord-ipc-0 .. discord-ipc-9 (one per running
    // client instance). Try each until one connects.
    for (int i = 0; i < 10 && m_pipe == INVALID_HANDLE_VALUE; ++i) {
        std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) m_pipe = h;
    }
    if (m_pipe == INVALID_HANDLE_VALUE) return false;

    // Opcode 0 = handshake. v must be 1.
    std::string hello = "{\"v\":1,\"client_id\":\"" + JsonEscape(m_clientId) + "\"}";
    if (!WriteFrame(0, hello)) {
        Disconnect();
        return false;
    }
    m_handshaked = true;
    DrainReplies();
    return true;
}

bool DiscordPresence::WriteFrame(uint32_t opcode, const std::string& json) {
    if (m_pipe == INVALID_HANDLE_VALUE) return false;
    uint32_t len = (uint32_t)json.size();
    std::string frame;
    frame.resize(8 + json.size());
    memcpy(frame.data(),     &opcode, 4);   // little-endian on x86/x64
    memcpy(frame.data() + 4, &len,    4);
    memcpy(frame.data() + 8, json.data(), json.size());

    DWORD written = 0;
    if (!WriteFile(m_pipe, frame.data(), (DWORD)frame.size(), &written, nullptr) ||
        written != frame.size()) {
        Disconnect();   // broken pipe (Discord closed) — reconnect next time
        return false;
    }
    return true;
}

// Discord replies to every frame; we don't need the contents, but unread bytes
// would eventually stall the pipe. Discard whatever is currently buffered without
// blocking.
void DiscordPresence::DrainReplies() {
    if (m_pipe == INVALID_HANDLE_VALUE) return;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
            return;
        char buf[1024];
        DWORD read = 0;
        DWORD want = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
        if (!ReadFile(m_pipe, buf, want, &read, nullptr) || read == 0)
            return;
    }
}

bool DiscordPresence::SendActivity(const std::string& activityJson) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!EnsureConnected()) return false;
    DrainReplies();
    std::string nonce = std::to_string(++m_nonce);
    std::string payload =
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
        std::to_string(GetCurrentProcessId()) +
        ",\"activity\":" + activityJson + "},\"nonce\":\"" + nonce + "\"}";
    return WriteFrame(1, payload);
}

void DiscordPresence::SetPlaying(const std::wstring& title, const std::wstring& details) {
    m_startEpoch = NowEpoch();
    std::string state = "Playing " + JsonEscape(Utf8(title));
    std::string activity =
        "{\"state\":\"" + state + "\"";
    if (!details.empty())
        activity += ",\"details\":\"" + JsonEscape(Utf8(details)) + "\"";
    activity += ",\"timestamps\":{\"start\":" + std::to_string(m_startEpoch) + "}";
    activity += ",\"assets\":{\"large_image\":\"icon\",\"large_text\":\"ArcadeLauncher\"}";
    activity += "}";
    SendActivity(activity);
}

void DiscordPresence::SetIdle() {
    m_startEpoch = 0;
    std::string activity =
        "{\"state\":\"In the library\""
        ",\"assets\":{\"large_image\":\"icon\",\"large_text\":\"ArcadeLauncher\"}}";
    SendActivity(activity);
}
