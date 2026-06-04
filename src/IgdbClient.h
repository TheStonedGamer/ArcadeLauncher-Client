#pragma once
#include "pch.h"

struct IgdbGame {
    int64_t      id = 0;
    std::wstring name;
    std::wstring summary;
    double       rating = 0.0;      // 0-100, 0 = no rating
    int          ratingCount = 0;
    int64_t      firstReleaseDate = 0; // unix timestamp
    std::wstring coverImageId;
    std::vector<std::wstring> genres;
};

// Minimal JSON value for parsing IGDB responses
struct JsonVal {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    double                                          num = 0;
    std::string                                     str;
    std::vector<JsonVal>                            arr;
    std::unordered_map<std::string, JsonVal>        obj;

    bool        has(const std::string& k) const { return type == Object && obj.count(k); }
    const JsonVal& get(const std::string& k) const;
    std::wstring wstr() const { return ToWide(str); }
    int64_t      i64()  const { return (int64_t)num; }
};

namespace MiniJson {
    JsonVal Parse(const std::string& text);
}

class IgdbClient {
public:
    IgdbClient() = default;
    IgdbClient(const std::wstring& clientId, const std::wstring& clientSecret);

    // Returns false if auth fails.
    bool Authenticate();
    bool IsAuthenticated() const;

    // Search by title; returns up to `limit` candidates sorted by relevance.
    // Optionally restrict to specific IGDB platform IDs (e.g. {21,5} for GC/Wii).
    std::vector<IgdbGame> Search(const std::wstring& title, int limit = 5,
                                 const std::vector<int>& platformIds = {});

    // Fetch single game by IGDB id (with cover, genres, summary).
    IgdbGame FetchById(int64_t id);

    // Bulk-fetch games for a platform (for local DB sync).
    // Returns up to `limit` (max 500) games starting at `offset`.
    // Returns an empty vector if auth fails or no results.
    std::vector<IgdbGame> FetchGamesByPlatform(int platformId,
                                               int offset = 0,
                                               int limit  = 500);

    // Resolve a cover id to its image_id string, then build a URL.
    std::wstring CoverUrl(const std::wstring& imageId,
                          const std::string& size = "cover_big") const;

    // Download cover image to destPath. Returns true on success.
    bool DownloadCover(const std::wstring& imageId, const std::wstring& destPath);

    void SetCredentials(const std::wstring& clientId, const std::wstring& clientSecret);
    bool HasCredentials() const { return !m_clientId.empty() && !m_clientSecret.empty(); }

    // Persist/restore token to avoid re-authing every launch
    std::wstring ClientId()     const { return m_clientId; }
    std::wstring ClientSecret() const { return m_clientSecret; }
    std::wstring SavedToken()  const { return m_accessToken; }
    int64_t      TokenExpiry() const { return m_tokenExpiry; }
    void RestoreToken(const std::wstring& token, int64_t expiry);

private:
    bool Post(const std::wstring& host, const std::wstring& path,
              const std::string& body,
              const std::vector<std::wstring>& extraHeaders,
              std::string& response);

    bool Download(const std::wstring& url, const std::wstring& dest);

    IgdbGame ParseGameObject(const JsonVal& obj);

    std::wstring m_clientId;
    std::wstring m_clientSecret;
    std::wstring m_accessToken;
    int64_t      m_tokenExpiry = 0;
};
