#include "pch.h"
#include "SocialManager.h"
#include "SocialJson.h"
#include <winhttp.h>
#include <shlobj.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

namespace social {

// Toggle one account's reaction on a message (defined below; used by the gateway
// frame handler above its definition).
static void ApplyReactionLocked(ChatMessage& m, uint64_t userId,
                                const std::wstring& emoji, bool on);

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

// Lock-free monotonic max — tracks the highest message id we've observed so a
// reconnect can ask the server to backfill only newer messages.
static void AtomicMax(std::atomic<uint64_t>& a, uint64_t v) {
    uint64_t cur = a.load();
    while (v > cur && !a.compare_exchange_weak(cur, v)) {}
}

SocialManager::~SocialManager() {
    Stop();
}

// Ensure the origin carries an explicit scheme. The config may store a bare
// host ("arcade.orlandoaio.net"); without a scheme WsUrl() would fall through to
// plaintext ws:// (port 80, which the proxy 301-redirects -> the upgrade fails)
// and the REST helpers' WinHttpCrackUrl would reject the URL outright. Mirror
// ServerClient's normalization: https:// for public hosts, http:// for local.
static std::wstring NormalizeOrigin(std::wstring url) {
    while (!url.empty() && url.back() == L'/') url.pop_back();
    if (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0)
        return url;
    if (url.empty()) return url;
    bool local = url.rfind(L"10.", 0) == 0 || url.rfind(L"192.168.", 0) == 0 ||
                 url.rfind(L"127.", 0) == 0 || url.rfind(L"localhost", 0) == 0;
    return (local ? L"http://" : L"https://") + url;
}

void SocialManager::Start(const std::wstring& baseUrl, const std::wstring& token) {
    Stop();
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_baseUrl = NormalizeOrigin(baseUrl);
        m_token = token;
        m_friends.clear();
        m_convos.clear();
        m_friendsLoaded = false;
    }
    if (baseUrl.empty() || token.empty()) return;
    m_running.store(true);
    m_backoffMs.store(1000);
    m_everConnected.store(false);
    LoadPrefs();              // fast local cache first (offline-friendly)
    PullPrefsFromServer();    // then adopt the server's authoritative copy (0.5)
    RefreshFriends();
    RefreshNotifications();
    PullFriendPolicy();       // adopt server's friend-request privacy setting (1.1)
    OpenGateway();
}

void SocialManager::Stop() {
    m_running.store(false);
    m_reconnectGen.fetch_add(1);
    StopHeartbeat();
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
        // On a *resume* (not the first connect) reconcile state that may have
        // changed while we were offline: friend list/presence + missed messages.
        if (m_everConnected.exchange(true))
            ReconcileAfterReconnect();
        // Start the application-level heartbeat so the WS receive loop keeps
        // getting data frames (server "pong") and never hits its idle timeout.
        StartHeartbeat();
        FireChanged();
    } else {
        StopHeartbeat();
        m_gateway.store(GatewayState::Disconnected);
        FireChanged();
        if (m_running.load()) ScheduleReconnect();
    }
}

void SocialManager::StartHeartbeat() {
    int gen = m_heartbeatGen.fetch_add(1) + 1;
    std::thread([this, gen]() {
        // Ping every 20s. The server replies with a data-frame {"type":"pong"}
        // (social_api.rs), which wakes WinHttpWebSocketReceive and resets its
        // 45s receive timeout. Without this, an idle gateway would time out and
        // drop into a perpetual reconnect loop.
        while (m_running.load() && m_heartbeatGen.load() == gen) {
            for (int i = 0; i < 20 && m_running.load() &&
                            m_heartbeatGen.load() == gen; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!m_running.load() || m_heartbeatGen.load() != gen) break;
            if (!SendGatewayJson("{\"type\":\"ping\"}")) break; // socket gone
        }
    }).detach();
}

void SocialManager::StopHeartbeat() {
    m_heartbeatGen.fetch_add(1); // invalidate any running ping loop
}

