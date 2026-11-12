@echo off
setlocal
cd /d "%~dp0"
if not defined QT_ROOT set "QT_ROOT=E:\ProgramData\anaconda3\Library"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=%QT_ROOT%"
if errorlevel 1 goto failed
cmake --build build --config Release --parallel 1
if errorlevel 1 goto failed
set "PATH=%QT_ROOT%\bin;%PATH%"
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 goto failed
if exist "run\" copy /y "build\Release\AxisForgeCAD.exe" "run\AxisForgeCAD.exe" >nul
echo Build and tests completed.
pause
exit /b 0
:failed
echo Build or tests failed. See messages above.
pause
exit /b 1
