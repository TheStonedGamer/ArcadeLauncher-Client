#include "pch.h"
#include "EmulatorScanner.h"

// Strip all trailing ROM tags: (USA), [!], (Rev 2), (Beta), etc.
static std::wstring StripRomTags(std::wstring title) {
    for (;;) {
        while (!title.empty() && iswspace(title.back())) title.pop_back();
        if (title.empty()) break;
        wchar_t close = title.back();
        if (close != L')' && close != L']') break;
        wchar_t open = (close == L')') ? L'(' : L'[';
        size_t p = title.rfind(open);
        if (p == std::wstring::npos) break;
        title = title.substr(0, p);
    }
    while (!title.empty() && iswspace(title.back())) title.pop_back();
    return title;
}

// Score a ROM filename using No-Intro / GoodNES naming conventions.
// Higher = better. We pick the highest-scoring ROM when titles collide.
static int RomScore(const std::wstring& fname) {
    std::wstring f = fname;
    for (auto& c : f) c = towlower(c);

    int score = 0;

    // Verified good dump — best possible
    if (f.find(L"[!]") != std::wstring::npos)          score += 20;

    // Alternates, bad dumps, over-dumps — avoid
    if (f.find(L"[a")  != std::wstring::npos)           score -= 10; // [a1],[a2]…
    if (f.find(L"[b")  != std::wstring::npos)           score -= 15; // bad dump
    if (f.find(L"[o")  != std::wstring::npos)           score -= 10; // over-dump

    // Prototypes and betas — usually interesting but not the release
    if (f.find(L"prototype") != std::wstring::npos)     score -=  8;
    if (f.find(L"beta")      != std::wstring::npos)     score -=  8;

    // Translation patches — fine but not the original
    if (f.find(L"trad-")     != std::wstring::npos)     score -=  5;

    // Prefer higher PRG / Rev revisions
    if (f.find(L"prg 0")     != std::wstring::npos)     score -=  2;
    if (f.find(L"prg 1")     != std::wstring::npos)     score +=  1;
    if (f.find(L"rev 0")     != std::wstring::npos)     score -=  2;
    if (f.find(L"rev a")     != std::wstring::npos)     score +=  1;
    if (f.find(L"rev b")     != std::wstring::npos)     score +=  2;

    // Prefer US/English releases over region-ambiguous or foreign
    if (f.find(L"(u)")       != std::wstring::npos)     score +=  3;
    if (f.find(L"(usa)")     != std::wstring::npos)     score +=  3;
    if (f.find(L"en,")       != std::wstring::npos)     score +=  1; // multi-lang with English

    // Hacks — deprioritise
    if (f.find(L"hack")      != std::wstring::npos)     score -= 12;

    return score;
}

static bool IsHexLike(const std::wstring& s) {
    if (s.empty()) return false;
    for (wchar_t c : s) {
        if (!iswxdigit(c)) return false;
    }
    return true;
}

static std::wstring FileNameOnly(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static std::wstring ParentPath(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

static std::wstring QuoteWindowsArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashCount = 0;

    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashCount;
            continue;
        }

        if (c == L'"') {
            out.append(slashCount * 2 + 1, L'\\');
            out.push_back(c);
            slashCount = 0;
            continue;
        }

        out.append(slashCount, L'\\');
        slashCount = 0;
        out.push_back(c);
    }

    out.append(slashCount * 2, L'\\');
    out.push_back(L'"');
    return out;
}

static std::wstring BuildRomArgs(const std::wstring& emulatorArgs, const std::wstring& romPath) {
    std::wstring args = emulatorArgs;
    std::wstring quotedRom = QuoteWindowsArg(romPath);
    size_t ph = args.find(L"{rom}");
    if (ph != std::wstring::npos)
        args.replace(ph, 5, quotedRom);
    else
        args += L" " + quotedRom;
    return args;
}