void SocialManager::ReconcileAfterReconnect() {
    // 1) Authoritative friend list + presence (also raises toasts for requests
    //    that arrived while we were disconnected, via RefreshFriends' diff).
    RefreshFriends();
    // 1b) Reconcile the persisted notification feed (read state + any rows the
    //     gateway backlog batch didn't include, e.g. already-read ones).
    RefreshNotifications();

    // 2) Backfill only the messages we missed while offline. Instead of re-pulling
    //    every open conversation's full history (one REST round-trip each), send a
    //    single resume frame; the server replies with one `chat_backfill` batch of
    //    everything newer than the highest id we've seen. See HandleGatewayFrame.
    std::ostringstream os;
    os << "{\"type\":\"resume\",\"afterMsgId\":" << m_lastMsgId.load() << "}";
    SendGatewayJson(os.str());
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
        NotifKind toastKind = NotifKind::System; bool emitToast = false;
        std::wstring who, bodyTxt;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if (FriendInfo* f = FindFriendLocked(uid)) {
                PresenceState before = f->presence;
                std::wstring beforeGame = f->currentGameId;
                f->presence = ps;
                f->currentGameId = gid;
                f->currentGameTitle = gt;
                if (ps != PresenceState::Offline) f->lastOnline = NowSecs();
                who = !f->nickname.empty() ? f->nickname : f->username;
                bool wasVisible = (before != PresenceState::Offline &&
                                   before != PresenceState::Invisible);
                // Presence toasts are favorites-only and gated by the user pref.
                bool fav = f->favorite && m_prefOnlineAlerts;
                // Friend started a game (new game or fresh launch).
                if (fav && ps == PresenceState::InGame && (before != PresenceState::InGame ||
                                                    beforeGame != gid) && !gt.empty()) {
                    toastKind = NotifKind::FriendInGame; emitToast = true;
                    bodyTxt = L"started playing " + gt;
                } else if (fav && !wasVisible && (ps == PresenceState::Online ||
                                           ps == PresenceState::Away ||
                                           ps == PresenceState::Busy)) {
                    toastKind = NotifKind::FriendOnline; emitToast = true;
                    bodyTxt = L"came online";
                }
            }
        }
        if (emitToast) Notify(toastKind, uid, who, bodyTxt);
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
        AtomicMax(m_lastMsgId, m.messageId);
        uint64_t self = m_selfId.load();
        uint64_t peer = (m.senderId == self) ? m.receiverId : m.senderId;
        m.isRead = (m.senderId == self);
        bool emitToast = false; std::wstring who, preview;
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
                if (m.senderId != self) {
                    c.unread++;
                    emitToast = true;
                    if (FriendInfo* f = FindFriendLocked(m.senderId))
                        who = !f->nickname.empty() ? f->nickname : f->username;
                    preview = m.messageText.size() > 80
                                  ? m.messageText.substr(0, 80) + L"…" : m.messageText;
                }
            }
        }
        // Caller (App) suppresses the toast if this peer's chat is already open.
        if (emitToast) Notify(NotifKind::Message, peer, who.empty() ? L"New message" : who, preview);
        FireChanged();
        return;
    }
    if (type == "read") {
        // Peer read my messages up to upToId. Mark my outgoing msgs read (1.2a).
        uint64_t reader = v["readerId"].asUint();
        uint64_t upTo   = v["upToId"].asUint();
        uint64_t self   = m_selfId.load();
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            Conversation& c = ConvLocked(reader);
            if (upTo > c.readUpTo) c.readUpTo = upTo;
            for (auto& m : c.messages) {
                if (m.senderId == self && m.messageId != 0 && m.messageId <= upTo)
                    m.isRead = true;
            }
        }
        FireChanged();
        return;
    }
    if (type == "chat_edit") {
        uint64_t mid = v["messageId"].asUint();
        std::wstring text = Utf8ToWide(v["text"].asString());
        int64_t editedAt = v["editedAt"].asInt();
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            for (auto& kv : m_convos) {
                for (auto& m : kv.second.messages) {
                    if (m.messageId == mid) {
                        m.messageText = text;
                        m.editedAt = editedAt ? editedAt : NowSecs();
                        m.deleted = false;
                    }
                }
            }
        }
        FireChanged();
        return;
    }
    if (type == "chat_delete") {
        uint64_t mid = v["messageId"].asUint();
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            for (auto& kv : m_convos)
                for (auto& m : kv.second.messages)
                    if (m.messageId == mid) m.deleted = true;
        }
        FireChanged();
        return;
    }
    if (type == "reaction") {
        uint64_t mid   = v["messageId"].asUint();
        uint64_t uid   = v["userId"].asUint();
        std::wstring emoji = Utf8ToWide(v["emoji"].asString());
        bool on        = v["on"].asBool();
        if (!emoji.empty()) {
            std::lock_guard<std::mutex> lk(m_mtx);
            for (auto& kv : m_convos)
                for (auto& m : kv.second.messages)
                    if (m.messageId == mid) ApplyReactionLocked(m, uid, emoji, on);
        }
        FireChanged();
        return;
    }
    if (type == "chat_backfill") {
        // Resume reply: messages missed while we were offline. Merge each into its
        // conversation (dedup by id, replace pending echoes), bump unread for new
        // inbound, but never toast — this is catch-up, not a live event.
        const JsonValue& arr = v["messages"];
        if (arr.isArray()) {
            uint64_t self = m_selfId.load();
            std::lock_guard<std::mutex> lk(m_mtx);
            for (const auto& jm : arr.arr) {
                ChatMessage m;
                m.messageId  = jm["messageId"].asUint();
                m.senderId   = jm["senderId"].asUint();
                m.receiverId = jm["receiverId"].asUint();
                m.messageText = Utf8ToWide(jm["text"].asString());
                m.timestamp  = jm["timestamp"].asInt();
                m.isRead     = jm["isRead"].asBool() || m.senderId == self;
                AtomicMax(m_lastMsgId, m.messageId);
                uint64_t peer = (m.senderId == self) ? m.receiverId : m.senderId;
                Conversation& c = ConvLocked(peer);
                // Already present (by id) or matches a pending echo? replace/skip.
                bool handled = false;
                for (auto& em : c.messages) {
                    if (em.messageId == m.messageId) { em = m; handled = true; break; }
                    if (em.pending && em.senderId == m.senderId &&
                        em.messageText == m.messageText) { em = m; handled = true; break; }
                }
                if (handled) continue;
                c.messages.push_back(m);
                if (!m.isRead && m.senderId != self) c.unread++;
            }
        }
        FireChanged();
        return;
    }
    if (type == "notification") {
        // Live persisted notification pushed by the server. Merge + toast.
        std::vector<Notification> toasts;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            IngestServerNotificationLocked(v, /*emitToast=*/true, toasts);
        }
        for (auto& n : toasts) {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_toastQueue.push_back(std::move(n));
        }
        FireChanged();
        return;
    }
    if (type == "notifications") {
        // Backlog batch delivered on (re)connect. Merge silently — no toast
        // storm for things the user may have already seen elsewhere.
        const JsonValue& items = v["items"];
        if (items.isArray()) {
            std::vector<Notification> ignore;
            std::lock_guard<std::mutex> lk(m_mtx);
            for (const auto& it : items.arr)
                IngestServerNotificationLocked(it, /*emitToast=*/false, ignore);
        }
        FireChanged();
        return;
    }
    if (type == "friend_request" || type == "friend_accepted" ||
        type == "friend_removed") {
        // Relationship changed; re-pull the authoritative list. The persisted
        // notification (if any) arrives separately as a "notification" frame.
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
                std::wstring who;
                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    if (FriendInfo* f = FindFriendLocked(from))
                        who = !f->nickname.empty() ? f->nickname : f->username;
                }
                Notify(NotifKind::VoiceInvite, from, who.empty() ? L"Voice call" : who,
                       L"is calling you");
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

