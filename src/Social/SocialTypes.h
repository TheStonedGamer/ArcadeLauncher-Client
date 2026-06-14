#pragma once
// SocialTypes.h - shared data model for the social subsystem (per social.md §1).
// Cache-friendly value types; relationships and conversations are held in
// std::vector / std::map inside SocialManager under a single mutex. Identifiers
// are server account ids (uint64). All user-facing text is UTF-16.

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>

namespace social {

enum class FriendStatus {
    None,
    RequestSent,
    RequestReceived,
    Accepted,
    Blocked
};

enum class PresenceState {
    Offline,
    Online,
    Away,
    Busy,
    Invisible,
    InGame
};

struct FriendInfo {
    uint64_t      accountId = 0;
    std::wstring  username;
    PresenceState presence = PresenceState::Offline;
    FriendStatus  relationStatus = FriendStatus::None;
    std::wstring  currentGameTitle;
    std::wstring  currentGameId;
    int64_t       lastOnline = 0;   // epoch seconds
    // Client-local personalization (persisted in social_prefs.json, never sent
    // to the server). Favorites pin to the top; nickname overrides display name.
    bool          favorite = false;
    std::wstring  nickname;
    int64_t       lastInteract = 0; // epoch secs — for "recently interacted" sort
};

struct ChatMessage {
    uint64_t     messageId = 0;
    uint64_t     senderId = 0;
    uint64_t     receiverId = 0;
    std::wstring messageText;
    int64_t      timestamp = 0;     // epoch seconds
    bool         isRead = false;
    bool         pending = false;   // sent locally, not yet acked by gateway
    int64_t      editedAt = 0;      // epoch seconds of last edit, 0 = never (1.2a)
    bool         deleted = false;   // tombstoned — render as "message deleted" (1.2a)
    // Reactions (1.2b): emoji -> set of account ids that reacted with it.
    std::map<std::wstring, std::vector<uint64_t>> reactions;
    // Attachment (1.3): id of an uploaded object linked to this message, 0 = none.
    uint64_t     attachmentId = 0;
    std::wstring attachmentName;    // original filename, for display (may be empty)
};

// Per-peer conversation: ordered message history + unread/typing state.
struct Conversation {
    uint64_t                 peerId = 0;
    std::vector<ChatMessage> messages;
    int                      unread = 0;
    bool                     peerTyping = false;
    int64_t                  peerTypingUntil = 0; // epoch ms; clears the indicator
    uint64_t                 readUpTo = 0;        // highest of MY msg ids the peer has read (1.2a)
};

// Desktop notification categories. The integer values are mirrored into the
// dependency-free Renderer (toast accent colors + glyphs), so keep them stable.
enum class NotifKind {
    FriendRequest = 0,   // someone sent you a request
    FriendAccepted = 1,  // your request was accepted
    FriendOnline = 2,    // a friend came online
    FriendInGame = 3,    // a friend started a game
    Message = 4,         // new direct message
    VoiceInvite = 5,     // incoming voice call
    System = 6           // generic/system announcement
};

struct Notification {
    uint64_t     id = 0;          // local render key (monotonic, per-session)
    uint64_t     serverId = 0;    // persisted row id (0 = client-only, e.g. presence)
    NotifKind    kind = NotifKind::System;
    uint64_t     accountId = 0;   // related user (0 if none) — click target
    std::wstring title;
    std::wstring body;
    int64_t      timestamp = 0;   // epoch ms
    bool         read = false;
};

// Map the server's notification `kind` string to a NotifKind. Unknown kinds fall
// back to System so future server-side kinds still surface (just without a glyph).
inline NotifKind NotifKindFromString(const std::string& s) {
    if (s == "friend_request")  return NotifKind::FriendRequest;
    if (s == "friend_accepted") return NotifKind::FriendAccepted;
    if (s == "friend_online")   return NotifKind::FriendOnline;
    if (s == "friend_ingame")   return NotifKind::FriendInGame;
    if (s == "message")         return NotifKind::Message;
    if (s == "voice_invite")    return NotifKind::VoiceInvite;
    return NotifKind::System;
}

// Voice signaling state machine states (per social.md §6).
enum class VoiceState {
    Idle,
    Connecting,
    Negotiating,
    Connected,
    Reconnecting,
    Disconnected,
    Failed
};

// Gateway connection lifecycle, surfaced to the UI for a status dot.
enum class GatewayState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting
};

inline const wchar_t* PresenceLabel(PresenceState s) {
    switch (s) {
        case PresenceState::Online:    return L"Online";
        case PresenceState::Away:      return L"Away";
        case PresenceState::Busy:      return L"Busy";
        case PresenceState::Invisible: return L"Invisible";
        case PresenceState::InGame:    return L"In-Game";
        default:                        return L"Offline";
    }
}

inline PresenceState PresenceFromString(const std::string& s) {
    if (s == "online")    return PresenceState::Online;
    if (s == "away")      return PresenceState::Away;
    if (s == "busy")      return PresenceState::Busy;
    if (s == "invisible") return PresenceState::Invisible;
    if (s == "ingame")    return PresenceState::InGame;
    return PresenceState::Offline;
}

inline const char* PresenceWire(PresenceState s) {
    switch (s) {
        case PresenceState::Online:    return "online";
        case PresenceState::Away:      return "away";
        case PresenceState::Busy:      return "busy";
        case PresenceState::Invisible: return "invisible";
        case PresenceState::InGame:    return "ingame";
        default:                        return "online";
    }
}

} // namespace social
