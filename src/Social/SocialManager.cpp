#include "pch.h"
#include "SocialManager.h"
#include "SocialJson.h"
#include <winhttp.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <cstring>

#pragma comment(lib, "winhttp.lib")

namespace social {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static int64_t NowSecs() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
static int64_t NowMs() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

SocialManager::~SocialManager() {
    Stop();
}

void SocialManager::Start(const std::wstring& baseUrl, const std::wstring& token) {
    Stop();
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_baseUrl = baseUrl;
        m_token = token;
        m_friends.clear();
        m_convos.clear();
    }
    if (baseUrl.empty() || token.empty()) return;
    m_running.store(true);
    m_backoffMs.store(1000);
    RefreshFriends();
    OpenGateway();
}

void SocialManager::Stop() {
    m_running.store(false);
    m_reconnectGen.fetch_add(1);
    StopVoiceMedia();
    m_ws.Close();
    m_gateway.store(GatewayState::Disconnected);
    m_voiceState.store(VoiceState::Idle);
    m_voicePeer.store(0);
}

void SocialManager::FireChanged() {
    if (m_onChanged) m_onChanged();
}

// ── URL helpers ─────────────────────────────────────────────────────────────

std::wstring SocialManager::WsUrl() const {
    std::wstring base;
    std::wstring token;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        base = m_baseUrl;
        token = m_token;
    }
    // http -> ws, https -> wss. Append the gateway path + token query param.
    std::wstring scheme = L"ws://";
    std::wstring rest = base;
    if (rest.rfind(L"https://", 0) == 0) { scheme = L"wss://"; rest = rest.substr(8); }
    else if (rest.rfind(L"http://", 0) == 0) { scheme = L"ws://"; rest = rest.substr(7); }
    while (!rest.empty() && rest.back() == L'/') rest.pop_back();
    return scheme + rest + L"/ws/social?token=" + token;
}

// ── Gateway ─────────────────────────────────────────────────────────────────

void SocialManager::OpenGateway() {
    if (!m_running.load()) return;
    m_gateway.store(GatewayState::Connecting);
    FireChanged();
    std::wstring url = WsUrl();
    m_ws.SetBinaryHandler([this](const void* data, size_t len) { HandleAudioFrame(data, len); });
    m_ws.Connect(url,
        [this](const std::string& msg) { HandleGatewayFrame(msg); },
        [this](bool connected) { OnGatewaySocketState(connected); });
}

void SocialManager::OnGatewaySocketState(bool connected) {
    if (connected) {
        m_gateway.store(GatewayState::Connected);
        m_backoffMs.store(1000);
        // Re-announce presence on (re)connect so the server reflects our state.
        PresenceState p;
        std::wstring gid, gtitle;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            p = m_presence; gid = m_curGameId; gtitle = m_curGameTitle;
        }
        if (p == PresenceState::InGame) SetInGame(gid, gtitle);
        else SetPresence(p);
        FireChanged();
    } else {
        m_gateway.store(GatewayState::Disconnected);
        FireChanged();
        if (m_running.load()) ScheduleReconnect();
    }
}

void SocialManager::ScheduleReconnect() {
    int gen = m_reconnectGen.fetch_add(1) + 1;
    int delay = m_backoffMs.load();
    m_backoffMs.store(delay < 30000 ? delay * 2 : 30000); // exponential backoff, cap 30s
    m_gateway.store(GatewayState::Reconnecting);
    std::thread([this, gen, delay]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (m_running.load() && m_reconnectGen.load() == gen)
            OpenGateway();
    }).detach();
}

bool SocialManager::SendGatewayJson(const std::string& json) {
    return m_ws.SendText(json);
}