bool SocialManager::HttpPut(const std::wstring& path, const std::string& json, std::string& body) {
    std::wstring base, token;
    { std::lock_guard<std::mutex> lk(m_mtx); base = m_baseUrl; token = m_token; }
    std::wstring url = base + path;
    HINTERNET sess = nullptr, conn = nullptr, req = nullptr; bool secure = false;
    if (!CrackAndOpen(url, L"PUT", sess, conn, req, secure)) return false;
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
        // Diff against the previous list so relationship changes raise toasts
        // with the requester's name resolved (gateway events carry only an id).
        struct Pending { NotifKind kind; uint64_t id; std::wstring name; };
        std::vector<Pending> toEmit;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            std::map<uint64_t, FriendStatus> prev;
            for (const auto& f : m_friends) prev[f.accountId] = f.relationStatus;
            bool firstLoad = !m_friendsLoaded;
            for (auto& f : next) {
                auto display = [&]() {
                    auto it = m_prefs.find(f.accountId);
                    if (it != m_prefs.end() && !it->second.nickname.empty()) return it->second.nickname;
                    return f.username;
                };
                auto pit = prev.find(f.accountId);
                FriendStatus before = pit == prev.end() ? FriendStatus::None : pit->second;
                if (!firstLoad) {
                    if (f.relationStatus == FriendStatus::RequestReceived &&
                        before != FriendStatus::RequestReceived)
                        toEmit.push_back({ NotifKind::FriendRequest, f.accountId, display() });
                    else if (f.relationStatus == FriendStatus::Accepted &&
                             before != FriendStatus::Accepted)
                        toEmit.push_back({ NotifKind::FriendAccepted, f.accountId, display() });
                }
            }
            m_friends = std::move(next);
            ApplyPrefsLocked();
            m_friendsLoaded = true;
        }
        for (const auto& e : toEmit) {
            if (e.kind == NotifKind::FriendRequest)
                Notify(e.kind, e.id, e.name, L"sent you a friend request");
            else
                Notify(e.kind, e.id, e.name, L"is now your friend");
        }
        FireChanged();
    }).detach();
}

