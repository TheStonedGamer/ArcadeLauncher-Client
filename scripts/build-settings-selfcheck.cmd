@echo off
REM Build + run the portable General-settings-model self-check under MSVC (standalone).
REM Locks settings:: (parse + non-destructive apply + panel bind + file round-trip).
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
  "%ROOT%\src\Platform\Paths.cpp" ^
  "%ROOT%\src\Platform\Text.cpp" ^
  /Fe:"%OUT%" /Fo:"%ROOT%\bin\\" shell32.lib ole32.lib || exit /b 1
echo.
"%OUT%"