void SocialManager::HandleGatewayFrame(const std::string& utf8) {
    JsonValue v = JsonValue::Parse(utf8);
    if (!v.isObject()) return;
    std::string type = v["type"].asString();

    if (type == "hello") {
        m_selfId.store(v["selfId"].asUint());
        FireChanged();
        return;
    }
    if (type == "pong") {
        return;
    }
    if (type == "presence") {
        uint64_t uid = v["userId"].asUint();
        PresenceState ps = PresenceFromString(v["state"].asString());
        std::wstring gid = Utf8ToWide(v["gameId"].asString());
        std::wstring gt  = Utf8ToWide(v["gameTitle"].asString());
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if (FriendInfo* f = FindFriendLocked(uid)) {
                f->presence = ps;
                f->currentGameId = gid;
                f->currentGameTitle = gt;
                if (ps != PresenceState::Offline) f->lastOnline = NowSecs();
            }
        }
        FireChanged();
        return;
    }
    if (type == "typing") {
        uint64_t from = v["fromId"].asUint();
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            Conversation& c = ConvLocked(from);
            c.peerTyping = true;
            c.peerTypingUntil = NowMs() + 6000;
        }
        FireChanged();
        return;
    }
    if (type == "chat") {
        ChatMessage m;
        m.messageId  = v["messageId"].asUint();
        m.senderId   = v["senderId"].asUint();
        m.receiverId = v["receiverId"].asUint();
        m.messageText = Utf8ToWide(v["text"].asString());
        m.timestamp  = v["timestamp"].asInt();
        uint64_t self = m_selfId.load();
        uint64_t peer = (m.senderId == self) ? m.receiverId : m.senderId;
        m.isRead = (m.senderId == self);
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            Conversation& c = ConvLocked(peer);
            c.peerTyping = false;
            // Replace a matching pending echo if present, else append.
            bool replaced = false;
            for (auto& em : c.messages) {
                if (em.pending && em.senderId == m.senderId &&
                    em.messageText == m.messageText) {
                    em = m; replaced = true; break;
                }
            }
            if (!replaced) {
                c.messages.push_back(m);
                if (m.senderId != self) c.unread++;
            }
        }
        FireChanged();
        return;
    }
    if (type == "friend_request" || type == "friend_accepted" ||
        type == "friend_removed") {
        // Relationship changed; re-pull the authoritative list.
        RefreshFriends();
        return;
    }
    if (type == "voice_signal") {
        uint64_t from = v["fromId"].asUint();
        std::string kind = v["payload"]["kind"].asString();
        if (kind == "invite") {
            if (m_voiceState.load() == VoiceState::Idle) {
                m_voicePeer.store(from);
                m_voiceState.store(VoiceState::Negotiating);
                FireChanged();
            }
        } else if (kind == "accept") {
            if (m_voicePeer.load() == from) {
                m_voiceState.store(VoiceState::Connected);
                StartVoiceMedia();
                FireChanged();
            }
        } else if (kind == "end") {
            if (m_voicePeer.load() == from) {
                StopVoiceMedia();
                m_voiceState.store(VoiceState::Idle);
                m_voicePeer.store(0);
                FireChanged();
            }
        }
        return;
    }
}

// ── REST ────────────────────────────────────────────────────────────────────

static bool CrackAndOpen(const std::wstring& url, const std::wstring& verb,
                         HINTERNET& sess, HINTERNET& conn, HINTERNET& req, bool& secure) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}; wchar_t path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2047;
    uc.dwSchemeLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    sess = WinHttpOpen(L"ArcadeLauncher/Social",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) return false;
    conn = WinHttpConnect(sess, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(sess); return false; }
    req = WinHttpOpenRequest(conn, verb.c_str(), path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(sess); return false; }
    return true;
}

static bool ReadAll(HINTERNET req, std::string& body) {
    if (!WinHttpReceiveResponse(req, nullptr)) return false;
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    body.clear();
    DWORD avail = 0;
    do {
        avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        std::string buf(avail, 0);
        DWORD read = 0;
        if (!WinHttpReadData(req, &buf[0], avail, &read)) break;
        body.append(buf.data(), read);
    } while (avail > 0);
    return status >= 200 && status < 300;
}

bool SocialManager::HttpGet(const std::wstring& path, std::string& body) {
    std::wstring base, token;
    { std::lock_guard<std::mutex> lk(m_mtx); base = m_baseUrl; token = m_token; }
    std::wstring url = base + path;
    HINTERNET sess = nullptr, conn = nullptr, req = nullptr; bool secure = false;
    if (!CrackAndOpen(url, L"GET", sess, conn, req, secure)) return false;
    std::wstring hdr = L"Authorization: Bearer " + token + L"\r\n";
    WinHttpAddRequestHeaders(req, hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && ReadAll(req, body);
    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
    return ok;
}

bool SocialManager::HttpPostJson(const std::wstring& path, const std::string& json, std::string& body) {
    std::wstring base, token;
    { std::lock_guard<std::mutex> lk(m_mtx); base = m_baseUrl; token = m_token; }
    std::wstring url = base + path;
    HINTERNET sess = nullptr, conn = nullptr, req = nullptr; bool secure = false;
    if (!CrackAndOpen(url, L"POST", sess, conn, req, secure)) return false;
    std::wstring hdr = L"Authorization: Bearer " + token + L"\r\nContent-Type: application/json\r\n";
    WinHttpAddRequestHeaders(req, hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 (LPVOID)json.data(), (DWORD)json.size(),
                                 (DWORD)json.size(), 0) && ReadAll(req, body);
    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
    return ok;
}

// ── Friends ─────────────────────────────────────────────────────────────────

void SocialManager::RefreshFriends() {
    if (!m_running.load()) return;
    std::thread([this]() {
        std::string body;
        if (!HttpGet(L"/api/social/friends", body)) return;
        JsonValue v = JsonValue::Parse(body);
        const JsonValue& arr = v["friends"];
        std::vector<FriendInfo> next;
        if (arr.isArray()) {
            for (const auto& f : arr.arr) {
                FriendInfo fi;
                fi.accountId = f["accountId"].asUint();
                fi.username  = Utf8ToWide(f["username"].asString());
                fi.presence  = PresenceFromString(f["presence"].asString());
                std::string rel = f["relation"].asString();
                fi.relationStatus =
                    rel == "accepted"         ? FriendStatus::Accepted :
                    rel == "request_sent"     ? FriendStatus::RequestSent :
                    rel == "request_received" ? FriendStatus::RequestReceived :
                                                FriendStatus::None;
                fi.currentGameId    = Utf8ToWide(f["currentGameId"].asString());
                fi.currentGameTitle = Utf8ToWide(f["currentGameTitle"].asString());
                next.push_back(std::move(fi));
            }
        }
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_friends = std::move(next);
        }
        FireChanged();
    }).detach();
}