void SocialManager::SendFriendRequest(const std::wstring& username) {
    std::string u = WideToUtf8(username);
    std::wstring uname = username;
    std::thread([this, u, uname]() {
        std::string body;
        std::string json = std::string("{\"username\":\"") + JsonEscape(u) + "\"}";
        HttpPostJson(L"/api/social/friends/request", json, body);
        // Body is JSON {"status":...} on success, or a plain-text error message.
        JsonValue v = JsonValue::Parse(body);
        std::string status = v.isObject() ? v["status"].asString() : "";
        if (status == "request_sent")
            Notify(NotifKind::FriendRequest, 0, uname, L"Friend request sent");
        else if (status == "accepted")
            Notify(NotifKind::FriendAccepted, 0, uname, L"is now your friend");
        else {
            // Surface the server's reason (e.g. "User not found", "Already friends").
            std::wstring reason = body.empty() ? L"Could not send request" : Utf8ToWide(body);
            Notify(NotifKind::System, 0, uname.empty() ? L"Add friend" : uname, reason);
        }
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

void SocialManager::IgnoreRequest(uint64_t userId) {
    RespondRequest(userId, "ignore");
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

// ── Personalization (favorites / nicknames / recency) ─────────────────────────

static std::wstring PrefsPath() {
    wchar_t path[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path);
    return std::wstring(path) + L"\\ArcadeLauncher\\social_prefs.json";
}

void SocialManager::ApplyPrefsLocked() {
    for (auto& f : m_friends) {
        auto it = m_prefs.find(f.accountId);
        if (it != m_prefs.end()) {
            f.favorite = it->second.favorite;
            f.nickname = it->second.nickname;
            f.lastInteract = it->second.lastInteract;
        }
    }
}

void SocialManager::LoadPrefs() {
    std::string body;
    HANDLE h = CreateFileW(PrefsPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(h, nullptr), got = 0;
    if (sz && sz != INVALID_FILE_SIZE) {
        body.resize(sz);
        ReadFile(h, &body[0], sz, &got, nullptr);
        body.resize(got);
    }
    CloseHandle(h);
    JsonValue v = JsonValue::Parse(body);
    const JsonValue& settings = v["settings"];
    const JsonValue& arr = v["prefs"];
    std::lock_guard<std::mutex> lk(m_mtx);
    if (settings.isObject()) {
        m_prefSound = settings["sound"].asBool(true);
        m_prefOnlineAlerts = settings["online"].asBool(true);
    }
    m_prefs.clear();
    if (arr.isArray())
        for (const auto& p : arr.arr) {
            LocalPref lp;
            lp.favorite = p["favorite"].asBool();
            lp.nickname = Utf8ToWide(p["nickname"].asString());
            lp.lastInteract = p["lastInteract"].asInt();
            m_prefs[p["accountId"].asUint()] = std::move(lp);
        }
    ApplyPrefsLocked();
}

// Serialize current prefs to the JSON shape shared by the local file and the
// server blob: {"settings":{sound,online},"prefs":[{accountId,favorite,...}]}.
std::string SocialManager::BuildPrefsJson() {
    std::ostringstream os;
    std::lock_guard<std::mutex> lk(m_mtx);
    os << "{\"settings\":{\"sound\":" << (m_prefSound ? "true" : "false")
       << ",\"online\":" << (m_prefOnlineAlerts ? "true" : "false") << "},";
    os << "\"prefs\":[";
    bool first = true;
    for (const auto& kv : m_prefs) {
        const LocalPref& lp = kv.second;
        if (!lp.favorite && lp.nickname.empty() && lp.lastInteract == 0) continue;
        if (!first) os << ",";
        first = false;
        os << "{\"accountId\":" << kv.first
           << ",\"favorite\":" << (lp.favorite ? "true" : "false")
           << ",\"nickname\":\"" << JsonEscape(WideToUtf8(lp.nickname)) << "\""
           << ",\"lastInteract\":" << lp.lastInteract << "}";
    }
    os << "]}";
    return os.str();
}

void SocialManager::SavePrefs() {
    std::string body = BuildPrefsJson();
    HANDLE h = CreateFileW(PrefsPath().c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(h, body.data(), (DWORD)body.size(), &wrote, nullptr);
        CloseHandle(h);
    }
    // Mirror to the server so prefs follow the user to other devices.
    PushPrefsToServer();
}

// POST the prefs blob to the server (best-effort, async). The server stores it
// opaquely; last write wins.
void SocialManager::PushPrefsToServer() {
    if (!m_running.load()) return;
    std::string body = BuildPrefsJson();
    std::thread([this, body]() {
        std::string resp;
        HttpPostJson(L"/api/social/prefs", body, resp);
    }).detach();
}

// GET the server's prefs blob and adopt it (server is authoritative across
// devices). Applies onto the friend list and refreshes the local cache file.
void SocialManager::PullPrefsFromServer() {
    if (!m_running.load()) return;
    std::thread([this]() {
        std::string resp;
        if (!HttpGet(L"/api/social/prefs", resp)) return;
        JsonValue v = JsonValue::Parse(resp);
        const JsonValue& prefs = v["prefs"];
        if (!prefs.isObject()) return; // nothing stored yet — keep local
        const JsonValue& settings = prefs["settings"];
        const JsonValue& arr = prefs["prefs"];
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if (settings.isObject()) {
                m_prefSound = settings["sound"].asBool(true);
                m_prefOnlineAlerts = settings["online"].asBool(true);
            }
            m_prefs.clear();
            if (arr.isArray())
                for (const auto& p : arr.arr) {
                    LocalPref lp;
                    lp.favorite = p["favorite"].asBool();
                    lp.nickname = Utf8ToWide(p["nickname"].asString());
                    lp.lastInteract = p["lastInteract"].asInt();
                    m_prefs[p["accountId"].asUint()] = std::move(lp);
                }
            ApplyPrefsLocked();
        }
        // Refresh the on-disk cache (write file only, no re-POST loop).
        std::string body = BuildPrefsJson();
        HANDLE h = CreateFileW(PrefsPath().c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            WriteFile(h, body.data(), (DWORD)body.size(), &wrote, nullptr);
            CloseHandle(h);
        }
        FireChanged();
    }).detach();
}

// ── Friend-request privacy (1.1) ──────────────────────────────────────────────

std::string SocialManager::FriendPolicy() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_friendPolicy;
}

void SocialManager::SetFriendPolicy(const std::string& policy) {
    if (policy != "everyone" && policy != "mutual" && policy != "nobody") return;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_friendPolicy = policy;
    }
    FireChanged();
    if (!m_running.load()) return;
    std::thread([this, policy]() {
        std::ostringstream os;
        os << "{\"friendPolicy\":\"" << policy << "\"}";
        std::string resp;
        HttpPut(L"/api/social/privacy", os.str(), resp);
    }).detach();
}

std::string SocialManager::DmPolicy() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_dmPolicy;
}

void SocialManager::SetDmPolicy(const std::string& policy) {
    if (policy != "everyone" && policy != "friends" && policy != "nobody") return;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_dmPolicy = policy;
    }
    FireChanged();
    if (!m_running.load()) return;
    std::thread([this, policy]() {
        std::ostringstream os;
        os << "{\"dmPolicy\":\"" << policy << "\"}";
        std::string resp;
        HttpPut(L"/api/social/privacy", os.str(), resp);
    }).detach();
}

