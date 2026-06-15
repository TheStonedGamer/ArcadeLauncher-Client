@echo off
REM Build + run the portable settings-panel self-check under MSVC (standalone).
REM Locks forms:: (panel layout/focus/event/readback) on Windows in addition to ctest.
setlocal
set ROOT=%~dp0..
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
  echo vcvars64.bat not found - adjust VCVARS in this script.
  exit /b 1
)
call %VCVARS% >nul
set OUT=%ROOT%\bin\forms_selfcheck.exe
cl /nologo /std:c++17 /EHsc /utf-8 /I "%ROOT%\src" ^
  "%ROOT%\src\core\FormsSelfCheckMain.cpp" ^
  "%ROOT%\src\Forms.cpp" ^
  "%ROOT%\src\Widgets.cpp" ^
  /Fe:"%OUT%" /Fo:"%ROOT%\bin\\" || exit /b 1
echo.
"%OUT%"