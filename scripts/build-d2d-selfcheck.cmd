@echo off
REM Build + run the Direct2D IRenderer2D self-check (Linux port L1b).
REM Standalone (not part of the launcher exe): sets up the MSVC environment,
REM compiles the shared portable TUs + the D2D backend + the self-check, runs it.
setlocal
set ROOT=%~dp0..
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
  echo vcvars64.bat not found - adjust VCVARS in this script.
  exit /b 1
)
call %VCVARS% >nul
set OUT=%ROOT%\bin\d2d_selfcheck.exe
cl /nologo /std:c++17 /EHsc /I "%ROOT%\src" ^
  "%ROOT%\src\core\RendererD2DSelfCheckMain.cpp" ^
  "%ROOT%\src\Platform\win\RendererD2D.cpp" ^
  "%ROOT%\src\CatalogGridView.cpp" ^
  "%ROOT%\src\GridLayout.cpp" ^
  "%ROOT%\src\Platform\Text.cpp" ^
  /Fe:"%OUT%" /Fo:"%ROOT%\bin\\" || exit /b 1
echo.
"%OUT%"