void SocialManager::PullFriendPolicy() {
    if (!m_running.load()) return;
    std::thread([this]() {
        std::string resp;
        if (!HttpGet(L"/api/social/privacy", resp)) return;
        JsonValue v = JsonValue::Parse(resp);
        std::string fp = v["friendPolicy"].asString();
        // dmPolicy may be absent on an older server — leave the default in place.
        std::string dp = v["dmPolicy"].asString();
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if (!fp.empty()) m_friendPolicy = fp;
            if (!dp.empty()) m_dmPolicy = dp;
        }
        FireChanged();
    }).detach();
}

void SocialManager::SetFavorite(uint64_t userId, bool fav) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_prefs[userId].favorite = fav;
        if (FriendInfo* f = FindFriendLocked(userId)) f->favorite = fav;
    }
    SavePrefs();
    FireChanged();
}

bool SocialManager::IsFavorite(uint64_t userId) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_prefs.find(userId);
    return it != m_prefs.end() && it->second.favorite;
}

void SocialManager::SetNickname(uint64_t userId, const std::wstring& nick) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_prefs[userId].nickname = nick;
        if (FriendInfo* f = FindFriendLocked(userId)) f->nickname = nick;
    }
    SavePrefs();
    FireChanged();
}

std::wstring SocialManager::NicknameOf(uint64_t userId) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_prefs.find(userId);
    return it != m_prefs.end() ? it->second.nickname : L"";
}

