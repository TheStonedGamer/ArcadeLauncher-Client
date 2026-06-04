#Requires -Version 5.1
<#
.SYNOPSIS
    Full build + MSI packaging pipeline for ArcadeLauncher.

.DESCRIPTION
    1. Finds MSBuild via vswhere
    2. Builds the solution (Release|x64 by default)
    3. Installs WiX v4 CLI if not already present
    4. Builds the MSI into dist\ArcadeLauncher-x64.msi

.PARAMETER Config
    Build configuration -- "Release" (default) or "Debug".

.PARAMETER SkipBuild
    Skip the MSBuild step (use existing bin\Release output).

.PARAMETER SkipPackage
    Build the exe but skip MSI packaging.

.PARAMETER Sign
    Attempt to sign the exe and MSI with a code-signing certificate.
    Requires SignTool.exe and a valid certificate in the current user's My store.
    Set $env:SIGN_THUMBPRINT to the certificate thumbprint, or the script will
    pick the first available code-signing cert.

.EXAMPLE
    # Normal release build + MSI
    .\scripts\build.ps1

    # Rebuild only, no MSI
    .\scripts\build.ps1 -SkipPackage

    # Package from an existing exe (fast iteration on installer changes)
    .\scripts\build.ps1 -SkipBuild

    # Signed release
    $env:SIGN_THUMBPRINT = "ABCDEF..."
    .\scripts\build.ps1 -Sign
#>
param(
    [ValidateSet("Release","Debug")]
    [string]$Config     = "Release",
    [switch]$SkipBuild,
    [switch]$SkipPackage,
    [switch]$Sign
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root    = [IO.Path]::GetFullPath("$PSScriptRoot\..")
$BinDir  = "$Root\bin\$Config"
$DistDir = "$Root\dist"
$Sln     = "$Root\ArcadeLauncher-Client.sln"
$Wxs     = "$Root\installer\ArcadeLauncher.wxs"
$OutMsi  = "$DistDir\ArcadeLauncher-x64.msi"
$Exe     = "$BinDir\ArcadeLauncher.exe"

$WixVersion = "4.0.5"
$WixUIExt   = "WixToolset.UI.wixext/$WixVersion"

# --- Helpers ------------------------------------------------------------------

function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host "  >>> $msg" -ForegroundColor Cyan
}

function Invoke-Checked {
    param([scriptblock]$sb)
    & $sb
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE." }
}

