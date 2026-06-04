#pragma once
#include "pch.h"

// Extracts a .zip or .7z archive to destDir.
//
// ZIP  — uses PowerShell Expand-Archive (Windows 10+, no extra tools needed).
// 7z   — uses the embedded LZMA SDK when HAVE_LZMA_SDK is defined
//        (run scripts\GetLzmaSDK.ps1 once, then rebuild), otherwise falls
//        back to a system-installed 7-Zip or 7za.exe.
//
// Returns true on success.
bool ExtractArchive(const std::wstring& archivePath, const std::wstring& destDir);

// Returns path to 7z.exe / 7za.exe, or empty string if not found.
std::wstring Find7Zip();