static bool IsGenericXbox360ContentFolder(std::wstring name) {
    for (auto& c : name) c = towlower(c);
    return name.empty()
        || name == L"content"
        || name == L"0000000000000000"
        || name == L"00007000"
        || name == L"0007000"
        || IsHexLike(name);
}

static std::wstring PickGodTitle(const std::wstring& godDir, const std::wstring& rootDir) {
    std::wstring cursor = ParentPath(godDir);
    std::wstring title;

    // GOD paths commonly look like <Game>\<TitleID>\00007000, and some
    // collections accidentally repeat the TitleID folder. Walk upward until
    // we find the user-facing folder name.
    while (!cursor.empty()) {
        std::wstring candidate = FileNameOnly(cursor);
        if (!IsGenericXbox360ContentFolder(candidate)) {
            title = candidate;
            break;
        }

        std::wstring parent = ParentPath(cursor);
        if (parent == cursor) break;
        cursor = parent;
    }

    if (title.empty())
        title = FileNameOnly(rootDir);
    return StripRomTags(title);
}

static std::wstring StablePathId(const std::wstring& path) {
    uint64_t hash = 1469598103934665603ull;
    for (wchar_t c : path) {
        wchar_t lc = towlower(c);
        hash ^= (uint64_t)lc;
        hash *= 1099511628211ull;
    }

    wchar_t buf[17]{};
    swprintf_s(buf, L"%016llx", (unsigned long long)hash);
    return buf;
}

static bool LooksLikeGodPackageFile(const WIN32_FIND_DATAW& fd) {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    if (fd.nFileSizeHigh == 0 && fd.nFileSizeLow == 0) return false;

    std::wstring name = fd.cFileName;
    if (name == L"." || name == L"..") return false;

    // GOD package payloads normally have no extension and live under 00007000
    // or 0007000, depending on how the rip/tool names the content folder.
    // Allow extensionless hex-like names, but skip obvious sidecar files.
    if (name.find(L'.') != std::wstring::npos) return false;
    return true;
}

static bool DirectoryExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool HasGodDataFolder(const std::wstring& godDir, const std::wstring& packageName) {
    return DirectoryExists(godDir + L"\\" + packageName + L".data");
}

static int GodPackageScore(const std::wstring& godDir, const WIN32_FIND_DATAW& fd) {
    int score = 0;
    if (HasGodDataFolder(godDir, fd.cFileName)) score += 100;
    if (IsHexLike(fd.cFileName)) score += 10;
    return score;
}

