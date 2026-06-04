#include "pch.h"
#include "ArchiveExtractor.h"
#include <cstdlib>

#ifdef HAVE_LZMA_SDK
extern "C" {
#include "lzma/7z.h"
#include "lzma/7zFile.h"
#include "lzma/7zCrc.h"
#include "lzma/Alloc.h"
}
#endif

// ── System 7-Zip finder ───────────────────────────────────────────────────────

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.rfind(L'\\');
    return pos != std::wstring::npos ? p.substr(0, pos) : p;
}

std::wstring Find7Zip() {
    wchar_t ad[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, ad);
    std::wstring tools = std::wstring(ad) + L"\\ArcadeLauncher\\tools\\";
    std::vector<std::wstring> candidates = {
        ExeDir() + L"\\7za.exe",
        ExeDir() + L"\\7z.exe",
        L"C:\\Program Files\\7-Zip\\7z.exe",
        L"C:\\Program Files (x86)\\7-Zip\\7z.exe",
        tools + L"7za.exe",
        tools + L"7z.exe",
    };
    for (auto& p : candidates)
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    return {};
}

// ── ZIP via PowerShell Expand-Archive ─────────────────────────────────────────

static bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir) {
    // PowerShell 5.1 (built into Windows 10+) handles ZIP natively.
    std::wstring cmd =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass "
        L"-Command \"Expand-Archive -LiteralPath '" + zipPath +
        L"' -DestinationPath '" + destDir + L"' -Force\"";

    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(L'\0');
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;

    WaitForSingleObject(pi.hProcess, 5 * 60 * 1000); // 5-minute timeout
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}

// ── 7z via embedded LZMA SDK ──────────────────────────────────────────────────

#ifdef HAVE_LZMA_SDK

static bool Extract7z_SDK(const std::wstring& archivePath,
                           const std::wstring& destDir) {
    CFileInStream archStream;
    CLookToRead2  lookStream;

    FileInStream_CreateVTable(&archStream);
    LookToRead2_CreateVTable(&lookStream, False);

    if (InFile_OpenW(&archStream.file, archivePath.c_str()) != 0)
        return false;

    const size_t kBufSize = 1u << 18; // 256 KB
    lookStream.buf       = (Byte*)ISzAlloc_Alloc(&g_Alloc, kBufSize);
    lookStream.bufSize   = kBufSize;
    lookStream.realStream = &archStream.vt;
    LookToRead2_INIT(&lookStream);

    CrcGenerateTable();

    CSzArEx db;
    SzArEx_Init(&db);
    SRes res = SzArEx_Open(&db, &lookStream.vt, &g_Alloc, &g_Alloc);
    if (res != SZ_OK) {
        SzArEx_Free(&db, &g_Alloc);
        ISzAlloc_Free(&g_Alloc, lookStream.buf);
        File_Close(&archStream.file);
        return false;
    }

    UInt32 blockIndex  = 0xFFFFFFFF;
    Byte  *outBuf      = nullptr;
    size_t outBufSize  = 0;
    bool   success     = true;

    for (UInt32 i = 0; i < db.NumFiles && success; i++) {
        // Build output path
        size_t nameLen = SzArEx_GetFileNameUtf16(&db, i, nullptr);
        std::vector<UInt16> name16(nameLen);
        SzArEx_GetFileNameUtf16(&db, i, name16.data());
        std::wstring rel(reinterpret_cast<wchar_t*>(name16.data()));
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        std::wstring fullPath = destDir + L"\\" + rel;

        if (SzArEx_IsDir(&db, i)) {
            SHCreateDirectoryExW(nullptr, fullPath.c_str(), nullptr);
            continue;
        }

        size_t offset = 0, outSize = 0;
        res = SzArEx_Extract(&db, &lookStream.vt, i,
                             &blockIndex, &outBuf, &outBufSize,
                             &offset, &outSize,
                             &g_Alloc, &g_Alloc);
        if (res != SZ_OK) { success = false; break; }

        // Ensure parent directory exists
        auto slash = fullPath.rfind(L'\\');
        if (slash != std::wstring::npos)
            SHCreateDirectoryExW(nullptr, fullPath.substr(0, slash).c_str(), nullptr);

        // Write extracted file
        HANDLE hf = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hf, outBuf + offset, (DWORD)outSize, &written, nullptr);
            CloseHandle(hf);
            if (written != (DWORD)outSize) success = false;
        } else {
            success = false;
        }
    }

    ISzAlloc_Free(&g_Alloc, outBuf);
    SzArEx_Free(&db, &g_Alloc);
    ISzAlloc_Free(&g_Alloc, lookStream.buf);
    File_Close(&archStream.file);
    return success;
}

#endif // HAVE_LZMA_SDK

// ── 7z via external system 7-Zip (fallback) ───────────────────────────────────

static bool Extract7z_External(const std::wstring& archivePath,
                                const std::wstring& destDir) {
    std::wstring sz = Find7Zip();
    if (sz.empty()) return false;

    std::wstring cmd = L"\"" + sz + L"\" x \"" + archivePath +
                       L"\" -o\"" + destDir + L"\" -y -aoa";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(L'\0');
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return exitCode == 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool ExtractArchive(const std::wstring& archivePath, const std::wstring& destDir) {
    std::wstring lower = archivePath;
    for (auto& c : lower) c = towlower(c);

    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".zip") == 0)
        return ExtractZip(archivePath, destDir);

    if (lower.size() >= 3 && lower.compare(lower.size() - 3, 3, L".7z") == 0) {
#ifdef HAVE_LZMA_SDK
        return Extract7z_SDK(archivePath, destDir);
#else
        return Extract7z_External(archivePath, destDir);
#endif
    }

    // Unknown format: try external 7-Zip as a last resort
    return Extract7z_External(archivePath, destDir);
}
