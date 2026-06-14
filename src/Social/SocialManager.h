#pragma once
// SocialManager.h - orchestrates the social subsystem (per social.md §Required
// Architecture). Subsumes the named roles from the spec into one cohesive,
// thread-safe owner to avoid a web of cross-referencing singletons:
//   * GatewayConnectionManager / WebSocketClient -> persistent WSS + reconnect
//   * PresenceManager  -> presence state + rich (InGame) presence push
//   * FriendManager    -> friend list / requests / block via REST + gateway diffs
//   * ChatManager / ConversationManager / MessageStore -> DMs, history, unread
//   * TypingStateManager -> typing indicators
//   * VoiceSignalingManager -> voice signaling state machine + frame relay
//
// Threading model: the WebSocket worker thread delivers gateway frames into
// HandleGatewayFrame under m_mtx; REST calls run on detached worker threads so
// the UI thread never blocks. After any state mutation we fire m_onChanged so
// the host can PostMessage a repaint. All public accessors copy under the lock.

#include "SocialTypes.h"
#include "WebSocketClient.h"
#include "VoiceEngine.h"
#include <functional>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <map>

// Posted to the main window whenever social state changes so it can repaint.
static constexpr UINT WM_APP_SOCIAL_CHANGED = WM_USER + 114;

namespace social {

class SocialManager {
public:
    SocialManager() = default;
    ~SocialManager();

    // baseUrl is the http(s):// origin (same as ServerClient's). token is the
    // launcher bearer token. Kicks an initial friends fetch and opens the
    // gateway; safe to call again to restart with new credentials.
    void Start(const std::wstring& baseUrl, const std::wstring& token);
    void Stop();

    bool IsRunning() const { return m_running.load(); }
    GatewayState Gateway() const { return m_gateway.load(); }
    uint64_t SelfId() const { return m_selfId.load(); }

    // Host registers a handler invoked (possibly off the UI thread) whenever
    // social state changes; it should marshal a repaint to the UI thread.
    void SetChangeHandler(std::function<void()> cb) { m_onChanged = std::move(cb); }

    // ── Friends ──────────────────────────────────────────────────────────────
    void RefreshFriends();                              // REST GET, async
    void SendFriendRequest(const std::wstring& username);
    void RespondRequest(uint64_t userId, const std::string& action); // accept|decline|cancel|remove
    void BlockUser(uint64_t userId, bool block);
    std::vector<FriendInfo> GetFriends() const;

    // ── Personalization (client-local; persisted to social_prefs.json) ─────────
    void SetFavorite(uint64_t userId, bool fav);
    bool IsFavorite(uint64_t userId) const;
    void SetNickname(uint64_t userId, const std::wstring& nick); // empty = reset
    std::wstring NicknameOf(uint64_t userId) const;
    void MarkInteracted(uint64_t userId);                        // for recent sort

    // ── Notifications (toast queue + session history) ──────────────────────────
    std::vector<Notification> DrainToasts();          // new toasts since last call
    std::vector<Notification> GetNotifications() const;
    int  UnreadNotifications() const;
    void MarkNotificationsRead();
    void ClearNotifications();

    // Notification preferences (persisted in social_prefs.json).
    bool NotifSoundEnabled() const;
    void SetNotifSound(bool on);
    bool PresenceAlertsEnabled() const;   // toast when (favorite) friends come online/launch
    void SetPresenceAlerts(bool on);

    // ── Presence ─────────────────────────────────────────────────────────────
    void SetPresence(PresenceState state);
    void SetInGame(const std::wstring& gameId, const std::wstring& gameTitle);
    void ClearInGame();                                 // back to Online

    // ── Chat ─────────────────────────────────────────────────────────────────
    void OpenConversation(uint64_t peerId);             // loads history, clears unread
    void SendChat(uint64_t peerId, const std::wstring& text);
    void NotifyTyping(uint64_t peerId);                 // throttled by caller
    Conversation GetConversation(uint64_t peerId) const;
    int  TotalUnread() const;