void SocialManager::MarkInteracted(uint64_t userId) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_prefs[userId].lastInteract = NowSecs();
        if (FriendInfo* f = FindFriendLocked(userId)) f->lastInteract = m_prefs[userId].lastInteract;
    }
    SavePrefs();
}

// ── Notifications ─────────────────────────────────────────────────────────────

void SocialManager::Notify(NotifKind kind, uint64_t accountId,
                           std::wstring title, std::wstring body) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        Notification n;
        n.id        = m_notifSeq++;
        n.kind      = kind;
        n.accountId = accountId;
        n.title     = std::move(title);
        n.body      = std::move(body);
        n.timestamp = NowMs();
        m_history.push_back(n);
        if (m_history.size() > 200)
            m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 200));
        m_toastQueue.push_back(std::move(n));
    }
    FireChanged();
}

// Merge one server notification row into m_history (call under m_mtx). Dedup is
// by serverId: an existing row's read state is refreshed; a new row is inserted
// keeping m_history ordered by timestamp ascending (newest last, matching Notify).
// A genuinely new + unread row is appended to `toasts` when emitToast is set.
void SocialManager::IngestServerNotificationLocked(const JsonValue& n, bool emitToast,
                                                   std::vector<Notification>& toasts) {
    uint64_t sid = n["id"].asUint();
    if (sid == 0) return;
    bool read = n["read"].asBool();

    // Already known? Just sync read state (server is authoritative).
    for (auto& ex : m_history) {
        if (ex.serverId == sid) {
            ex.read = read;
            return;
        }
    }

    Notification nn;
    nn.id        = m_notifSeq++;
    nn.serverId  = sid;
    nn.kind      = NotifKindFromString(n["kind"].asString());
    nn.accountId = n["actorId"].asUint();
    // Prefer the actor's name as the title; fall back to a kind-appropriate label.
    std::wstring actor = Utf8ToWide(n["actorName"].asString());
    nn.title     = !actor.empty() ? actor : L"Notification";
    nn.body      = Utf8ToWide(n["body"].asString());
    nn.timestamp = n["timestamp"].asInt() * 1000; // server stores epoch seconds
    nn.read      = read;

    // Insert keeping ascending timestamp order (history is newest-last).
    auto pos = m_history.end();
    while (pos != m_history.begin() && (pos - 1)->timestamp > nn.timestamp) --pos;
    m_history.insert(pos, nn);
    if (m_history.size() > 200)
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 200));

    if (emitToast && !read) toasts.push_back(nn);
}

