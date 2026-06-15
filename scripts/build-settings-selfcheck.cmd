@echo off
REM Build + run the portable General-settings-model self-check under MSVC (standalone).
REM Locks settings:: (parse + non-destructive apply + panel bind) on Windows alongside ctest.
setlocal
set ROOT=%~dp0..
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
  echo vcvars64.bat not found - adjust VCVARS in this script.
  exit /b 1
)
call %VCVARS% >nul
set OUT=%ROOT%\bin\settings_selfcheck.exe
cl /nologo /std:c++17 /EHsc /utf-8 /I "%ROOT%\src" ^
  "%ROOT%\src\core\SettingsSelfCheckMain.cpp" ^
  "%ROOT%\src\Settings.cpp" ^
  "%ROOT%\src\Forms.cpp" ^
  "%ROOT%\src\Widgets.cpp" ^
  /Fe:"%OUT%" /Fo:"%ROOT%\bin\\" || exit /b 1
echo.
"%OUT%"