static void AddXbox360GodPackages(const EmulatorRomConfig& cfg,
                                  const std::wstring& rootDir,
                                  const std::wstring& scanDir,
                                  std::vector<Game>& games) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((scanDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring path = scanDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring dirName = fd.cFileName;
            for (auto& c : dirName) c = towlower(c);

            if (dirName == L"00007000" || dirName == L"0007000") {
                WIN32_FIND_DATAW pkgFd;
                HANDLE pkg = FindFirstFileW((path + L"\\*").c_str(), &pkgFd);
                if (pkg != INVALID_HANDLE_VALUE) {
                    bool foundPackage = false;
                    WIN32_FIND_DATAW bestPkgFd{};
                    int bestScore = -1;

                    do {
                        if (!LooksLikeGodPackageFile(pkgFd)) continue;
                        int score = GodPackageScore(path, pkgFd);
                        if (!foundPackage || score > bestScore) {
                            foundPackage = true;
                            bestPkgFd = pkgFd;
                            bestScore = score;
                        }
                    } while (FindNextFileW(pkg, &pkgFd));
                    FindClose(pkg);

                    if (foundPackage) {
                        std::wstring romPath = path + L"\\" + bestPkgFd.cFileName;
                        std::wstring title = PickGodTitle(path, rootDir);
                        std::wstring args = BuildRomArgs(cfg.emulatorArgs, romPath);

                        Game g;
                        g.id           = PlatformName(cfg.platform) + L"_god_" + StablePathId(romPath);
                        g.platform     = cfg.platform;
                        g.emulatorPath = cfg.emulatorPath;
                        g.romPath      = romPath;
                        g.arguments    = args;
                        g.title        = title.empty() ? FileNameOnly(ParentPath(path)) : title;

                        if (cfg.romDb) {
                            const auto* info = cfg.romDb->Lookup(cfg.platform, g.title);
                            if (info) {
                                g.title       = info->title;
                                g.igdbId      = info->igdbId;
                                g.igdbMatched = (info->igdbId > 0);
                            }
                        }

                        games.push_back(std::move(g));
                    }
                }
            } else {
                AddXbox360GodPackages(cfg, rootDir, path, games);
            }
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

EmulatorScanner::EmulatorScanner(EmulatorRomConfig cfg) : m_cfg(std::move(cfg)) {}

std::vector<Game> EmulatorScanner::Scan() {
    std::vector<Game> games;
    if (m_cfg.emulatorPath.empty()) return games;
    if (m_cfg.romDirs.empty())     return games;

    for (auto& dir : m_cfg.romDirs) {
        if (dir.empty()) continue;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring fname = fd.cFileName;
            // Extract extension
            size_t dot = fname.rfind(L'.');
            if (dot == std::wstring::npos) continue;
            std::wstring ext = fname.substr(dot + 1);
            for (auto& c : ext) c = towlower(c);

            bool match = false;
            for (auto& e : m_cfg.extensions) {
                std::wstring le = e;
                for (auto& c : le) c = towlower(c);
                if (ext == le) { match = true; break; }
            }
            if (!match) continue;

            // Title = filename without extension, ROM tags stripped
            std::wstring title = StripRomTags(fname.substr(0, dot));

            std::wstring romPath = dir + L"\\" + fname;
            std::wstring args = BuildRomArgs(m_cfg.emulatorArgs, romPath);

            Game g;
            g.id           = PlatformName(m_cfg.platform) + L"_" + fname;
            g.platform     = m_cfg.platform;
            g.emulatorPath = m_cfg.emulatorPath;
            g.romPath      = romPath;
            g.arguments    = args;

            // Enhance with ROM database: canonical title + pre-mapped IGDB ID
            if (m_cfg.romDb) {
                const auto* info = m_cfg.romDb->Lookup(m_cfg.platform, title);
                if (info) {
                    g.title      = info->title;
                    g.igdbId     = info->igdbId;
                    g.igdbMatched = (info->igdbId > 0);
                } else {
                    g.title = title;
                }
            } else {
                g.title = title;
            }

            games.push_back(std::move(g));

        } while (FindNextFileW(h, &fd));
        FindClose(h);

        if (m_cfg.platform == Platform::Xbox360)
            AddXbox360GodPackages(m_cfg, dir, dir, games);
    }

    // ── Deduplicate ROM variants ───────────────────────────────────────────────
    // Many ROM collections contain multiple revisions/alternates of the same game
    // (e.g. "Zelda (U) (PRG 0).nes" and "Zelda (U) (PRG 1).nes"). After tag
    // stripping they produce the same title. Keep the highest-scored ROM only.
    //
    // We use a map: title → index of current winner in `games`.
    // Losers get their id cleared; we erase them at the end.
    std::unordered_map<std::wstring, size_t> bestIdx; // title → winner index
    for (size_t i = 0; i < games.size(); ++i) {
        const std::wstring& t = games[i].title;
        auto it = bestIdx.find(t);
        if (it == bestIdx.end()) {
            bestIdx[t] = i;
        } else {
            size_t prev = it->second;
            int scoreNew  = RomScore(games[i].romPath);
            int scorePrev = RomScore(games[prev].romPath);
            if (scoreNew > scorePrev) {
                games[prev].id.clear(); // mark old winner as discarded
                bestIdx[t] = i;
            } else {
                games[i].id.clear();   // discard new challenger
            }
        }
    }
    games.erase(std::remove_if(games.begin(), games.end(),
        [](const Game& g) { return g.id.empty(); }), games.end());

    return games;
}