function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio 2022 with the 'Desktop development with C++' workload."
    }
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild `
                          -find "MSBuild\**\Bin\MSBuild.exe" 2>$null |
               Select-Object -First 1
    if (-not $msbuild -or -not (Test-Path $msbuild)) {
        throw "MSBuild not found via vswhere. Ensure the C++ workload is installed."
    }
    return $msbuild
}

function Ensure-WiX {
    # 1. Already on PATH?
    $wix = (Get-Command wix -ErrorAction SilentlyContinue)
    if ($wix) { return $wix.Source }

    # 2. Installed as a dotnet global tool but PATH not yet refreshed?
    $dotnetToolsWix = "$env:USERPROFILE\.dotnet\tools\wix.exe"
    if (Test-Path $dotnetToolsWix) { return $dotnetToolsWix }

    # 3. Install it now
    Write-Host "  WiX CLI not found -- installing via dotnet tool..." -ForegroundColor Yellow

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        throw "dotnet SDK not found. Install the .NET SDK from https://dotnet.microsoft.com/download"
    }

    dotnet tool install --global wix --version $WixVersion
    if ($LASTEXITCODE -ne 0) { throw "dotnet tool install wix failed." }

    if (-not (Test-Path $dotnetToolsWix)) {
        throw "wix.exe not found at $dotnetToolsWix after install. Restart the shell and try again."
    }
    return $dotnetToolsWix
}

function Find-SignTool {
    $signtool = (Get-Command signtool -ErrorAction SilentlyContinue)
    if ($signtool) { return $signtool.Source }
    $sdk = "C:\Program Files (x86)\Windows Kits\10\bin"
    $candidates = Get-ChildItem "$sdk\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
                  Sort-Object FullName -Descending
    if ($candidates) { return $candidates[0].FullName }
    throw "signtool.exe not found. Install the Windows 10 SDK."
}

function Sign-File([string]$path, [string]$signtool) {
    $thumb = $env:SIGN_THUMBPRINT
    if (-not $thumb) {
        $cert = Get-ChildItem Cert:\CurrentUser\My |
                Where-Object { $_.EnhancedKeyUsageList.FriendlyName -contains "Code Signing" } |
                Select-Object -First 1
        if (-not $cert) { throw "No code-signing certificate found in CurrentUser\My." }
        $thumb = $cert.Thumbprint
        Write-Host "  Using certificate: $($cert.Subject) ($thumb)"
    }
    Invoke-Checked {
        & $signtool sign /sha1 $thumb /tr "http://timestamp.digicert.com" /td sha256 /fd sha256 $path
    }
}

# --- Step 0: Header -----------------------------------------------------------

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "   ArcadeLauncher Build Pipeline" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Config  : $Config"
Write-Host "  Root    : $Root"
Write-Host "  BinDir  : $BinDir"
Write-Host "  Output  : $OutMsi"

# --- Step 1: Build ------------------------------------------------------------

if (-not $SkipBuild) {
    Write-Step "Building $Config|x64"
    $msbuild = Find-MSBuild
    Write-Host "  MSBuild : $msbuild"
    Invoke-Checked {
        & $msbuild $Sln /p:Configuration=$Config /p:Platform=x64 /m /nologo `
            /p:TreatWarningsAsErrors=false
    }
    Write-Host "  Build succeeded  -->  $Exe" -ForegroundColor Green
} else {
    Write-Host "  (skipping build -- using existing $Exe)"
}

if (-not (Test-Path $Exe)) {
    throw "Executable not found at: $Exe`nRun without -SkipBuild first."
}

# --- Step 2: Sign exe (optional) ----------------------------------------------

if ($Sign) {
    Write-Step "Signing executable"
    $st = Find-SignTool
    Sign-File $Exe $st
    Write-Host "  Signed $Exe" -ForegroundColor Green
}

# --- Step 3: Package MSI ------------------------------------------------------

if (-not $SkipPackage) {
    Write-Step "Packaging MSI"
    New-Item -ItemType Directory -Force $DistDir | Out-Null

    $wixExe = Ensure-WiX

    Invoke-Checked { & $wixExe extension add $WixUIExt --global 2>$null }

    # Read version from Version.h (single source of truth)
    $versionH = Get-Content "$Root\src\Version.h" -Raw
    $major = [regex]::Match($versionH, '#define ARCADE_VERSION_MAJOR\s+(\d+)').Groups[1].Value
    $minor = [regex]::Match($versionH, '#define ARCADE_VERSION_MINOR\s+(\d+)').Groups[1].Value
    $patch = [regex]::Match($versionH, '#define ARCADE_VERSION_PATCH\s+(\d+)').Groups[1].Value
    $productVersion = "$major.$minor.$patch"
    Write-Host "  Version : $productVersion"

    Write-Host "  Running: wix build ..."
    Invoke-Checked {
        & $wixExe build $Wxs `
            -ext WixToolset.UI.wixext `
            -d "BinDir=$BinDir" `
            -d "ProductVersion=$productVersion" `
            -out $OutMsi `
            -arch x64
    }
    Write-Host "  MSI created  -->  $OutMsi" -ForegroundColor Green

    if ($Sign) {
        Write-Step "Signing MSI"
        $st = Find-SignTool
        Sign-File $OutMsi $st
        Write-Host "  Signed $OutMsi" -ForegroundColor Green
    }

    $size = [math]::Round((Get-Item $OutMsi).Length / 1MB, 2)
    Write-Host ""
    Write-Host "  Done!  $OutMsi  ($size MB)" -ForegroundColor Green
} else {
    Write-Host "  (skipping MSI packaging)"
}