void SocialManager::RefreshNotifications() {
    std::thread([this]() {
        std::string body;
        if (!HttpGet(L"/api/social/notifications", body)) return;
        JsonValue v = JsonValue::Parse(body);
        const JsonValue& arr = v["notifications"];
        if (!arr.isArray()) return;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            std::vector<Notification> ignore;
            // Server returns newest-first; ingest in reverse for ascending order.
            for (auto it = arr.arr.rbegin(); it != arr.arr.rend(); ++it)
                IngestServerNotificationLocked(*it, /*emitToast=*/false, ignore);
        }
        FireChanged();
    }).detach();
}

std::vector<Notification> SocialManager::DrainToasts() {
    std::lock_guard<std::mutex> lk(m_mtx);
    std::vector<Notification> out;
    out.swap(m_toastQueue);
    return out;
}

std::vector<Notification> SocialManager::GetNotifications() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_history;
}

int SocialManager::UnreadNotifications() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    int n = 0;
    for (const auto& x : m_history) if (!x.read) ++n;
    return n;
}

void SocialManager::MarkNotificationsRead() {
    uint64_t maxServerId = 0;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (auto& x : m_history) {
            x.read = true;
            if (x.serverId > maxServerId) maxServerId = x.serverId;
        }
    }
    FireChanged();
    // Persist read state server-side (upToId=0 marks all unread read). Fire and
    // forget on a worker thread; local state already reflects the change.
    std::thread([this, maxServerId]() {
        std::string body;
        std::string json = "{\"upToId\":" + std::to_string(maxServerId) + "}";
        HttpPostJson(L"/api/social/notifications/read", json, body);
    }).detach();
}

void SocialManager::ClearNotifications() {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_history.clear();
    }
    FireChanged();
}

bool SocialManager::NotifSoundEnabled() const {
    std::lock_guard<std::mutex> lk(m_mtx); return m_prefSound;
}
void SocialManager::SetNotifSound(bool on) {
    { std::lock_guard<std::mutex> lk(m_mtx); m_prefSound = on; }
    SavePrefs(); FireChanged();
}
bool SocialManager::PresenceAlertsEnabled() const {
    std::lock_guard<std::mutex> lk(m_mtx); return m_prefOnlineAlerts;
}
void SocialManager::SetPresenceAlerts(bool on) {
    { std::lock_guard<std::mutex> lk(m_mtx); m_prefOnlineAlerts = on; }
    SavePrefs(); FireChanged();
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
                cm.editedAt   = m["editedAt"].asInt();   // 0/null when never edited
                cm.deleted    = m["deleted"].asBool();
                AtomicMax(m_lastMsgId, cm.messageId);
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
    // Tell the peer their messages are read now that the conversation is open.
    MarkConversationRead(peerId);
}

void SocialManager::MarkConversationRead(uint64_t peerId) {
    if (!m_running.load()) return;
    std::ostringstream os;
    os << "{\"type\":\"read\",\"to\":" << peerId << "}";
    SendGatewayJson(os.str());
}

void SocialManager::EditMessage(uint64_t peerId, uint64_t msgId,
                                const std::wstring& text) {
    if (text.empty()) return;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        Conversation& c = ConvLocked(peerId);
        for (auto& m : c.messages) {
            if (m.messageId == msgId) {
                m.messageText = text;
                m.editedAt = NowSecs();
                break;
            }
        }
    }
    FireChanged();
    std::ostringstream os;
    os << "{\"type\":\"edit\",\"msgId\":" << msgId << ",\"text\":\""
       << JsonEscape(WideToUtf8(text)) << "\"}";
    SendGatewayJson(os.str());
}

