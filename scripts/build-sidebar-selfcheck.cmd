@echo off
REM Build + run the portable dynamic-sidebar self-check under MSVC (standalone).
REM Locks sidebar:: on Windows in addition to the Linux ctest gate.
setlocal
set ROOT=%~dp0..
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
  echo vcvars64.bat not found - adjust VCVARS in this script.
  exit /b 1
)
call %VCVARS% >nul
set OUT=%ROOT%\bin\sidebar_selfcheck.exe
cl /nologo /std:c++17 /EHsc /utf-8 /I "%ROOT%\src" ^
  "%ROOT%\src\core\SidebarSelfCheckMain.cpp" ^
  "%ROOT%\src\SidebarModel.cpp" ^
  "%ROOT%\src\GameFilter.cpp" ^
  /Fe:"%OUT%" /Fo:"%ROOT%\bin\\" || exit /b 1
echo.
"%OUT%"