void SocialManager::SendFriendRequest(const std::wstring& username) {
    std::string u = WideToUtf8(username);
    std::thread([this, u]() {
        std::string body;
        std::string json = std::string("{\"username\":\"") + JsonEscape(u) + "\"}";
        HttpPostJson(L"/api/social/friends/request", json, body);
        RefreshFriends();
    }).detach();
}

void SocialManager::RespondRequest(uint64_t userId, const std::string& action) {
    std::thread([this, userId, action]() {
        std::ostringstream os;
        os << "{\"userId\":" << userId << ",\"action\":\"" << action << "\"}";
        std::string body;
        HttpPostJson(L"/api/social/friends/respond", os.str(), body);
        RefreshFriends();
    }).detach();
}

void SocialManager::BlockUser(uint64_t userId, bool block) {
    std::thread([this, userId, block]() {
        std::ostringstream os;
        os << "{\"userId\":" << userId << ",\"block\":" << (block ? "true" : "false") << "}";
        std::string body;
        HttpPostJson(L"/api/social/friends/block", os.str(), body);
        RefreshFriends();
    }).detach();
}

std::vector<FriendInfo> SocialManager::GetFriends() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_friends;
}

FriendInfo* SocialManager::FindFriendLocked(uint64_t id) {
    for (auto& f : m_friends) if (f.accountId == id) return &f;
    return nullptr;
}

// ── Presence ────────────────────────────────────────────────────────────────

void SocialManager::SetPresence(PresenceState state) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_presence = state;
        if (state != PresenceState::InGame) { m_curGameId.clear(); m_curGameTitle.clear(); }
    }
    std::ostringstream os;
    os << "{\"type\":\"presence\",\"state\":\"" << PresenceWire(state) << "\"}";
    SendGatewayJson(os.str());
}

void SocialManager::SetInGame(const std::wstring& gameId, const std::wstring& gameTitle) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_presence = PresenceState::InGame;
        m_curGameId = gameId;
        m_curGameTitle = gameTitle;
    }
    std::ostringstream os;
    os << "{\"type\":\"presence\",\"state\":\"ingame\",\"gameId\":\""
       << JsonEscape(WideToUtf8(gameId)) << "\",\"gameTitle\":\""
       << JsonEscape(WideToUtf8(gameTitle)) << "\"}";
    SendGatewayJson(os.str());
}

void SocialManager::ClearInGame() {
    SetPresence(PresenceState::Online);
}

// ── Chat ────────────────────────────────────────────────────────────────────

Conversation& SocialManager::ConvLocked(uint64_t peerId) {
    auto it = m_convos.find(peerId);
    if (it == m_convos.end()) {
        Conversation c; c.peerId = peerId;
        it = m_convos.emplace(peerId, std::move(c)).first;
    }
    return it->second;
}

void SocialManager::OpenConversation(uint64_t peerId) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ConvLocked(peerId).unread = 0;
    }
    std::thread([this, peerId]() {
        std::wstring path = L"/api/social/messages/" + std::to_wstring(peerId);
        std::string body;
        if (!HttpGet(path, body)) return;
        JsonValue v = JsonValue::Parse(body);
        const JsonValue& arr = v["messages"];
        uint64_t self = m_selfId.load();
        std::vector<ChatMessage> hist;
        if (arr.isArray()) {
            for (const auto& m : arr.arr) {
                ChatMessage cm;
                cm.messageId  = m["messageId"].asUint();
                cm.senderId   = m["senderId"].asUint();
                cm.receiverId = m["receiverId"].asUint();
                cm.messageText = Utf8ToWide(m["text"].asString());
                cm.timestamp  = m["timestamp"].asInt();
                cm.isRead     = m["isRead"].asBool() || cm.senderId == self;
                hist.push_back(std::move(cm));
            }
        }
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            Conversation& c = ConvLocked(peerId);
            c.messages = std::move(hist);
            c.unread = 0;
        }
        FireChanged();
    }).detach();
}

