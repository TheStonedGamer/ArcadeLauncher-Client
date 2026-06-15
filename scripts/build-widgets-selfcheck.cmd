@echo off
REM Build + run the portable widget-logic self-check under MSVC (standalone).
REM Locks widgets:: (push-button state machine) on Windows in addition to ctest.
setlocal
set ROOT=%~dp0..
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
  echo vcvars64.bat not found - adjust VCVARS in this script.
  exit /b 1
)
call %VCVARS% >nul
set OUT=%ROOT%\bin\widgets_selfcheck.exe
cl /nologo /std:c++17 /EHsc /utf-8 /I "%ROOT%\src" ^
  "%ROOT%\src\core\WidgetsSelfCheckMain.cpp" ^
  "%ROOT%\src\Widgets.cpp" ^
  /Fe:"%OUT%" /Fo:"%ROOT%\bin\\" || exit /b 1
echo.
"%OUT%"