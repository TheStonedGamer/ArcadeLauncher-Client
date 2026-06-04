# Downloads the SQLite amalgamation (sqlite3.c + sqlite3.h) into src\sqlite\
# Run once; subsequent builds skip if the header already exists.
$dest = "$PSScriptRoot\..\src\sqlite"
if (Test-Path "$dest\sqlite3.h") { exit 0 }

$url  = "https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
$tmp  = "$env:TEMP\sqlite_amal.zip"

Write-Host "Downloading SQLite amalgamation..."
Invoke-WebRequest $url -OutFile $tmp -UseBasicParsing
Expand-Archive   $tmp "$env:TEMP\sqlite_amal" -Force

$src = Get-ChildItem "$env:TEMP\sqlite_amal" -Recurse -Filter "sqlite3.h" |
       Select-Object -First 1
if (-not $src) { Write-Error "sqlite3.h not found in archive"; exit 1 }

New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item (Join-Path $src.DirectoryName "sqlite3.h") $dest
Copy-Item (Join-Path $src.DirectoryName "sqlite3.c") $dest

Remove-Item $tmp -Force
Remove-Item "$env:TEMP\sqlite_amal" -Recurse -Force
Write-Host "SQLite ready at $dest"