void SocialManager::SendChat(uint64_t peerId, const std::wstring& text) {
    if (text.empty()) return;
    // Optimistic local echo (pending) for instant feedback; replaced on gateway ack.
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ChatMessage m;
        m.senderId = m_selfId.load();
        m.receiverId = peerId;
        m.messageText = text;
        m.timestamp = NowSecs();
        m.isRead = true;
        m.pending = true;
        ConvLocked(peerId).messages.push_back(std::move(m));
    }
    FireChanged();
    std::ostringstream os;
    os << "{\"type\":\"chat\",\"to\":" << peerId << ",\"text\":\""
       << JsonEscape(WideToUtf8(text)) << "\"}";
    SendGatewayJson(os.str());
}

void SocialManager::NotifyTyping(uint64_t peerId) {
    std::ostringstream os;
    os << "{\"type\":\"typing\",\"to\":" << peerId << "}";
    SendGatewayJson(os.str());
}

Conversation SocialManager::GetConversation(uint64_t peerId) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_convos.find(peerId);
    if (it == m_convos.end()) { Conversation c; c.peerId = peerId; return c; }
    Conversation copy = it->second;
    // Expire a stale typing indicator on read.
    if (copy.peerTyping && NowMs() > copy.peerTypingUntil) copy.peerTyping = false;
    return copy;
}

int SocialManager::TotalUnread() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    int n = 0;
    for (auto& kv : m_convos) n += kv.second.unread;
    return n;
}

// ── Voice signaling ─────────────────────────────────────────────────────────
// State machine + frame relay over the gateway. Media capture/transport is a
// separate scaffolded stage; the signaling here establishes call intent and the
// negotiated state both peers agree on (Idle -> Connecting -> Negotiating ->
// Connected -> Disconnected/Failed) which a media layer plugs into.

void SocialManager::SendVoiceSignal(uint64_t peerId, const std::string& kind) {
    std::ostringstream os;
    os << "{\"type\":\"voice_signal\",\"to\":" << peerId
       << ",\"payload\":{\"kind\":\"" << kind << "\"}}";
    SendGatewayJson(os.str());
}

void SocialManager::StartVoiceCall(uint64_t peerId) {
    if (m_voiceState.load() != VoiceState::Idle) return;
    m_voicePeer.store(peerId);
    m_voiceState.store(VoiceState::Connecting);
    SendVoiceSignal(peerId, "invite");
    FireChanged();
}

void SocialManager::AcceptVoiceCall(uint64_t peerId) {
    if (m_voicePeer.load() != peerId) return;
    m_voiceState.store(VoiceState::Connected);
    SendVoiceSignal(peerId, "accept");
    StartVoiceMedia();
    FireChanged();
}

void SocialManager::EndVoiceCall() {
    uint64_t peer = m_voicePeer.load();
    if (peer) SendVoiceSignal(peer, "end");
    StopVoiceMedia();
    m_voiceState.store(VoiceState::Idle);
    m_voicePeer.store(0);
    FireChanged();
}

// ── Voice media (WASAPI capture/render + PCM relay) ──────────────────────────

void SocialManager::StartVoiceMedia() {
    if (m_voice.IsRunning()) return;
    m_voice.Start([this](const int16_t* samples, size_t count) {
        uint64_t peer = m_voicePeer.load();
        if (!peer) return;
        // Frame = [u64 LE peer id][raw S16 PCM]. The server swaps the header to
        // our id before forwarding, so the recipient learns who is speaking.
        std::vector<uint8_t> frame;
        frame.resize(8 + count * sizeof(int16_t));
        std::memcpy(frame.data(), &peer, 8);
        std::memcpy(frame.data() + 8, samples, count * sizeof(int16_t));
        m_ws.SendBinary(frame.data(), frame.size());
    });
}

void SocialManager::StopVoiceMedia() {
    m_voice.Stop();
}

void SocialManager::HandleAudioFrame(const void* data, size_t len) {
    if (len < 8) return;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t from = 0;
    std::memcpy(&from, p, 8);
    // Only render audio from the peer we're actually in a call with.
    if (from != m_voicePeer.load() || m_voiceState.load() != VoiceState::Connected) return;
    size_t pcmBytes = len - 8;
    size_t count = pcmBytes / sizeof(int16_t);
    if (count == 0) return;
    m_voice.PushPlayback(reinterpret_cast<const int16_t*>(p + 8), count);
}

} // namespace social