void SocialManager::DeleteMessage(uint64_t peerId, uint64_t msgId) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        Conversation& c = ConvLocked(peerId);
        for (auto& m : c.messages) {
            if (m.messageId == msgId) { m.deleted = true; break; }
        }
    }
    FireChanged();
    std::ostringstream os;
    os << "{\"type\":\"delete\",\"msgId\":" << msgId << "}";
    SendGatewayJson(os.str());
}

// Apply a reaction toggle to a message in any conversation (call under m_mtx).
static void ApplyReactionLocked(ChatMessage& m, uint64_t userId,
                                const std::wstring& emoji, bool on) {
    auto& who = m.reactions[emoji];
    auto it = std::find(who.begin(), who.end(), userId);
    if (on) {
        if (it == who.end()) who.push_back(userId);
    } else {
        if (it != who.end()) who.erase(it);
        if (who.empty()) m.reactions.erase(emoji);
    }
}

void SocialManager::ToggleReaction(uint64_t peerId, uint64_t msgId,
                                   const std::wstring& emoji) {
    if (emoji.empty()) return;
    uint64_t self = m_selfId.load();
    bool on = true;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        Conversation& c = ConvLocked(peerId);
        for (auto& m : c.messages) {
            if (m.messageId == msgId) {
                auto rit = m.reactions.find(emoji);
                bool has = rit != m.reactions.end() &&
                           std::find(rit->second.begin(), rit->second.end(), self) != rit->second.end();
                on = !has;
                ApplyReactionLocked(m, self, emoji, on);
                break;
            }
        }
    }
    FireChanged();
    std::ostringstream os;
    os << "{\"type\":\"react\",\"msgId\":" << msgId << ",\"emoji\":\""
       << JsonEscape(WideToUtf8(emoji)) << "\",\"on\":" << (on ? "true" : "false") << "}";
    SendGatewayJson(os.str());
}

void SocialManager::LoadOlderHistory(uint64_t peerId) {
    if (!m_running.load()) return;
    // Find the oldest known message id for this peer to page before it.
    uint64_t before = 0;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        auto it = m_convos.find(peerId);
        if (it != m_convos.end()) {
            for (const auto& m : it->second.messages) {
                if (m.messageId != 0 && (before == 0 || m.messageId < before))
                    before = m.messageId;
            }
        }
    }
    if (before == 0) return;   // nothing loaded yet — OpenConversation handles first page
    std::thread([this, peerId, before]() {
        std::wstring path = L"/api/social/messages/" + std::to_wstring(peerId) +
                            L"?before=" + std::to_wstring(before);
        std::string body;
        if (!HttpGet(path, body)) return;
        JsonValue v = JsonValue::Parse(body);
        const JsonValue& arr = v["messages"];
        uint64_t self = m_selfId.load();
        std::vector<ChatMessage> older;
        if (arr.isArray()) {
            for (const auto& m : arr.arr) {
                ChatMessage cm;
                cm.messageId  = m["messageId"].asUint();
                cm.senderId   = m["senderId"].asUint();
                cm.receiverId = m["receiverId"].asUint();
                cm.messageText = Utf8ToWide(m["text"].asString());
                cm.timestamp  = m["timestamp"].asInt();
                cm.isRead     = m["isRead"].asBool() || cm.senderId == self;
                cm.editedAt   = m["editedAt"].asInt();
                cm.deleted    = m["deleted"].asBool();
                older.push_back(std::move(cm));
            }
        }
        if (older.empty()) return;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            Conversation& c = ConvLocked(peerId);
            // Prepend, skipping any ids we already have (dedup).
            std::vector<ChatMessage> merged;
            merged.reserve(older.size() + c.messages.size());
            for (auto& m : older) {
                bool dup = false;
                for (const auto& e : c.messages)
                    if (e.messageId != 0 && e.messageId == m.messageId) { dup = true; break; }
                if (!dup) merged.push_back(std::move(m));
            }
            for (auto& e : c.messages) merged.push_back(std::move(e));
            c.messages = std::move(merged);
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
