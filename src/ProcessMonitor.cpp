#include "pch.h"
#include "ProcessMonitor.h"
#include <set>

ProcessMonitor::~ProcessMonitor() {
    KillCurrent();
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

bool ProcessMonitor::Launch(const std::wstring& exe, const std::wstring& args,
                             const std::wstring& workDir, DoneCallback cb) {
    if (m_running.load()) return false;

    std::wstring cmdLine = QuoteWindowsArg(exe);
    if (!args.empty()) cmdLine += L" " + args;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring wd = workDir.empty() ?
        exe.substr(0, exe.rfind(L'\\')) : workDir;

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                        FALSE, 0, nullptr,
                        wd.empty() ? nullptr : wd.c_str(),
                        &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    m_hProcess = pi.hProcess;
    m_running.store(true);
    m_watchThread = std::thread(&ProcessMonitor::WatchThread, this, pi.hProcess, std::move(cb));
    return true;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::set<DWORD> SnapshotPids() {
    std::set<DWORD> pids;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(hSnap, &pe))
        do { pids.insert(pe.th32ProcessID); } while (Process32NextW(hSnap, &pe));
    CloseHandle(hSnap);
    return pids;
}

// Processes whose names indicate launcher/system infrastructure — never the game.
static bool IsInfraProcess(const std::wstring& lowerName) {
    static const wchar_t* kSkip[] = {
        L"steam", L"steamwebhelper", L"steamservice",
        L"epicgameslauncher", L"epiconlineservices", L"unrealcefsubprocess",
        L"easyanticheat", L"battleye", L"galaxyclient", L"gog",
        L"conhost", L"svchost", L"runtimebroker", L"dllhost",
        L"explorer", L"taskhostw", L"csrss", L"wininit", L"smss",
        nullptr
    };
    for (int i = 0; kSkip[i]; ++i)
        if (lowerName.find(kSkip[i]) != std::wstring::npos) return true;
    return false;
}

bool ProcessMonitor::LaunchUri(const std::wstring& uri, const std::wstring& exeHint,
                                int timeoutSec, DoneCallback cb) {
    // Snapshot all PIDs that exist BEFORE we launch so we can identify the new
    // game process as whichever one appears afterwards.
    std::set<DWORD> existingPids = SnapshotPids();

    HINSTANCE r = ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) return false;

    if (cb == nullptr) return true;

    // Lowercase the hint once for repeated comparisons.
    std::wstring hintLow = exeHint;
    for (auto& c : hintLow) c = towlower(c);

    m_running.store(true);
    m_watchThread = std::thread([this, timeoutSec, existingPids,
                                 hintLow, cb = std::move(cb)]() mutable {

        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(timeoutSec);
        HANDLE hFound = INVALID_HANDLE_VALUE;

        // Give the launcher a moment to spawn the game process.
        Sleep(2000);

        while (std::chrono::steady_clock::now() < deadline &&
               hFound == INVALID_HANDLE_VALUE) {

            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe{ sizeof(pe) };
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        // Only consider processes that weren't running before we launched.
                        if (existingPids.count(pe.th32ProcessID)) continue;

                        std::wstring nameLow = pe.szExeFile;
                        for (auto& c : nameLow) c = towlower(c);

                        if (!hintLow.empty()) {
                            // Precise hint (e.g. Epic exe filename) — match it.
                            if (nameLow.find(hintLow) == std::wstring::npos) continue;
                        } else {
                            // No hint (Steam) — skip known infrastructure, take
                            // the first new "real" process that appeared.
                            if (IsInfraProcess(nameLow)) continue;
                        }

                        hFound = OpenProcess(
                            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                            FALSE, pe.th32ProcessID);
                        if (hFound != INVALID_HANDLE_VALUE) break;

                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }

            if (hFound == INVALID_HANDLE_VALUE) Sleep(1500);
        }

        if (hFound != INVALID_HANDLE_VALUE) {
            m_hProcess = hFound;
            WatchThread(hFound, cb);
        } else {
            m_running.store(false);
        }
    });
    return true;
}

void ProcessMonitor::WatchThread(HANDLE hProcess, DoneCallback cb) {
    auto start = std::chrono::steady_clock::now();
    WaitForSingleObject(hProcess, INFINITE);
    auto end = std::chrono::steady_clock::now();
    uint64_t elapsed = (uint64_t)std::chrono::duration_cast<
        std::chrono::seconds>(end - start).count();
    CloseHandle(hProcess);
    m_hProcess = INVALID_HANDLE_VALUE;
    m_running.store(false);
    if (cb) cb(elapsed);
}

void ProcessMonitor::KillCurrent() {
    if (m_hProcess != INVALID_HANDLE_VALUE)
        TerminateProcess(m_hProcess, 0);
    if (m_watchThread.joinable())
        m_watchThread.join();
}
