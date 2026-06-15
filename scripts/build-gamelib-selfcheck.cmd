@echo off
REM Build + run the GameLibrary Save/Load round-trip self-check.
REM Standalone (not part of the launcher exe): sets up the MSVC environment,
REM compiles GameLibrary + the portable logic TUs + the self-check, runs it.
REM /utf-8 so the Unicode test literals decode correctly.
setlocal
set ROOT=%~dp0..
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
  echo vcvars64.bat not found - adjust VCVARS in this script.
  exit /b 1
)
call %VCVARS% >nul
set OUT=%ROOT%\bin\gamelib_selfcheck.exe
cl /nologo /std:c++17 /EHsc /utf-8 /I "%ROOT%\src" ^
  "%ROOT%\src\core\GameLibrarySelfCheckMain.cpp" ^
  "%ROOT%\src\GameLibrary.cpp" ^
  "%ROOT%\src\GameVariants.cpp" ^
  "%ROOT%\src\GameSearch.cpp" ^
  "%ROOT%\src\Platform\Text.cpp" ^
  /Fe:"%OUT%" /Fo:"%ROOT%\bin\\" || exit /b 1
echo.
"%OUT%"
