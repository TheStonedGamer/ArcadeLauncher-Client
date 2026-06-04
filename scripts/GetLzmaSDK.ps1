# GetLzmaSDK.ps1
# Downloads the LZMA SDK C source files (public domain, Igor Pavlov) from the
# official 7-Zip GitHub repository into src\lzma\.
# Run this once before building. After it completes, rebuild the project.
# LZMA SDK extraction will then be compiled in (no external 7-Zip required).

param([string]$TargetDir = "")
if ($TargetDir -eq "") {
    $TargetDir = Join-Path $PSScriptRoot "..\src\lzma"
}
$TargetDir = [IO.Path]::GetFullPath($TargetDir)
New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null

$baseUrl = "https://raw.githubusercontent.com/ip7z/7zip/main/C"
$files = @(
    "Precomp.h", "Compiler.h", "RotateDefs.h",
    "Types.h", "7zTypes.h", "7zWindows.h", "Alloc.h", "Alloc.c",
    "7zArcIn.c", "7zAlloc.c", "7zAlloc.h", "7zBuf.c", "7zBuf.h", "7zBuf2.c",
    "CpuArch.h", "CpuArch.c",
    "LzmaDec.h", "LzmaDec.c",
    "Lzma2Dec.h", "Lzma2Dec.c",
    "7z.h", "7zCrc.h", "7zCrc.c", "7zCrcOpt.c",
    "7zDec.c", "7zFile.h", "7zFile.c", "7zStream.c",
    "Bra.h", "Bra.c", "Bra86.c", "BraIA64.c",
    "Bcj2.h", "Bcj2.c",
    "Delta.h", "Delta.c",
    "Ppmd.h", "Ppmd7.h", "Ppmd7.c", "Ppmd7Dec.c"
) | Select-Object -Unique

$ok = 0; $skip = 0
foreach ($f in $files) {
    $dest = Join-Path $TargetDir $f
    try {
        Invoke-WebRequest "$baseUrl/$f" -OutFile $dest -UseBasicParsing -ErrorAction Stop
        Write-Host "  OK  $f"
        $ok++
    } catch {
        Write-Warning "  --  $f (not found, skipping)"
        $skip++
    }
}
Write-Host ""
Write-Host "Done: $ok files downloaded to $TargetDir ($skip skipped)"
Write-Host "Rebuild the project to compile the LZMA SDK in."
