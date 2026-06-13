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
};

struct ChatMessage {
    uint64_t     messageId = 0;
    uint64_t     senderId = 0;
    uint64_t     receiverId = 0;
    std::wstring messageText;
    int64_t      timestamp = 0;     // epoch seconds
    bool         isRead = false;
    bool         pending = false;   // sent locally, not yet acked by gateway
};

// Per-peer conversation: ordered message history + unread/typing state.
struct Conversation {
    uint64_t                 peerId = 0;
    std::vector<ChatMessage> messages;
    int                      unread = 0;
    bool                     peerTyping = false;
    int64_t                  peerTypingUntil = 0; // epoch ms; clears the indicator
};

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
