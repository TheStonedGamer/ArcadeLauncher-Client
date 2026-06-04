#pragma once
#include "pch.h"

// Watches a launched process and reports elapsed seconds when it exits.
class ProcessMonitor {
public:
    using DoneCallback = std::function<void(uint64_t elapsedSeconds)>;

    ProcessMonitor() = default;
    ~ProcessMonitor();

    // Launch exe with args, watch it, call cb when it exits.
    bool Launch(const std::wstring& exe, const std::wstring& args,
                const std::wstring& workDir, DoneCallback cb);

    // Launch a URI (steam://, com.epicgames.launcher://) then watch for
    // a child process matching exeHint to appear within timeoutSec.
    bool LaunchUri(const std::wstring& uri, const std::wstring& exeHint,
                   int timeoutSec, DoneCallback cb);

    bool IsRunning() const { return m_running.load(); }
    void KillCurrent();

private:
    void WatchThread(HANDLE hProcess, DoneCallback cb);

    HANDLE              m_hProcess = INVALID_HANDLE_VALUE;
    std::thread         m_watchThread;
    std::atomic<bool>   m_running{ false };
};