    // ── Voice signaling (state machine + frame relay; media is scaffolded) ────
    VoiceState Voice() const { return m_voiceState.load(); }
    uint64_t   VoicePeer() const { return m_voicePeer.load(); }
    void StartVoiceCall(uint64_t peerId);
    void AcceptVoiceCall(uint64_t peerId);
    void EndVoiceCall();
    void SetVoiceMuted(bool muted) { m_voice.SetMuted(muted); }
    bool IsVoiceMuted() const { return m_voice.IsMuted(); }

private:
    // Gateway lifecycle
    void OpenGateway();
    void HandleGatewayFrame(const std::string& utf8);
    void OnGatewaySocketState(bool connected);
    void ScheduleReconnect();
    void ReconcileAfterReconnect();   // re-pull friends + missed messages on resume
    bool SendGatewayJson(const std::string& json);

    // REST helpers (own WinHTTP, simple JSON in/out). Run on worker threads.
    bool HttpGet(const std::wstring& path, std::string& body);
    bool HttpPostJson(const std::wstring& path, const std::string& json, std::string& body);
    std::wstring WsUrl() const;   // derives ws(s)://.../ws/social?token=...

    void FireChanged();
    FriendInfo* FindFriendLocked(uint64_t id);          // call under m_mtx

    // Notifications: emit one (history + toast queue), fire change. Thread-safe.
    void Notify(NotifKind kind, uint64_t accountId,
                std::wstring title, std::wstring body);

    // Personalization persistence (social_prefs.json next to other app data).
    void LoadPrefs();
    void SavePrefs();
    void ApplyPrefsLocked();                            // overlay favorite/nick onto m_friends
    Conversation& ConvLocked(uint64_t peerId);          // call under m_mtx
    void SendVoiceSignal(uint64_t peerId, const std::string& kind);

    // Voice media: start/stop the WASAPI engine and relay PCM frames over the
    // gateway's binary path, prefixed with the destination account id.
    void StartVoiceMedia();
    void StopVoiceMedia();
    void HandleAudioFrame(const void* data, size_t len);  // inbound binary frame

    std::wstring m_baseUrl;
    std::wstring m_token;
    std::atomic<uint64_t> m_selfId{ 0 };

    WebSocketClient m_ws;
    std::atomic<bool>          m_running{ false };
    std::atomic<GatewayState>  m_gateway{ GatewayState::Disconnected };
    std::atomic<int>           m_backoffMs{ 1000 };
    std::atomic<int>           m_reconnectGen{ 0 };     // invalidates stale timers
    std::atomic<bool>          m_everConnected{ false }; // first connect vs a resume

    mutable std::mutex m_mtx;
    std::vector<FriendInfo>                m_friends;     // guarded
    std::map<uint64_t, Conversation>       m_convos;      // guarded
    bool                                   m_friendsLoaded = false; // suppress toasts on first load

    // Notifications (guarded by m_mtx).
    std::vector<Notification>              m_history;     // capped, newest last
    std::vector<Notification>              m_toastQueue;  // undrained toasts
    uint64_t                               m_notifSeq = 1;
    bool                                   m_prefSound = true;        // play a sound on toasts
    bool                                   m_prefOnlineAlerts = true; // presence toasts on/off

    // Client-local personalization, keyed by account id (guarded by m_mtx).
    struct LocalPref { bool favorite = false; std::wstring nickname; int64_t lastInteract = 0; };
    std::map<uint64_t, LocalPref>          m_prefs;
    PresenceState                          m_presence = PresenceState::Online;
    std::wstring                           m_curGameId;
    std::wstring                           m_curGameTitle;

    std::atomic<VoiceState> m_voiceState{ VoiceState::Idle };
    std::atomic<uint64_t>   m_voicePeer{ 0 };
    VoiceEngine             m_voice;

    std::function<void()> m_onChanged;
};

} // namespace social
