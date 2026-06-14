#include "pch.h"
#include "SocialManager.h"
#include "SocialJson.h"
#include <winhttp.h>
#include <shlobj.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <cstring>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

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
        m_friendsLoaded = false;
    }
    if (baseUrl.empty() || token.empty()) return;
    m_running.store(true);
    m_backoffMs.store(1000);
    m_everConnected.store(false);
    LoadPrefs();
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
        // On a *resume* (not the first connect) reconcile state that may have
        // changed while we were offline: friend list/presence + missed messages.
        if (m_everConnected.exchange(true))
            ReconcileAfterReconnect();
        FireChanged();
    } else {
        m_gateway.store(GatewayState::Disconnected);
        FireChanged();
        if (m_running.load()) ScheduleReconnect();
    }
}

void SocialManager::ReconcileAfterReconnect() {
    // 1) Authoritative friend list + presence (also raises toasts for requests
    //    that arrived while we were disconnected, via RefreshFriends' diff).
    RefreshFriends();

    // 2) Re-pull history for conversations opened this session so messages sent
    //    while we were offline appear and unread counts are corrected. Server
    //    isRead flags drive the unread recompute (we don't clobber it to 0).
    std::vector<uint64_t> peers;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (const auto& kv : m_convos)
            if (!kv.second.messages.empty()) peers.push_back(kv.first);
    }
    for (uint64_t peerId : peers) {
        std::thread([this, peerId]() {
            std::wstring path = L"/api/social/messages/" + std::to_wstring(peerId);
            std::string body;
            if (!HttpGet(path, body)) return;
            JsonValue v = JsonValue::Parse(body);
            const JsonValue& arr = v["messages"];
            uint64_t self = m_selfId.load();
            std::vector<ChatMessage> hist;
            int unread = 0;
            if (arr.isArray()) {
                for (const auto& m : arr.arr) {
                    ChatMessage cm;
                    cm.messageId  = m["messageId"].asUint();
                    cm.senderId   = m["senderId"].asUint();
                    cm.receiverId = m["receiverId"].asUint();
                    cm.messageText = Utf8ToWide(m["text"].asString());
                    cm.timestamp  = m["timestamp"].asInt();
                    cm.isRead     = m["isRead"].asBool() || cm.senderId == self;
                    if (!cm.isRead && cm.senderId != self) ++unread;
                    hist.push_back(std::move(cm));
                }
            }
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                Conversation& c = ConvLocked(peerId);
                c.messages = std::move(hist);
                c.unread = unread;
            }
            FireChanged();
        }).detach();
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

void SocialManager::SavePrefs() {
    std::ostringstream os;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        os << "{\"settings\":{\"sound\":" << (m_prefSound ? "true" : "false")
           << ",\"online\":" << (m_prefOnlineAlerts ? "true" : "false") << "},";
    }
    os << "\"prefs\":[";
    {
        std::lock_guard<std::mutex> lk(m_mtx);
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
    }
    os << "]}";
    std::string body = os.str();
    HANDLE h = CreateFileW(PrefsPath().c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(h, body.data(), (DWORD)body.size(), &wrote, nullptr);
    CloseHandle(h);
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
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (auto& x : m_history) x.read = true;
    }
    FireChanged();
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
